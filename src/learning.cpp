#include "xai/learning.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace xai::learning {
namespace {
using Value = contracts::CanonicalValue;
using Object = Value::Object;
using Array = Value::Array;
using Counts = SequentialPredictor::Counts;
constexpr std::uint64_t kHardMaxCount = 10'000;
constexpr std::array<std::string_view, 3> kExpertNames{
    "global-beta-bernoulli-v1", "structure-context-beta-v1", "previous-outcome-beta-v1"};
constexpr std::array<std::string_view, 3> kTransitionNames{"start", "not_completed", "completed"};
constexpr std::string_view kPriorSpec = "three equal model prior masses 1/3; each Bernoulli expert Beta(1,1)";
constexpr std::string_view kLibrarySpec =
    "xai-wmc-completion-library-v1; global Bernoulli; log2 structural-count context Bernoulli; "
    "first-order previous-outcome Bernoulli; features variables,clauses,literal_occurrences; "
    "target exact completion versus explicit noncompletion; prior Beta(1,1) for each expert";

[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument(message); }

[[nodiscard]] std::string sha256(std::string_view input) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while (bytes.size() % 64U != 56U) bytes.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>((bit_length >> static_cast<unsigned>(shift)) & 0xffU));
    std::array<std::uint32_t, 8> hash{
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto base = offset + i * 4U;
            words[i] = (static_cast<std::uint32_t>(bytes[base]) << 24U) |
                       (static_cast<std::uint32_t>(bytes[base + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(bytes[base + 2U]) << 8U) |
                       static_cast<std::uint32_t>(bytes[base + 3U]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = std::rotr(words[i - 15U], 7) ^ std::rotr(words[i - 15U], 18) ^ (words[i - 15U] >> 3U);
            const auto s1 = std::rotr(words[i - 2U], 17) ^ std::rotr(words[i - 2U], 19) ^ (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }
        auto a = hash[0]; auto b = hash[1]; auto c = hash[2]; auto d = hash[3];
        auto e = hash[4]; auto f = hash[5]; auto g = hash[6]; auto h = hash[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const auto upper1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto first = h + upper1 + choice + constants[i] + words[i];
            const auto upper0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto second = upper0 + majority;
            h = g; g = f; f = e; e = d + first;
            d = c; c = b; b = a; a = first + second;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto word : hash) output << std::setw(8) << word;
    return output.str();
}

[[nodiscard]] const std::string& library_hash_value() {
    static const auto value = sha256(kLibrarySpec);
    return value;
}
[[nodiscard]] const std::string& prior_hash_value() {
    static const auto value = sha256(kPriorSpec);
    return value;
}

[[nodiscard]] Value u64(std::uint64_t value) { return Value{value}; }
[[nodiscard]] Value text(std::string value) { return Value{std::move(value)}; }
[[nodiscard]] Value bool_or_null(const std::optional<bool>& value) {
    return value ? Value{*value} : Value{nullptr};
}
[[nodiscard]] std::string real_text(long double value) {
    if (!std::isfinite(value)) fail("non-finite predictive value");
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::scientific << std::setprecision(std::numeric_limits<long double>::max_digits10) << value;
    return output.str();
}
[[nodiscard]] long double parse_real(const Value& value) {
    const auto* string = std::get_if<std::string>(&value.data);
    if (!string || string->empty()) fail("expected finite canonical real string");
    std::istringstream input(*string);
    input.imbue(std::locale::classic());
    long double result = 0;
    input >> result;
    char trailing = 0;
    if (!input || (input >> trailing) || !std::isfinite(result) || real_text(result) != *string)
        fail("invalid or noncanonical finite real string");
    return result;
}
[[nodiscard]] std::string numeric_runtime_id() {
    std::string result = "long-double-radix:" + std::to_string(std::numeric_limits<long double>::radix) +
        ";digits:" + std::to_string(std::numeric_limits<long double>::digits) +
        ";max-digits10:" + std::to_string(std::numeric_limits<long double>::max_digits10) +
        ";min-exponent:" + std::to_string(std::numeric_limits<long double>::min_exponent) +
        ";max-exponent:" + std::to_string(std::numeric_limits<long double>::max_exponent);
#if defined(__clang__)
    result += ";clang:" + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    result += ";gcc:" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
    result += ";msvc:" + std::to_string(_MSC_VER);
#endif
#if defined(__GLIBCXX__)
    result += ";libstdc++:" + std::to_string(__GLIBCXX__);
#endif
    return result;
}
[[nodiscard]] const Object& as_object(const Value& value) {
    const auto* result = std::get_if<Object>(&value.data);
    if (!result) fail("expected JSON object");
    return *result;
}
[[nodiscard]] const Array& as_array(const Value& value) {
    const auto* result = std::get_if<Array>(&value.data);
    if (!result) fail("expected JSON array");
    return *result;
}
[[nodiscard]] const std::string& as_string(const Value& value) {
    const auto* result = std::get_if<std::string>(&value.data);
    if (!result) fail("expected JSON string");
    return *result;
}
[[nodiscard]] std::uint64_t as_u64(const Value& value) {
    if (const auto* result = std::get_if<std::uint64_t>(&value.data)) return *result;
    if (const auto* result = std::get_if<std::int64_t>(&value.data); result && *result >= 0)
        return static_cast<std::uint64_t>(*result);
    fail("expected non-negative integer");
}
[[nodiscard]] const Value& member(const Object& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end()) fail("missing field: " + std::string(key));
    return found->second;
}
void check_keys(const Object& object, std::initializer_list<std::string_view> keys) {
    if (object.size() != keys.size()) fail("object has missing or unknown fields");
    for (const auto key : keys)
        if (!object.contains(key)) fail("object has missing or unknown fields");
}
[[nodiscard]] bool is_null(const Value& value) {
    return std::holds_alternative<std::nullptr_t>(value.data);
}
[[nodiscard]] Value counts_value(const Counts& counts) {
    return Value{Object{{"negative", u64(counts.negative)}, {"positive", u64(counts.positive)}}};
}
[[nodiscard]] Counts parse_counts(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"negative", "positive"});
    return {as_u64(member(object, "positive")), as_u64(member(object, "negative"))};
}
[[nodiscard]] std::uint64_t checked_total(const Counts& counts, std::uint64_t maximum) {
    if (counts.positive > maximum || counts.negative > maximum - counts.positive)
        fail("snapshot sufficient-statistic counter exceeds its configured maximum");
    return counts.positive + counts.negative;
}
[[nodiscard]] bool valid_partition(contracts::DataPartition value) {
    switch (value) {
        case contracts::DataPartition::training:
        case contracts::DataPartition::development:
        case contracts::DataPartition::calibration:
        case contracts::DataPartition::final_test:
        case contracts::DataPartition::streaming:
        case contracts::DataPartition::not_applicable: return true;
    }
    return false;
}
[[nodiscard]] bool named_partition(contracts::DataPartition value) {
    return value != contracts::DataPartition::not_applicable && valid_partition(value);
}
[[nodiscard]] bool valid_metadata(const PredictorDefinition& definition, const PredictionInput& input) {
    return definition.run_id.valid() && definition.library_id.valid() && definition.result_schema_id.valid() &&
           definition.state_schema_id.valid() && definition.code_build_id.valid() &&
           input.observation_id.valid() && input.result_id.valid() && input.partition_id.valid() &&
           valid_partition(input.partition);
}
[[nodiscard]] std::string feature_context(const PredictionInput& input) {
    if (!input.variables || !input.clauses || !input.literal_occurrences) return {};
    return "v" + std::to_string(std::bit_width(*input.variables)) + ":c" +
           std::to_string(std::bit_width(*input.clauses)) + ":l" +
           std::to_string(std::bit_width(*input.literal_occurrences));
}
[[nodiscard]] bool valid_context_key(std::string_view key) {
    const std::array<char, 3> markers{'v', 'c', 'l'};
    const std::array<unsigned, 3> maximum_bits{20U, 24U, 27U};
    std::size_t position = 0;
    for (std::size_t i = 0; i < markers.size(); ++i) {
        if (position >= key.size() || key[position++] != markers[i]) return false;
        const auto end = key.find(i + 1U == markers.size() ? '\0' : ':', position);
        const auto stop = end == std::string_view::npos ? key.size() : end;
        unsigned value = 0;
        const auto parsed = std::from_chars(key.data() + position, key.data() + stop, value);
        if (position == stop || parsed.ec != std::errc{} || parsed.ptr != key.data() + stop || value > maximum_bits[i])
            return false;
        position = stop + (end == std::string_view::npos ? 0U : 1U);
    }
    return position == key.size();
}
[[nodiscard]] std::string feature_error(const PredictionInput& input) {
    if (input.feature_schema_id != contracts::SchemaId{"xai.wmc_structure"} ||
        input.feature_schema_version != 1)
        return "feature schema ID or version is unsupported";
    if (!input.phase0_eligible)
        return "Phase 0 parser and exact-weight eligibility must be confirmed before prediction or update";
    if (!input.variables || !input.clauses || !input.literal_occurrences)
        return "all parser-derived structural features are required; missing data are not imputed";
    if (*input.variables > 1'000'000 || *input.clauses > 10'000'000 ||
        *input.literal_occurrences > 100'000'000)
        return "structural feature exceeds the Phase 0 WMC parser cap";
    return {};
}
[[nodiscard]] std::array<long double, 3> log_weights(const std::array<long double, 3>& scores) {
    std::array<long double, 3> result{};
    constexpr long double prior = -1.098612288668109691395245236922525704647L;
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = prior + scores[i];
    const auto maximum = *std::max_element(result.begin(), result.end());
    long double sum = 0;
    for (const auto item : result) sum += std::exp(item - maximum);
    const auto normalizer = maximum + std::log(sum);
    for (auto& item : result) item -= normalizer;
    return result;
}
[[nodiscard]] std::array<long double, 3> weights(const std::array<long double, 3>& scores) {
    auto result = log_weights(scores);
    for (auto& item : result) item = std::exp(item);
    return result;
}
[[nodiscard]] long double log_sum_exp(const std::array<long double, 3>& terms) {
    const auto maximum = *std::max_element(terms.begin(), terms.end());
    long double sum = 0;
    for (const auto item : terms) sum += std::exp(item - maximum);
    return maximum + std::log(sum);
}
[[nodiscard]] std::pair<std::array<long double, 3>, std::array<long double, 3>> expert_log_probabilities(
    const Counts& global, const Counts& context, const Counts& transition) {
    const auto log_positive = [](const Counts& counts) {
        const auto numerator = static_cast<long double>(counts.positive) + 1.0L;
        const auto denominator = static_cast<long double>(counts.positive) +
                                 static_cast<long double>(counts.negative) + 2.0L;
        return std::log(numerator) - std::log(denominator);
    };
    const auto log_negative = [](const Counts& counts) {
        const auto numerator = static_cast<long double>(counts.negative) + 1.0L;
        const auto denominator = static_cast<long double>(counts.positive) +
                                 static_cast<long double>(counts.negative) + 2.0L;
        return std::log(numerator) - std::log(denominator);
    };
    return {{log_positive(global), log_positive(context), log_positive(transition)},
            {log_negative(global), log_negative(context), log_negative(transition)}};
}
[[nodiscard]] contracts::DiagnosticCode diagnostic_code(contracts::Status status) {
    switch (status) {
        case contracts::Status::unsupported_input: return contracts::DiagnosticCode::unsupported_input;
        case contracts::Status::invalid_schema: return contracts::DiagnosticCode::invalid_schema;
        case contracts::Status::unknown: return contracts::DiagnosticCode::unknown;
        case contracts::Status::resource_limit: return contracts::DiagnosticCode::resource_limit;
        case contracts::Status::timeout: return contracts::DiagnosticCode::timeout;
        case contracts::Status::approximate: return contracts::DiagnosticCode::approximation;
        default: return contracts::DiagnosticCode::invalid_record;
    }
}
[[nodiscard]] contracts::RecordEnvelope record_identity(const PredictorDefinition& definition,
                                                         const PredictionInput& input) {
    contracts::RecordEnvelope record;
    record.identity.run_id = definition.run_id;
    record.identity.result_id = input.result_id.valid() ? input.result_id : contracts::ResultId{"result:invalid"};
    record.identity.schema_id = definition.result_schema_id;
    record.identity.code_build_id = definition.code_build_id;
    record.identity.model_id = definition.library_id;
    if (input.observation_id.valid()) record.identity.observation_id = input.observation_id;
    if (named_partition(input.partition) && input.partition_id.valid()) {
        record.identity.partition = input.partition;
        record.identity.partition_id = input.partition_id;
    } else {
        record.identity.partition = contracts::DataPartition::not_applicable;
    }
    return record;
}

