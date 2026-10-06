#include "xai/contracts.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace xai::contracts {
namespace {
constexpr std::size_t kMaximumRecordBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumDepth = 64;
constexpr std::size_t kMaximumNodes = 1'000'000;

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead <= 0x7fU) { ++i; continue; }
        std::size_t width = 0;
        std::uint32_t code = 0;
        if (lead >= 0xc2U && lead <= 0xdfU) { width = 2; code = lead & 0x1fU; }
        else if (lead >= 0xe0U && lead <= 0xefU) { width = 3; code = lead & 0x0fU; }
        else if (lead >= 0xf0U && lead <= 0xf4U) { width = 4; code = lead & 0x07U; }
        else return false;
        if (i + width > text.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (continuation & 0x3fU);
        }
        if ((width == 3 && code < 0x800U) || (width == 4 && code < 0x10000U) ||
            code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
        ++i;
        for (std::size_t j = 1; j < width; ++j) ++i;
    }
    return true;
}

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

void append_string(std::string& output, std::string_view value) {
    if (!valid_utf8(value)) fail("string is not valid UTF-8");
    output.push_back('"');
    constexpr char digits[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20U) {
                output += "\\u00";
                output.push_back(digits[(ch >> 4U) & 0xfU]);
                output.push_back(digits[ch & 0xfU]);
            } else output.push_back(static_cast<char>(ch));
        }
    }
    output.push_back('"');
}

void append_value(std::string& output, const CanonicalValue& value, std::size_t depth) {
    if (depth > kMaximumDepth) fail("record nesting exceeds the canonical depth limit");
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
        else if constexpr (std::is_same_v<T, bool>) output += item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::int64_t> ||
                           std::is_same_v<T, std::uint64_t>) output += std::to_string(item);
        else if constexpr (std::is_same_v<T, std::string>) append_string(output, item);
        else if constexpr (std::is_same_v<T, CanonicalValue::Array>) {
            output.push_back('[');
            bool first = true;
            for (const auto& child : item) {
                if (!first) output.push_back(',');
                first = false;
                append_value(output, child, depth + 1);
            }
            output.push_back(']');
        } else if constexpr (std::is_same_v<T, CanonicalValue::Object>) {
            output.push_back('{');
            bool first = true;
            for (const auto& [key, child] : item) {
                if (!first) output.push_back(',');
                first = false;
                append_string(output, key);
                output.push_back(':');
                append_value(output, child, depth + 1);
            }
            output.push_back('}');
        }
    }, value.data);
}

[[nodiscard]] std::string serialize_value(const CanonicalValue& value) {
    std::string output;
    append_value(output, value, 0);
    return output;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {
        if (input.size() > kMaximumRecordBytes) fail("record exceeds the 16 MiB size limit");
    }
    [[nodiscard]] CanonicalValue parse() {
        skip_space();
        auto value = parse_value(0);
        skip_space();
        if (position_ != input_.size()) fail("trailing bytes after JSON record");
        return value;
    }
private:
    std::string_view input_;
    std::size_t position_{0};
    std::size_t nodes_{0};

    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
    }
    [[nodiscard]] char peek() const {
        if (position_ >= input_.size()) fail("unexpected end of JSON record");
        return input_[position_];
    }
    char take() {
        const char result = peek();
        ++position_;
        return result;
    }
    void expect(char wanted) {
        if (take() != wanted) fail("unexpected JSON token");
    }
    void count_node() {
        if (++nodes_ > kMaximumNodes) fail("record exceeds the JSON node limit");
    }
    [[nodiscard]] unsigned int hex4() {
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = take();
            unsigned int digit = 0;
            if (ch >= '0' && ch <= '9') digit = static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') digit = 10U + static_cast<unsigned int>(ch - 'a');
            else if (ch >= 'A' && ch <= 'F') digit = 10U + static_cast<unsigned int>(ch - 'A');
            else fail("invalid Unicode escape in JSON string");
            value = value * 16U + digit;
        }
        return value;
    }
    static void append_codepoint(std::string& output, std::uint32_t code) {
        if (code <= 0x7fU) output.push_back(static_cast<char>(code));
        else if (code <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (code >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
        } else if (code <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (code >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (code >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
        }
    }
    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string output;
        while (true) {
            const unsigned char ch = static_cast<unsigned char>(take());
            if (ch == '"') break;
            if (ch < 0x20U) fail("unescaped control byte in JSON string");
            if (ch != '\\') { output.push_back(static_cast<char>(ch)); continue; }
            const char escaped = take();
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t code = hex4();
                if (code >= 0xd800U && code <= 0xdbffU) {
                    if (take() != '\\' || take() != 'u') fail("unpaired high surrogate");
                    const std::uint32_t low = hex4();
                    if (low < 0xdc00U || low > 0xdfffU) fail("invalid low surrogate");
                    code = 0x10000U + ((code - 0xd800U) << 10U) + (low - 0xdc00U);
                } else if (code >= 0xdc00U && code <= 0xdfffU) fail("unpaired low surrogate");
                append_codepoint(output, code);
                break;
            }
            default: fail("invalid escape in JSON string");
            }
        }
        if (!valid_utf8(output)) fail("JSON string is not valid UTF-8");
        return output;
    }
    [[nodiscard]] CanonicalValue parse_number() {
        const std::size_t begin = position_;
        if (peek() == '-') ++position_;
        if (position_ >= input_.size()) fail("incomplete JSON integer");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                fail("leading zero in JSON integer");
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') fail("invalid JSON integer");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == '.' || input_[position_] == 'e' ||
                                           input_[position_] == 'E'))
            fail("floating-point JSON numbers are forbidden; encode exact values explicitly");
        const auto token = input_.substr(begin, position_ - begin);
        if (token.front() == '-') {
            std::int64_t number = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
                fail("JSON integer is outside signed 64-bit range");
            return CanonicalValue{number};
        }
        std::uint64_t number = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
            fail("JSON integer is outside unsigned 64-bit range");
        return CanonicalValue{number};
    }
    [[nodiscard]] CanonicalValue parse_value(std::size_t depth) {
        if (depth > kMaximumDepth) fail("record nesting exceeds the canonical depth limit");
        count_node();
        skip_space();
        const char ch = peek();
        if (ch == '"') return CanonicalValue{parse_string()};
        if (ch == '{') {
            ++position_;
            CanonicalValue::Object object;
            skip_space();
            if (peek() == '}') { ++position_; return CanonicalValue{std::move(object)}; }
            while (true) {
                skip_space();
                if (peek() != '"') fail("JSON object key must be a string");
                auto key = parse_string();
                skip_space();
                expect(':');
                auto value = parse_value(depth + 1);
                if (!object.emplace(std::move(key), std::move(value)).second)
                    fail("duplicate key in JSON object");
                skip_space();
                const char separator = take();
                if (separator == '}') break;
                if (separator != ',') fail("invalid JSON object separator");
            }
            return CanonicalValue{std::move(object)};
        }
        if (ch == '[') {
            ++position_;
            CanonicalValue::Array array;
            skip_space();
            if (peek() == ']') { ++position_; return CanonicalValue{std::move(array)}; }
            while (true) {
                array.push_back(parse_value(depth + 1));
                skip_space();
                const char separator = take();
                if (separator == ']') break;
                if (separator != ',') fail("invalid JSON array separator");
            }
            return CanonicalValue{std::move(array)};
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) return parse_number();
        const auto parse_keyword = [&](std::string_view word, CanonicalValue value) {
            if (input_.substr(position_, word.size()) != word) fail("invalid JSON value");
            position_ += word.size();
            return value;
        };
        if (ch == 't') return parse_keyword("true", CanonicalValue{true});
        if (ch == 'f') return parse_keyword("false", CanonicalValue{false});
        if (ch == 'n') return parse_keyword("null", CanonicalValue{nullptr});
        fail("invalid JSON value");
    }
};

template <typename T>
[[nodiscard]] std::string_view id_kind(const T&) {
    if constexpr (std::is_same_v<T, ObservationId>) return "observation";
    else if constexpr (std::is_same_v<T, SourceId>) return "source";
    else if constexpr (std::is_same_v<T, EvidenceId>) return "evidence";
    else if constexpr (std::is_same_v<T, FactId>) return "fact";
    else if constexpr (std::is_same_v<T, EntityId>) return "entity";
    else if constexpr (std::is_same_v<T, ModelId>) return "model";
    else if constexpr (std::is_same_v<T, SchemaId>) return "schema";
    else if constexpr (std::is_same_v<T, CodeBuildId>) return "code_build";
    else if constexpr (std::is_same_v<T, PartitionId>) return "partition";
    else if constexpr (std::is_same_v<T, QueryId>) return "query";
    else if constexpr (std::is_same_v<T, CertificateId>) return "certificate";
    else return "result";
}