[[nodiscard]] Value feature_value(const PredictionInput& input) {
    return Value{Object{
        {"clauses", input.clauses ? u64(*input.clauses) : Value{nullptr}},
        {"literal_occurrences", input.literal_occurrences ? u64(*input.literal_occurrences) : Value{nullptr}},
        {"variables", input.variables ? u64(*input.variables) : Value{nullptr}}}};
}

[[nodiscard]] contracts::RecordEnvelope failure_record(const PredictorDefinition& definition,
                                                        const PredictionInput& input,
                                                        contracts::Status status,
                                                        const std::string& message) {
    auto record = record_identity(definition, input);
    record.status = status;
    record.errors.push_back({diagnostic_code(status), message, std::nullopt});
    record.exactness = contracts::Exactness::not_applicable;
    record.reproducibility.algorithm = "xai-sequential-log-mixture-beta-bernoulli-v1";
    record.reproducibility.artifact_sha256 = {{"model_library", library_hash_value()},
                                               {"model_prior", prior_hash_value()}};
    return record;
}

[[nodiscard]] Value audit_value(const TrainingUseDecision& decision) {
    return Value{Object{
        {"accepted", Value{decision.accepted}},
        {"observation_id", text(decision.observation_id.value())},
        {"partition", text(std::string(contracts::to_string(decision.partition)))},
        {"partition_id", text(decision.partition_id.value())},
        {"reason", text(decision.reason)},
        {"status", text(std::string(contracts::to_string(decision.status)))}}};
}