template <typename T>
[[nodiscard]] CanonicalValue optional_id(const std::optional<T>& value) {
    return value ? CanonicalValue{value->value()} : CanonicalValue{nullptr};
}

[[nodiscard]] const std::string& as_string(const CanonicalValue& value);

template <typename T>
[[nodiscard]] std::optional<T> parse_optional_id(const CanonicalValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) return std::nullopt;
    return T{as_string(value)};
}

[[nodiscard]] CanonicalValue::Object resource_budget_value(const ResourceBudget& budget) {
    const auto optional_number = [](const std::optional<std::uint64_t>& value) {
        return value ? CanonicalValue{*value} : CanonicalValue{nullptr};
    };
    return {{"cpu_time_ms", optional_number(budget.cpu_time_ms)},
            {"memory_bytes", optional_number(budget.memory_bytes)},
            {"operations", optional_number(budget.operations)}};
}
[[nodiscard]] CanonicalValue::Object usage_value(const ResourceUsage& usage) {
    return {{"cpu_time_ms", CanonicalValue{usage.cpu_time_ms}},
            {"memory_bytes", CanonicalValue{usage.memory_bytes}},
            {"operations", CanonicalValue{usage.operations}}};
}

[[nodiscard]] CanonicalValue diagnostic_value(const Diagnostic& diagnostic) {
    CanonicalValue::Object object{{"code", CanonicalValue{std::string(to_string(diagnostic.code))}},
                                  {"message", CanonicalValue{diagnostic.message}}};
    object.emplace("field_path", diagnostic.field_path ? CanonicalValue{*diagnostic.field_path}
                                                        : CanonicalValue{nullptr});
    return CanonicalValue{std::move(object)};
}
[[nodiscard]] CanonicalValue diagnostics_value(const std::vector<Diagnostic>& diagnostics) {
    CanonicalValue::Array array;
    array.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) array.push_back(diagnostic_value(diagnostic));
    return CanonicalValue{std::move(array)};
}
[[nodiscard]] CanonicalValue lineage_value(const std::vector<LineageEntry>& lineage) {
    CanonicalValue::Array array;
    array.reserve(lineage.size());
    for (const auto& entry : lineage) {
        CanonicalValue::Object object;
        std::visit([&](const auto& id) {
            object.emplace("id", CanonicalValue{id.value()});
            object.emplace("kind", CanonicalValue{std::string(id_kind(id))});
        }, entry.identifier);
        object.emplace("relation", CanonicalValue{std::string(to_string(entry.relation))});
        array.emplace_back(std::move(object));
    }
    return CanonicalValue{std::move(array)};
}

[[nodiscard]] CanonicalValue record_value(const RecordEnvelope& record) {
    CanonicalValue::Object identity{
        {"certificate_id", optional_id(record.identity.certificate_id)},
        {"code_build_id", CanonicalValue{record.identity.code_build_id.value()}},
        {"entity_id", optional_id(record.identity.entity_id)},
        {"evidence_id", optional_id(record.identity.evidence_id)},
        {"fact_id", optional_id(record.identity.fact_id)},
        {"model_id", optional_id(record.identity.model_id)},
        {"observation_id", optional_id(record.identity.observation_id)},
        {"partition", CanonicalValue{std::string(to_string(record.identity.partition))}},
        {"partition_id", optional_id(record.identity.partition_id)},
        {"query_id", optional_id(record.identity.query_id)},
        {"result_id", CanonicalValue{record.identity.result_id.value()}},
        {"run_id", CanonicalValue{record.identity.run_id.value()}},
        {"schema_id", CanonicalValue{record.identity.schema_id.value()}},
        {"source_id", optional_id(record.identity.source_id)}};
    CanonicalValue::Array assumptions;
    for (const auto& assumption : record.assumptions) assumptions.emplace_back(assumption);
    CanonicalValue::Object artifact_hashes;
    for (const auto& [name, hash] : record.reproducibility.artifact_sha256)
        artifact_hashes.emplace(name, CanonicalValue{hash});
    CanonicalValue::Object reproducibility{
        {"algorithm", CanonicalValue{record.reproducibility.algorithm}},
        {"artifact_sha256", CanonicalValue{std::move(artifact_hashes)}},
        {"format_version", CanonicalValue{static_cast<std::uint64_t>(record.reproducibility.format_version)}},
        {"seed", record.reproducibility.seed ? CanonicalValue{*record.reproducibility.seed}
                                               : CanonicalValue{nullptr}}};
    CanonicalValue::Object root{
        {"assumptions", CanonicalValue{std::move(assumptions)}},
        {"budget", CanonicalValue{resource_budget_value(record.budget)}},
        {"consumed", CanonicalValue{usage_value(record.consumed)}},
        {"envelope_version", CanonicalValue{static_cast<std::uint64_t>(record.envelope_version)}},
        {"errors", diagnostics_value(record.errors)},
        {"exactness", CanonicalValue{std::string(to_string(record.exactness))}},
        {"identity", CanonicalValue{std::move(identity)}},
        {"lineage", lineage_value(record.lineage)},
        {"reproducibility", CanonicalValue{std::move(reproducibility)}},
        {"result", record.result ? *record.result : CanonicalValue{nullptr}},
        {"schema_version", CanonicalValue{static_cast<std::uint64_t>(record.schema_version)}},
        {"status", CanonicalValue{std::string(to_string(record.status))}},
        {"warnings", diagnostics_value(record.warnings)}};
    return CanonicalValue{std::move(root)};
}

using Object = CanonicalValue::Object;
[[nodiscard]] const Object& as_object(const CanonicalValue& value) {
    const auto* result = std::get_if<Object>(&value.data);
    if (!result) fail("expected JSON object");
    return *result;
}
[[nodiscard]] const CanonicalValue::Array& as_array(const CanonicalValue& value) {
    const auto* result = std::get_if<CanonicalValue::Array>(&value.data);
    if (!result) fail("expected JSON array");
    return *result;
}
[[nodiscard]] const std::string& as_string(const CanonicalValue& value) {
    const auto* result = std::get_if<std::string>(&value.data);
    if (!result) fail("expected JSON string");
    return *result;
}
[[nodiscard]] std::uint64_t as_u64(const CanonicalValue& value) {
    if (const auto* result = std::get_if<std::uint64_t>(&value.data)) return *result;
    if (const auto* result = std::get_if<std::int64_t>(&value.data); result && *result >= 0)
        return static_cast<std::uint64_t>(*result);
    fail("expected non-negative JSON integer");
}
[[nodiscard]] std::uint32_t as_u32(const CanonicalValue& value) {
    const auto number = as_u64(value);
    if (number > std::numeric_limits<std::uint32_t>::max()) fail("integer exceeds uint32 range");
    return static_cast<std::uint32_t>(number);
}
[[nodiscard]] bool is_null(const CanonicalValue& value) {
    return std::holds_alternative<std::nullptr_t>(value.data);
}
[[nodiscard]] const CanonicalValue& member(const Object& object, std::string_view key) {
    const auto it = object.find(key);
    if (it == object.end()) fail("missing required field: " + std::string(key));
    return it->second;
}
void check_keys(const Object& object, std::initializer_list<std::string_view> expected) {
    if (object.size() != expected.size()) fail("record has missing or unknown fields");
    for (const auto key : expected) if (!object.contains(key)) fail("record has missing or unknown fields");
}
[[nodiscard]] std::optional<std::uint64_t> parse_optional_u64(const CanonicalValue& value) {
    if (is_null(value)) return std::nullopt;
    return as_u64(value);
}
[[nodiscard]] std::optional<std::string> parse_optional_string(const CanonicalValue& value) {
    if (is_null(value)) return std::nullopt;
    return as_string(value);
}