[[nodiscard]] contracts::Status parse_status(std::string_view value) {
    if (value == "success") return contracts::Status::success;
    if (value == "unsupported_input") return contracts::Status::unsupported_input;
    if (value == "invalid_schema") return contracts::Status::invalid_schema;
    if (value == "resource_limit") return contracts::Status::resource_limit;
    if (value == "unknown") return contracts::Status::unknown;
    fail("unsupported status in training-use audit");
}
[[nodiscard]] contracts::DataPartition parse_partition(std::string_view value) {
    if (value == "training") return contracts::DataPartition::training;
    if (value == "development") return contracts::DataPartition::development;
    if (value == "calibration") return contracts::DataPartition::calibration;
    if (value == "final_test") return contracts::DataPartition::final_test;
    if (value == "streaming") return contracts::DataPartition::streaming;
    if (value == "not_applicable") return contracts::DataPartition::not_applicable;
    fail("invalid partition in training-use audit");
}

[[nodiscard]] Value result_value(const PredictionInput& input, const PredictionSummary& prediction,
                                 const Counts& global, const Counts& context,
                                 const std::array<Counts, 3>& transitions,
                                 std::size_t transition_index,
                                 const std::array<long double, 3>& expert_scores,
                                 const std::optional<CompletionOutcome>& outcome,
                                 bool updated, std::string_view library_hash,
                                 std::string_view prior_hash) {
    const auto model_weights = prediction.model_weights_after;
    Array models;
    models.reserve(3);
    for (std::size_t i = 0; i < 3; ++i) {
        const Counts& sufficient = i == 0 ? global : (i == 1 ? context : transitions[transition_index]);
        models.emplace_back(Object{
            {"cumulative_model_log_score", text(real_text(expert_scores[i]))},
            {"id", text(std::string(kExpertNames[i]))},
            {"posterior_weight_after", text(real_text(model_weights[i]))},
            {"posterior_weight_before", text(real_text(prediction.model_weights_before[i]))},
            {"probability_completed_before", text(real_text(prediction.expert_completion_probability[i]))},
            {"sufficient_statistics", counts_value(sufficient)}});
    }
    Array transition_values;
    for (std::size_t i = 0; i < transitions.size(); ++i)
        transition_values.emplace_back(Object{{"context", text(std::string(kTransitionNames[i]))},
                                              {"counts", counts_value(transitions[i])}});
    const auto target = outcome ? text(std::string(to_string(*outcome))) : Value{nullptr};
    const auto observed_log = prediction.observed_log_probability
        ? text(real_text(*prediction.observed_log_probability)) : Value{nullptr};
    return Value{Object{
        {"accepted_examples", u64(prediction.accepted_examples)},
        {"cumulative_mixture_log_score", text(real_text(prediction.cumulative_mixture_log_score))},
        {"feature_schema_id", text(input.feature_schema_id.value())},
        {"feature_schema_version", u64(input.feature_schema_version)},
        {"features", feature_value(input)},
        {"log_probability_observed", observed_log},
        {"model_library_sha256", text(std::string(library_hash))},
        {"model_prior_sha256", text(std::string(prior_hash))},
        {"models", Value{std::move(models)}},
        {"mixture_probability_completed", text(real_text(prediction.completion_probability))},
        {"observation_id", text(input.observation_id.value())},
        {"phase0_eligible", Value{input.phase0_eligible}},
        {"partition", text(std::string(contracts::to_string(input.partition)))},
        {"partition_id", text(input.partition_id.value())},
        {"prediction_stage", text(updated ? "prequential_update" : "read_only_prediction")},
        {"target", target},
        {"training_use_decision", text(updated ? "accepted" : "prediction_only")},
        {"transition_sufficient_statistics", Value{std::move(transition_values)}}}};
}