[[nodiscard]] Status parse_status(std::string_view value) {
    constexpr Status all[] = {Status::success, Status::unsupported_input, Status::invalid_schema,
        Status::unknown, Status::inconsistent, Status::zero_normalizer, Status::timeout,
        Status::resource_limit, Status::approximate, Status::non_identified,
        Status::alarm_frozen, Status::certificate_rejected, Status::abstention};
    for (const auto item : all) if (to_string(item) == value) return item;
    fail("unknown record status");
}
[[nodiscard]] Exactness parse_exactness(std::string_view value) {
    for (const auto item : {Exactness::exact, Exactness::approximate, Exactness::not_applicable})
        if (to_string(item) == value) return item;
    fail("unknown exactness designation");
}
[[nodiscard]] DataPartition parse_partition(std::string_view value) {
    for (const auto item : {DataPartition::training, DataPartition::development,
        DataPartition::calibration, DataPartition::final_test, DataPartition::streaming,
        DataPartition::not_applicable}) if (to_string(item) == value) return item;
    fail("unknown data partition label");
}
[[nodiscard]] LineageRelation parse_relation(std::string_view value) {
    for (const auto item : {LineageRelation::derived_from, LineageRelation::observes,
        LineageRelation::cites, LineageRelation::produced_by, LineageRelation::validates})
        if (to_string(item) == value) return item;
    fail("unknown lineage relation");
}
[[nodiscard]] DiagnosticCode parse_diagnostic_code(std::string_view value) {
    constexpr DiagnosticCode all[] = {DiagnosticCode::invalid_record, DiagnosticCode::malformed_input,
        DiagnosticCode::unsupported_input, DiagnosticCode::invalid_schema, DiagnosticCode::unknown,
        DiagnosticCode::inconsistent, DiagnosticCode::zero_normalizer, DiagnosticCode::timeout,
        DiagnosticCode::resource_limit, DiagnosticCode::approximation, DiagnosticCode::non_identified,
        DiagnosticCode::alarm_frozen, DiagnosticCode::certificate_rejected, DiagnosticCode::abstention};
    for (const auto item : all) if (to_string(item) == value) return item;
    fail("unknown diagnostic code");
}
[[nodiscard]] AnyIdentifier parse_any_identifier(std::string_view kind, const std::string& value) {
    if (kind == "observation") return ObservationId{value};
    if (kind == "source") return SourceId{value};
    if (kind == "evidence") return EvidenceId{value};
    if (kind == "fact") return FactId{value};
    if (kind == "entity") return EntityId{value};
    if (kind == "model") return ModelId{value};
    if (kind == "schema") return SchemaId{value};
    if (kind == "code_build") return CodeBuildId{value};
    if (kind == "partition") return PartitionId{value};
    if (kind == "query") return QueryId{value};
    if (kind == "certificate") return CertificateId{value};
    if (kind == "result") return ResultId{value};
    fail("unknown identifier kind in lineage");
}
[[nodiscard]] std::vector<Diagnostic> parse_diagnostics(const CanonicalValue& value) {
    std::vector<Diagnostic> result;
    for (const auto& item : as_array(value)) {
        const auto& object = as_object(item);
        check_keys(object, {"code", "field_path", "message"});
        Diagnostic diagnostic;
        diagnostic.code = parse_diagnostic_code(as_string(member(object, "code")));
        diagnostic.message = as_string(member(object, "message"));
        diagnostic.field_path = parse_optional_string(member(object, "field_path"));
        result.push_back(std::move(diagnostic));
    }
    return result;
}
[[nodiscard]] RecordEnvelope parse_record(const CanonicalValue& value) {
    const auto& root = as_object(value);
    check_keys(root, {"assumptions", "budget", "consumed", "envelope_version", "errors",
        "exactness", "identity", "lineage", "reproducibility", "result", "schema_version",
        "status", "warnings"});
    RecordEnvelope record;
    record.envelope_version = as_u32(member(root, "envelope_version"));
    record.schema_version = as_u32(member(root, "schema_version"));
    record.status = parse_status(as_string(member(root, "status")));
    record.exactness = parse_exactness(as_string(member(root, "exactness")));
    if (!is_null(member(root, "result"))) record.result = member(root, "result");
    for (const auto& assumption : as_array(member(root, "assumptions")))
        record.assumptions.push_back(as_string(assumption));
    record.errors = parse_diagnostics(member(root, "errors"));
    record.warnings = parse_diagnostics(member(root, "warnings"));

    const auto& identity = as_object(member(root, "identity"));
    check_keys(identity, {"certificate_id", "code_build_id", "entity_id", "evidence_id", "fact_id",
        "model_id", "observation_id", "partition", "partition_id", "query_id", "result_id",
        "run_id", "schema_id", "source_id"});
    record.identity.run_id = RunId{as_string(member(identity, "run_id"))};
    record.identity.result_id = ResultId{as_string(member(identity, "result_id"))};
    record.identity.schema_id = SchemaId{as_string(member(identity, "schema_id"))};
    record.identity.code_build_id = CodeBuildId{as_string(member(identity, "code_build_id"))};
    record.identity.partition = parse_partition(as_string(member(identity, "partition")));
    record.identity.observation_id = parse_optional_id<ObservationId>(member(identity, "observation_id"));
    record.identity.source_id = parse_optional_id<SourceId>(member(identity, "source_id"));
    record.identity.evidence_id = parse_optional_id<EvidenceId>(member(identity, "evidence_id"));
    record.identity.fact_id = parse_optional_id<FactId>(member(identity, "fact_id"));
    record.identity.entity_id = parse_optional_id<EntityId>(member(identity, "entity_id"));
    record.identity.model_id = parse_optional_id<ModelId>(member(identity, "model_id"));
    record.identity.partition_id = parse_optional_id<PartitionId>(member(identity, "partition_id"));
    record.identity.query_id = parse_optional_id<QueryId>(member(identity, "query_id"));
    record.identity.certificate_id = parse_optional_id<CertificateId>(member(identity, "certificate_id"));

    for (const auto& item : as_array(member(root, "lineage"))) {
        const auto& object = as_object(item);
        check_keys(object, {"id", "kind", "relation"});
        record.lineage.push_back({parse_any_identifier(as_string(member(object, "kind")),
            as_string(member(object, "id"))),
            parse_relation(as_string(member(object, "relation")))});
    }
    const auto& budget = as_object(member(root, "budget"));
    check_keys(budget, {"cpu_time_ms", "memory_bytes", "operations"});
    record.budget.cpu_time_ms = parse_optional_u64(member(budget, "cpu_time_ms"));
    record.budget.memory_bytes = parse_optional_u64(member(budget, "memory_bytes"));
    record.budget.operations = parse_optional_u64(member(budget, "operations"));
    const auto& consumed = as_object(member(root, "consumed"));
    check_keys(consumed, {"cpu_time_ms", "memory_bytes", "operations"});
    record.consumed.cpu_time_ms = as_u64(member(consumed, "cpu_time_ms"));
    record.consumed.memory_bytes = as_u64(member(consumed, "memory_bytes"));
    record.consumed.operations = as_u64(member(consumed, "operations"));

    const auto& reproduction = as_object(member(root, "reproducibility"));
    check_keys(reproduction, {"algorithm", "artifact_sha256", "format_version", "seed"});
    record.reproducibility.algorithm = as_string(member(reproduction, "algorithm"));
    record.reproducibility.format_version = as_u32(member(reproduction, "format_version"));
    record.reproducibility.seed = parse_optional_u64(member(reproduction, "seed"));
    const auto& hashes = as_object(member(reproduction, "artifact_sha256"));
    for (const auto& [name, hash] : hashes)
        record.reproducibility.artifact_sha256.emplace(name, as_string(hash));
    return record;
}

}  // namespace

template <typename Tag>
Identifier<Tag>::Identifier(std::string value) : value_(std::move(value)) {
    if (!valid_identifier(value_)) throw std::invalid_argument("invalid typed identifier");
}
template <typename Tag>
bool Identifier<Tag>::valid() const noexcept { return valid_identifier(value_); }

// Instantiate each public identifier type in this translation unit.
template class Identifier<RunTag>;
template class Identifier<ObservationTag>;
template class Identifier<SourceTag>;
template class Identifier<EvidenceTag>;
template class Identifier<FactTag>;
template class Identifier<EntityTag>;
template class Identifier<ModelTag>;
template class Identifier<SchemaTag>;
template class Identifier<CodeBuildTag>;
template class Identifier<PartitionTag>;
template class Identifier<QueryTag>;
template class Identifier<CertificateTag>;
template class Identifier<ResultTag>;

CanonicalValue::CanonicalValue(std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        data = static_cast<std::int64_t>(value);
    else data = value;
}

bool valid_identifier(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128) return false;
    const auto alpha_num = [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9');
    };
    if (!alpha_num(static_cast<unsigned char>(value.front()))) return false;
    for (const unsigned char ch : value)
        if (!alpha_num(ch) && ch != '.' && ch != '_' && ch != ':' && ch != '-') return false;
    return true;
}