[[nodiscard]] contracts::RecordEnvelope success_record(const PredictorDefinition& definition,
                                                       const PredictionInput& input,
                                                       const PredictionSummary& prediction,
                                                       const Counts& global, const Counts& context,
                                                       const std::array<Counts, 3>& transitions,
                                                       std::size_t transition_index,
                                                       const std::array<long double, 3>& expert_scores,
                                                       const std::optional<CompletionOutcome>& outcome,
                                                       bool updated) {
    auto record = record_identity(definition, input);
    record.status = contracts::Status::approximate;
    record.result = result_value(input, prediction, global, context, transitions, transition_index, expert_scores,
                                 outcome, updated, library_hash_value(), prior_hash_value());
    record.assumptions = {
        "the target is exact WMC completion versus explicit solver noncompletion on eligible, parser-valid instances",
        "the three experts and equal prior masses are fixed by model-library version xai-wmc-completion-v1",
        "all predictive comparisons are relative to this declared library and prior, not an accuracy guarantee",
        "floating-point probabilities and log scores use the declared sequential log-mixture algorithm"};
    record.lineage.push_back({contracts::AnyIdentifier{input.observation_id}, contracts::LineageRelation::observes});
    record.lineage.push_back({contracts::AnyIdentifier{definition.library_id}, contracts::LineageRelation::produced_by});
    record.exactness = contracts::Exactness::approximate;
    record.warnings.push_back({contracts::DiagnosticCode::approximation,
        "log-mixture probabilities and scores use long-double arithmetic; compare within declared tolerance", std::nullopt});
    record.budget.operations = definition.limits.max_examples;
    record.consumed.operations = prediction.accepted_examples;
    record.reproducibility.algorithm = "xai-sequential-log-mixture-beta-bernoulli-v1";
    record.reproducibility.artifact_sha256 = {{"model_library", library_hash_value()},
                                               {"model_prior", prior_hash_value()}};
    return record;
}

}  // namespace

std::string_view to_string(CompletionOutcome value) noexcept {
    switch (value) {
        case CompletionOutcome::completed_exact: return "completed_exact";
        case CompletionOutcome::explicit_noncompletion: return "explicit_noncompletion";
        case CompletionOutcome::non_result: return "non_result";
    }
    return "unknown";
}

std::string_view model_library_sha256() noexcept { return library_hash_value(); }
std::string_view model_prior_sha256() noexcept { return prior_hash_value(); }

SequentialPredictor::SequentialPredictor(PredictorDefinition definition)
    : definition_(std::move(definition)), library_sha256_(library_hash_value()), prior_sha256_(prior_hash_value()) {
    if (!definition_.run_id.valid() || !definition_.library_id.valid() ||
        !definition_.result_schema_id.valid() || !definition_.state_schema_id.valid() ||
        !definition_.code_build_id.valid() ||
        definition_.library_id != contracts::ModelId{"library:wmc-completion-v1"})
        fail("predictor identifiers are invalid");
    if (definition_.limits.max_examples == 0 || definition_.limits.max_audit_entries == 0 ||
        definition_.limits.max_contexts == 0 || definition_.limits.max_examples > kHardMaxCount ||
        definition_.limits.max_audit_entries > kHardMaxCount || definition_.limits.max_contexts > kHardMaxCount)
        fail("learning limits must be positive and no greater than the hard ten-thousand-entry ceiling");
}