std::string_view to_string(Status value) noexcept {
    switch (value) {
    case Status::success: return "success";
    case Status::unsupported_input: return "unsupported_input";
    case Status::invalid_schema: return "invalid_schema";
    case Status::unknown: return "unknown";
    case Status::inconsistent: return "inconsistent";
    case Status::zero_normalizer: return "zero_normalizer";
    case Status::timeout: return "timeout";
    case Status::resource_limit: return "resource_limit";
    case Status::approximate: return "approximate";
    case Status::non_identified: return "non_identified";
    case Status::alarm_frozen: return "alarm_frozen";
    case Status::certificate_rejected: return "certificate_rejected";
    case Status::abstention: return "abstention";
    }
    return "invalid_status";
}
std::string_view to_string(Exactness value) noexcept {
    switch (value) {
    case Exactness::exact: return "exact";
    case Exactness::approximate: return "approximate";
    case Exactness::not_applicable: return "not_applicable";
    }
    return "invalid_exactness";
}
std::string_view to_string(DataPartition value) noexcept {
    switch (value) {
    case DataPartition::training: return "training";
    case DataPartition::development: return "development";
    case DataPartition::calibration: return "calibration";
    case DataPartition::final_test: return "final_test";
    case DataPartition::streaming: return "streaming";
    case DataPartition::not_applicable: return "not_applicable";
    }
    return "invalid_partition";
}
std::string_view to_string(LineageRelation value) noexcept {
    switch (value) {
    case LineageRelation::derived_from: return "derived_from";
    case LineageRelation::observes: return "observes";
    case LineageRelation::cites: return "cites";
    case LineageRelation::produced_by: return "produced_by";
    case LineageRelation::validates: return "validates";
    }
    return "invalid_lineage_relation";
}
std::string_view to_string(DiagnosticCode value) noexcept {
    switch (value) {
    case DiagnosticCode::invalid_record: return "invalid_record";
    case DiagnosticCode::malformed_input: return "malformed_input";
    case DiagnosticCode::unsupported_input: return "unsupported_input";
    case DiagnosticCode::invalid_schema: return "invalid_schema";
    case DiagnosticCode::unknown: return "unknown";
    case DiagnosticCode::inconsistent: return "inconsistent";
    case DiagnosticCode::zero_normalizer: return "zero_normalizer";
    case DiagnosticCode::timeout: return "timeout";
    case DiagnosticCode::resource_limit: return "resource_limit";
    case DiagnosticCode::approximation: return "approximation";
    case DiagnosticCode::non_identified: return "non_identified";
    case DiagnosticCode::alarm_frozen: return "alarm_frozen";
    case DiagnosticCode::certificate_rejected: return "certificate_rejected";
    case DiagnosticCode::abstention: return "abstention";
    }
    return "invalid_diagnostic_code";
}