LearningResult SequentialPredictor::failure(const PredictionInput& input, contracts::Status status,
                                           std::string message) const {
    LearningResult result;
    result.status = status;
    result.diagnostic = std::move(message);
    result.record = failure_record(definition_, input, status, result.diagnostic);
    return result;
}

void SequentialPredictor::append_audit(const PredictionInput& input, contracts::Status status,
                                       bool accepted, std::string reason) {
    training_use_audit_.push_back(TrainingUseDecision{input.observation_id, input.partition_id,
        input.partition, status, accepted, std::move(reason)});
}

LearningResult SequentialPredictor::predict(const PredictionInput& input) const {
    if (!valid_metadata(definition_, input))
        return failure(input, contracts::Status::invalid_schema,
                       "prediction identifiers or partition metadata are invalid");
    if (!named_partition(input.partition))
        return failure(input, contracts::Status::unsupported_input,
                       "predictions require an explicit named data partition");
    const auto invalid = feature_error(input);
    if (!invalid.empty())
        return failure(input, input.feature_schema_version == 1 &&
            input.feature_schema_id == contracts::SchemaId{"xai.wmc_structure"}
                ? contracts::Status::unsupported_input : contracts::Status::invalid_schema, invalid);
    const auto bucket = feature_context(input);
    const auto found = context_counts_.find(bucket);
    const Counts empty{};
    const Counts& context = found == context_counts_.end() ? empty : found->second;
    const auto transition_index = !previous_outcome_ ? 0U : (*previous_outcome_ ? 2U : 1U);
    const auto [log_positive, log_negative] = expert_log_probabilities(global_counts_, context,
                                                                        transition_counts_[transition_index]);
    const auto posterior_logs = log_weights(expert_log_scores_);
    std::array<long double, 3> positive_terms{};
    for (std::size_t i = 0; i < positive_terms.size(); ++i) positive_terms[i] = posterior_logs[i] + log_positive[i];
    PredictionSummary prediction;
    prediction.completion_probability = std::exp(log_sum_exp(positive_terms));
    prediction.expert_completion_probability = {std::exp(log_positive[0]), std::exp(log_positive[1]),
                                                std::exp(log_positive[2])};
    prediction.model_weights_before = weights(expert_log_scores_);
    prediction.model_weights_after = prediction.model_weights_before;
    prediction.cumulative_mixture_log_score = cumulative_mixture_log_score_;
    prediction.accepted_examples = accepted_examples_;
    LearningResult result;
    result.status = contracts::Status::approximate;
    result.prediction = prediction;
    result.record = success_record(definition_, input, prediction, global_counts_, context,
        transition_counts_, transition_index, expert_log_scores_, std::nullopt, false);
    return result;
}

LearningResult SequentialPredictor::observe(const PredictionInput& input, CompletionOutcome outcome) {
    const auto metadata_ok = valid_metadata(definition_, input);
    const auto reject = [&](contracts::Status status, const std::string& reason, bool audit) {
        if (audit && metadata_ok && training_use_audit_.size() < definition_.limits.max_audit_entries)
            append_audit(input, status, false, reason);
        return failure(input, status, reason);
    };
    if (!metadata_ok)
        return reject(contracts::Status::invalid_schema, "observation identifiers or partition metadata are invalid", false);
    if (training_use_audit_.size() >= definition_.limits.max_audit_entries)
        return failure(input, contracts::Status::resource_limit, "training-use audit entry limit reached");
    if (input.partition != contracts::DataPartition::training && input.partition != contracts::DataPartition::streaming)
        return reject(contracts::Status::unsupported_input,
                      "updates are authorized only for training or streaming partitions; outcome was not recorded", true);
    if (input.partition == contracts::DataPartition::training && streaming_started_)
        return reject(contracts::Status::invalid_schema,
                      "training cannot resume after the sequential stream has started", true);
    const auto invalid = feature_error(input);
    if (!invalid.empty()) {
        const auto status = input.feature_schema_version == 1 &&
            input.feature_schema_id == contracts::SchemaId{"xai.wmc_structure"}
                ? contracts::Status::unsupported_input : contracts::Status::invalid_schema;
        return reject(status, invalid, true);
    }
    if (outcome == CompletionOutcome::non_result)
        return reject(contracts::Status::unsupported_input,
                      "non-results such as external kills or invalid attempts are not training labels", true);
    if (outcome != CompletionOutcome::completed_exact && outcome != CompletionOutcome::explicit_noncompletion)
        return reject(contracts::Status::invalid_schema, "outcome label is outside the declared binary target", true);
    if (accepted_observation_ids_.contains(input.observation_id.value()))
        return reject(contracts::Status::invalid_schema, "observation ID was already used for an accepted update", true);
    if (accepted_examples_ >= definition_.limits.max_examples)
        return reject(contracts::Status::resource_limit, "accepted-example limit reached", true);
    const auto bucket = feature_context(input);
    if (!context_counts_.contains(bucket) && context_counts_.size() >= definition_.limits.max_contexts)
        return reject(contracts::Status::resource_limit, "structural-context limit reached", true);

    const auto found = context_counts_.find(bucket);
    const Counts empty{};
    const Counts& context_before = found == context_counts_.end() ? empty : found->second;
    const auto transition_index = !previous_outcome_ ? 0U : (*previous_outcome_ ? 2U : 1U);
    const auto [log_positive, log_negative] = expert_log_probabilities(
        global_counts_, context_before, transition_counts_[transition_index]);
    const bool positive = outcome == CompletionOutcome::completed_exact;
    const auto& observed_logs = positive ? log_positive : log_negative;
    const auto prior_log_weights = log_weights(expert_log_scores_);
    const auto prior_weights = weights(expert_log_scores_);
    std::array<long double, 3> positive_terms{};
    std::array<long double, 3> mixture_terms{};
    for (std::size_t i = 0; i < mixture_terms.size(); ++i) {
        positive_terms[i] = prior_log_weights[i] + log_positive[i];
        mixture_terms[i] = prior_log_weights[i] + observed_logs[i];
    }
    const auto log_positive_mixture = log_sum_exp(positive_terms);
    const auto observed_log_probability = log_sum_exp(mixture_terms);
    if (!std::isfinite(observed_log_probability) || observed_log_probability > 1e-15L)
        return reject(contracts::Status::unknown, "mixture probability could not be represented safely", true);

    for (std::size_t i = 0; i < expert_log_scores_.size(); ++i)
        expert_log_scores_[i] += observed_logs[i];
    cumulative_mixture_log_score_ += std::min(observed_log_probability, 0.0L);
    if (positive) {
        ++global_counts_.positive;
        ++context_counts_[bucket].positive;
        ++transition_counts_[transition_index].positive;
    } else {
        ++global_counts_.negative;
        ++context_counts_[bucket].negative;
        ++transition_counts_[transition_index].negative;
    }
    previous_outcome_ = positive;
    accepted_observation_ids_.insert(input.observation_id.value());
    ++accepted_examples_;
    if (input.partition == contracts::DataPartition::streaming) streaming_started_ = true;
    append_audit(input, contracts::Status::success, true,
                 input.partition == contracts::DataPartition::training ? "authorized training update" : "authorized streaming update");

    const auto post_weights = weights(expert_log_scores_);
    PredictionSummary prediction;
    prediction.completion_probability = std::exp(log_positive_mixture);
    prediction.expert_completion_probability = {std::exp(log_positive[0]), std::exp(log_positive[1]),
                                                std::exp(log_positive[2])};
    prediction.model_weights_before = prior_weights;
    prediction.model_weights_after = post_weights;
    prediction.observed_log_probability = observed_log_probability;
    prediction.cumulative_mixture_log_score = cumulative_mixture_log_score_;
    prediction.accepted_examples = accepted_examples_;
    LearningResult result;
    result.status = contracts::Status::approximate;
    result.prediction = prediction;
    result.state_updated = true;
    result.record = success_record(definition_, input, prediction, global_counts_, context_counts_.at(bucket),
        transition_counts_, transition_index, expert_log_scores_, outcome, true);
    return result;
}

std::string SequentialPredictor::serialize() const {
    Array accepted_ids;
    for (const auto& id : accepted_observation_ids_) accepted_ids.emplace_back(id);
    Array audit;
    audit.reserve(training_use_audit_.size());
    for (const auto& item : training_use_audit_) audit.push_back(audit_value(item));
    Array contexts;
    contexts.reserve(context_counts_.size());
    for (const auto& [key, counts] : context_counts_)
        contexts.emplace_back(Object{{"bucket", text(key)}, {"counts", counts_value(counts)}});
    Array transitions;
    for (std::size_t i = 0; i < transition_counts_.size(); ++i)
        transitions.emplace_back(Object{{"context", text(std::string(kTransitionNames[i]))},
                                        {"counts", counts_value(transition_counts_[i])}});
    Array scores;
    for (const auto score : expert_log_scores_) scores.emplace_back(real_text(score));
    Object limits{{"max_audit_entries", u64(definition_.limits.max_audit_entries)},
                  {"max_contexts", u64(definition_.limits.max_contexts)},
                  {"max_examples", u64(definition_.limits.max_examples)}};
    Object result_value_object{
        {"accepted_examples", u64(accepted_examples_)},
        {"accepted_observation_ids", Value{std::move(accepted_ids)}},
        {"audit", Value{std::move(audit)}},
        {"contexts", Value{std::move(contexts)}},
        {"global_counts", counts_value(global_counts_)},
        {"last_outcome", bool_or_null(previous_outcome_)},
        {"limits", Value{std::move(limits)}},
        {"log_scores", Value{std::move(scores)}},
        {"mixture_log_score", text(real_text(cumulative_mixture_log_score_))},
        {"model_library_sha256", text(library_sha256_)},
        {"model_prior_sha256", text(prior_sha256_)},
        {"numeric_runtime_id", text(numeric_runtime_id())},
        {"result_schema_id", text(definition_.result_schema_id.value())},
        {"streaming_started", Value{streaming_started_}},
        {"state_schema_id", text(definition_.state_schema_id.value())},
        {"transitions", Value{std::move(transitions)}}};
    contracts::RecordEnvelope record;
    record.identity.run_id = definition_.run_id;
    record.identity.result_id = contracts::ResultId{"result:predictive-state"};
    record.identity.schema_id = definition_.state_schema_id;
    record.identity.code_build_id = definition_.code_build_id;
    record.identity.model_id = definition_.library_id;
    record.identity.partition = contracts::DataPartition::not_applicable;
    record.status = contracts::Status::approximate;
    record.result = Value{std::move(result_value_object)};
    record.assumptions = {"fixed versioned three-expert Bernoulli library and equal prior masses",
                          "accepted examples contain only training/streaming outcomes; denied attempts are audited without labels"};
    record.exactness = contracts::Exactness::approximate;
    record.warnings.push_back({contracts::DiagnosticCode::approximation,
        "predictive log scores are long-double values serialized as canonical strings", std::nullopt});
    record.budget.operations = definition_.limits.max_examples;
    record.consumed.operations = accepted_examples_;
    record.reproducibility.algorithm = "xai-sequential-log-mixture-beta-bernoulli-v1";
    record.reproducibility.artifact_sha256 = {{"model_library", library_sha256_}, {"model_prior", prior_sha256_}};
    return contracts::encode_record(record);
}