std::vector<std::string> validate_record(const RecordEnvelope& record) {
    std::vector<std::string> issues;
    const auto check_id = [&](const auto& id, std::string_view name) {
        if (!id.valid()) issues.emplace_back(std::string(name) + " is not a valid identifier");
    };
    if (record.envelope_version != 1) issues.emplace_back("unsupported envelope version");
    if (record.schema_version == 0) issues.emplace_back("schema version must be positive");
    if (to_string(record.status) == "invalid_status") issues.emplace_back("status enum is invalid");
    if (to_string(record.exactness) == "invalid_exactness") issues.emplace_back("exactness enum is invalid");
    check_id(record.identity.run_id, "run_id");
    check_id(record.identity.result_id, "result_id");
    check_id(record.identity.schema_id, "schema_id");
    check_id(record.identity.code_build_id, "code_build_id");
    if (to_string(record.identity.partition) == "invalid_partition")
        issues.emplace_back("data partition enum is invalid");
    if (record.identity.partition == DataPartition::not_applicable) {
        if (record.identity.partition_id) issues.emplace_back("not_applicable partition must not carry a partition_id");
    } else if (!record.identity.partition_id || !record.identity.partition_id->valid()) {
        issues.emplace_back("a named data partition requires a valid partition_id");
    }
    if (record.identity.observation_id) check_id(*record.identity.observation_id, "observation_id");
    if (record.identity.source_id) check_id(*record.identity.source_id, "source_id");
    if (record.identity.evidence_id) check_id(*record.identity.evidence_id, "evidence_id");
    if (record.identity.fact_id) check_id(*record.identity.fact_id, "fact_id");
    if (record.identity.entity_id) check_id(*record.identity.entity_id, "entity_id");
    if (record.identity.model_id) check_id(*record.identity.model_id, "model_id");
    if (record.identity.partition_id) check_id(*record.identity.partition_id, "partition_id");
    if (record.identity.query_id) check_id(*record.identity.query_id, "query_id");
    if (record.identity.certificate_id) check_id(*record.identity.certificate_id, "certificate_id");
    if (!valid_utf8(record.reproducibility.algorithm) || record.reproducibility.algorithm.empty())
        issues.emplace_back("reproducibility algorithm must be a non-empty UTF-8 string");
    if (record.reproducibility.format_version != 1)
        issues.emplace_back("unsupported reproducibility metadata format version");
    for (const auto& [name, hash] : record.reproducibility.artifact_sha256) {
        if (name.empty() || !valid_utf8(name)) issues.emplace_back("artifact hash name is empty or invalid UTF-8");
        if (hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            issues.emplace_back("artifact hash must be 64 lowercase hexadecimal SHA-256 characters");
    }
    const auto check_budget = [&](const std::optional<std::uint64_t>& maximum,
                                  std::uint64_t used, std::string_view name) {
        if (maximum && used > *maximum)
            issues.emplace_back(std::string(name) + " consumption exceeds its declared budget");
    };
    check_budget(record.budget.cpu_time_ms, record.consumed.cpu_time_ms, "CPU-time");
    check_budget(record.budget.memory_bytes, record.consumed.memory_bytes, "memory");
    check_budget(record.budget.operations, record.consumed.operations, "operation");
    for (const auto& assumption : record.assumptions)
        if (assumption.empty() || !valid_utf8(assumption)) issues.emplace_back("assumptions must be non-empty valid UTF-8 strings");
    for (const auto& entry : record.lineage) {
        const bool id_valid = std::visit([](const auto& id) { return id.valid(); }, entry.identifier);
        if (!id_valid) issues.emplace_back("lineage contains an invalid typed identifier");
        if (to_string(entry.relation) == "invalid_lineage_relation")
            issues.emplace_back("lineage relation enum is invalid");
    }
    const auto check_diagnostics = [&](const std::vector<Diagnostic>& values, std::string_view name) {
        for (const auto& diagnostic : values) {
            if (to_string(diagnostic.code) == "invalid_diagnostic_code")
                issues.emplace_back(std::string(name) + " diagnostic code enum is invalid");
            if (diagnostic.message.empty() || !valid_utf8(diagnostic.message))
                issues.emplace_back(std::string(name) + " diagnostic message must be non-empty valid UTF-8");
            if (diagnostic.field_path && (diagnostic.field_path->empty() || !valid_utf8(*diagnostic.field_path)))
                issues.emplace_back(std::string(name) + " diagnostic field path is invalid");
        }
    };
    check_diagnostics(record.warnings, "warning");
    check_diagnostics(record.errors, "error");
    if (record.status == Status::success) {
        if (!record.result) issues.emplace_back("success status requires a structured result");
        if (!record.errors.empty()) issues.emplace_back("success status cannot carry error diagnostics");
        if (record.exactness == Exactness::approximate) issues.emplace_back("success cannot be labeled approximate; use approximate status");
    } else if (record.status == Status::approximate) {
        if (!record.result) issues.emplace_back("approximate status requires a result");
        if (record.exactness != Exactness::approximate) issues.emplace_back("approximate status requires approximate exactness");
        if (record.warnings.empty()) issues.emplace_back("approximate result requires a warning describing the limitation");
        if (!record.errors.empty()) issues.emplace_back("approximate result cannot carry error diagnostics");
    } else {
        if (record.result) issues.emplace_back("failure, unknown, and abstention statuses cannot carry a result value");
        if (record.exactness != Exactness::not_applicable) issues.emplace_back("non-result status must use not_applicable exactness");
        if (record.errors.empty()) issues.emplace_back("non-success status requires an explicit error diagnostic");
    }
    return issues;
}

std::string encode_record(const RecordEnvelope& record) {
    const auto issues = validate_record(record);
    if (!issues.empty()) throw std::invalid_argument("invalid record: " + issues.front());
    const auto output = serialize_value(record_value(record));
    if (output.size() > kMaximumRecordBytes) throw std::invalid_argument("record exceeds the 16 MiB size limit");
    return output;
}

DecodeResult decode_record(std::string_view json,
                           const std::optional<SchemaExpectation>& expected) {
    try {
        const auto value = JsonParser{json}.parse();
        if (serialize_value(value) != json) fail("record is valid JSON but not canonical JSON");
        auto record = parse_record(value);
        const auto issues = validate_record(record);
        if (!issues.empty()) fail("invalid record: " + issues.front());
        if (expected && (record.identity.schema_id != expected->schema_id ||
                         record.schema_version != expected->schema_version))
            return DecodeFailure{Status::invalid_schema, "record schema does not match the required schema and version"};
        return record;
    } catch (const std::exception& error) {
        return DecodeFailure{Status::invalid_schema, error.what()};
    }
}

}  // namespace xai::contracts