SequentialPredictor SequentialPredictor::restore(std::string_view canonical_record) {
    const auto decoded = contracts::decode_record(canonical_record);
    if (!std::holds_alternative<contracts::RecordEnvelope>(decoded))
        fail("predictor snapshot failed shared-record validation: " + std::get<contracts::DecodeFailure>(decoded).message);
    const auto& record = std::get<contracts::RecordEnvelope>(decoded);
    if (record.status != contracts::Status::approximate || record.exactness != contracts::Exactness::approximate ||
        !record.result || record.identity.partition != contracts::DataPartition::not_applicable ||
        !record.identity.model_id)
        fail("snapshot is not an approximate versioned predictive-state record");
    if (!record.reproducibility.artifact_sha256.contains("model_library") ||
        !record.reproducibility.artifact_sha256.contains("model_prior") ||
        record.reproducibility.artifact_sha256.at("model_library") != library_hash_value() ||
        record.reproducibility.artifact_sha256.at("model_prior") != prior_hash_value())
        fail("snapshot model library or prior hash is incompatible");
    const auto& root = as_object(*record.result);
    check_keys(root, {"accepted_examples", "accepted_observation_ids", "audit", "contexts", "global_counts",
        "last_outcome", "limits", "log_scores", "mixture_log_score", "model_library_sha256",
        "model_prior_sha256", "numeric_runtime_id", "result_schema_id", "state_schema_id",
        "streaming_started", "transitions"});
    if (as_string(member(root, "model_library_sha256")) != library_hash_value() ||
        as_string(member(root, "model_prior_sha256")) != prior_hash_value())
        fail("snapshot result hashes disagree with the fixed model library or prior");
    if (as_string(member(root, "numeric_runtime_id")) != numeric_runtime_id())
        fail("snapshot numeric representation/runtime is incompatible with this build");
    const auto& limit_values = as_object(member(root, "limits"));
    check_keys(limit_values, {"max_audit_entries", "max_contexts", "max_examples"});
    PredictorDefinition definition;
    definition.run_id = record.identity.run_id;
    definition.library_id = *record.identity.model_id;
    definition.state_schema_id = record.identity.schema_id;
    definition.result_schema_id = contracts::SchemaId{as_string(member(root, "result_schema_id"))};
    if (as_string(member(root, "state_schema_id")) != record.identity.schema_id.value())
        fail("snapshot state schema ID disagrees with its record envelope");
    definition.code_build_id = record.identity.code_build_id;
    definition.limits.max_examples = as_u64(member(limit_values, "max_examples"));
    definition.limits.max_audit_entries = as_u64(member(limit_values, "max_audit_entries"));
    definition.limits.max_contexts = as_u64(member(limit_values, "max_contexts"));
    SequentialPredictor predictor{definition};
    predictor.accepted_examples_ = as_u64(member(root, "accepted_examples"));
    predictor.global_counts_ = parse_counts(member(root, "global_counts"));
    predictor.cumulative_mixture_log_score_ = parse_real(member(root, "mixture_log_score"));
    if (predictor.cumulative_mixture_log_score_ > 1e-15L)
        fail("mixture log score must not be positive");
    const auto& last = member(root, "last_outcome");
    if (!is_null(last)) {
        const auto* value = std::get_if<bool>(&last.data);
        if (!value) fail("last outcome must be Boolean or null");
        predictor.previous_outcome_ = *value;
    }
    const auto& score_values = as_array(member(root, "log_scores"));
    if (score_values.size() != 3) fail("snapshot must contain scores for exactly three experts");
    for (std::size_t i = 0; i < 3; ++i) {
        predictor.expert_log_scores_[i] = parse_real(score_values[i]);
        if (predictor.expert_log_scores_[i] > 1e-15L) fail("expert log score must not be positive");
    }
    for (const auto& item : as_array(member(root, "accepted_observation_ids"))) {
        const auto id = as_string(item);
        if (!contracts::valid_identifier(id) || !predictor.accepted_observation_ids_.insert(id).second)
            fail("snapshot contains an invalid or duplicate accepted observation ID");
    }
    for (const auto& item : as_array(member(root, "contexts"))) {
        const auto& context = as_object(item);
        check_keys(context, {"bucket", "counts"});
        const auto bucket = as_string(member(context, "bucket"));
        if (!valid_context_key(bucket) ||
            !predictor.context_counts_.emplace(bucket, parse_counts(member(context, "counts"))).second)
            fail("snapshot contains an invalid or duplicate structural context bucket");
    }
    const auto& transition_values = as_array(member(root, "transitions"));
    if (transition_values.size() != 3) fail("snapshot must contain three transition contexts");
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& item = as_object(transition_values[i]);
        check_keys(item, {"context", "counts"});
        if (as_string(member(item, "context")) != kTransitionNames[i])
            fail("snapshot transition order or context name is incompatible");
        predictor.transition_counts_[i] = parse_counts(member(item, "counts"));
    }
    for (const auto& item : as_array(member(root, "audit"))) {
        const auto& audit = as_object(item);
        check_keys(audit, {"accepted", "observation_id", "partition", "partition_id", "reason", "status"});
        const auto id = as_string(member(audit, "observation_id"));
        const auto partition_id = as_string(member(audit, "partition_id"));
        if (!contracts::valid_identifier(id) || !contracts::valid_identifier(partition_id))
            fail("snapshot training-use audit contains an invalid identifier");
        const auto& accepted_value = member(audit, "accepted");
        const auto* accepted = std::get_if<bool>(&accepted_value.data);
        if (!accepted) fail("audit accepted flag must be Boolean");
        const auto status = parse_status(as_string(member(audit, "status")));
        const auto partition = parse_partition(as_string(member(audit, "partition")));
        const auto reason = as_string(member(audit, "reason"));
        if (*accepted != (status == contracts::Status::success) ||
            (*accepted && partition != contracts::DataPartition::training && partition != contracts::DataPartition::streaming))
            fail("snapshot training-use audit violates the update partition policy");
        predictor.training_use_audit_.push_back({contracts::ObservationId{id}, contracts::PartitionId{partition_id},
            partition, status, *accepted, reason});
    }
    const auto& streaming = member(root, "streaming_started");
    const auto* streaming_flag = std::get_if<bool>(&streaming.data);
    if (!streaming_flag) fail("streaming_started must be Boolean");
    predictor.streaming_started_ = *streaming_flag;

    if (predictor.definition_.limits.max_examples == 0 || predictor.definition_.limits.max_audit_entries == 0 ||
        predictor.definition_.limits.max_contexts == 0 || predictor.accepted_examples_ > predictor.definition_.limits.max_examples ||
        predictor.training_use_audit_.size() > predictor.definition_.limits.max_audit_entries ||
        predictor.context_counts_.size() > predictor.definition_.limits.max_contexts ||
        predictor.accepted_observation_ids_.size() != predictor.accepted_examples_ ||
        checked_total(predictor.global_counts_, predictor.definition_.limits.max_examples) != predictor.accepted_examples_)
        fail("snapshot counts exceed limits or disagree with accepted observations");
    std::uint64_t context_total = 0;
    for (const auto& [key, counts] : predictor.context_counts_) {
        (void)key;
        const auto current = checked_total(counts, predictor.definition_.limits.max_examples);
        if (current > predictor.definition_.limits.max_examples - context_total)
            fail("snapshot structural-context totals exceed the accepted-example limit");
        context_total += current;
    }
    std::uint64_t transition_total = 0;
    for (const auto& counts : predictor.transition_counts_) {
        const auto current = checked_total(counts, predictor.definition_.limits.max_examples);
        if (current > predictor.definition_.limits.max_examples - transition_total)
            fail("snapshot transition totals exceed the accepted-example limit");
        transition_total += current;
    }
    if (context_total != predictor.accepted_examples_ || transition_total != predictor.accepted_examples_ ||
        predictor.previous_outcome_.has_value() != (predictor.accepted_examples_ > 0))
        fail("snapshot sufficient statistics disagree with accepted-example count");
    std::uint64_t accepted_audits = 0;
    bool saw_streaming = false;
    std::set<std::string, std::less<>> audited_accepted_ids;
    for (const auto& item : predictor.training_use_audit_) {
        if (item.accepted) {
            ++accepted_audits;
            if (!predictor.accepted_observation_ids_.contains(item.observation_id.value()))
                fail("accepted audit decision has no matching accepted observation");
            if (!audited_accepted_ids.insert(item.observation_id.value()).second)
                fail("accepted observation ID appears more than once in the training-use audit");
            if (item.partition == contracts::DataPartition::training && saw_streaming)
                fail("accepted training update appears after the stream began");
            if (item.partition == contracts::DataPartition::streaming) saw_streaming = true;
        }
    }
    if (accepted_audits != predictor.accepted_examples_ ||
        audited_accepted_ids != predictor.accepted_observation_ids_ || saw_streaming != predictor.streaming_started_)
        fail("snapshot training-use audit disagrees with accepted state");
    if (predictor.serialize() != canonical_record)
        fail("snapshot is not a canonical replay of its typed predictor state");
    return predictor;
}

}  // namespace xai::learning
