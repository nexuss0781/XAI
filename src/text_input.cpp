#include "xai/text_input.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xai::interaction {
namespace {
using Object = contracts::CanonicalValue::Object;
using Value = contracts::CanonicalValue;

struct ValidationIssue {
    contracts::Status status;
    contracts::DiagnosticCode code;
    const char* message;
};

[[nodiscard]] bool is_control(char32_t code_point) noexcept {
    return code_point <= 0x1fU || (code_point >= 0x7fU && code_point <= 0x9fU);
}

[[nodiscard]] bool valid_utf8_text(std::string_view text) noexcept {
    std::size_t position = 0;
    while (position < text.size()) {
        const auto lead = static_cast<unsigned char>(text[position]);
        char32_t code_point = 0;
        std::size_t length = 0;
        char32_t minimum = 0;
        if (lead <= 0x7fU) {
            code_point = lead;
            length = 1;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            code_point = static_cast<char32_t>(lead & 0x1fU);
            length = 2;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            code_point = static_cast<char32_t>(lead & 0x0fU);
            length = 3;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            code_point = static_cast<char32_t>(lead & 0x07U);
            length = 4;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (length > text.size() - position) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[position + offset]);
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = static_cast<char32_t>((code_point << 6U) | (byte & 0x3fU));
        }
        if (length > 1 && code_point < minimum) return false;
        if (code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU))
            return false;
        position += length;
    }
    return true;
}

[[nodiscard]] bool valid_metadata_text(std::string_view text, std::size_t maximum) noexcept {
    if (text.empty() || text.size() > maximum) return false;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto lead = static_cast<unsigned char>(text[position]);
        char32_t code_point = 0;
        std::size_t length = 0;
        char32_t minimum = 0;
        if (lead <= 0x7fU) {
            code_point = lead;
            length = 1;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            code_point = static_cast<char32_t>(lead & 0x1fU);
            length = 2;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            code_point = static_cast<char32_t>(lead & 0x0fU);
            length = 3;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            code_point = static_cast<char32_t>(lead & 0x07U);
            length = 4;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (length > text.size() - position) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[position + offset]);
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = static_cast<char32_t>((code_point << 6U) | (byte & 0x3fU));
        }
        if ((length > 1 && code_point < minimum) || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU) || is_control(code_point))
            return false;
        position += length;
    }
    return true;
}

[[nodiscard]] bool valid_locale(std::string_view locale) noexcept {
    if (locale.empty() || locale.size() > 63) return false;
    std::size_t subtag_length = 0;
    std::size_t subtag_index = 0;
    for (const unsigned char ch : locale) {
        if (ch == '-') {
            if (subtag_length == 0 || subtag_length > 8) return false;
            ++subtag_index;
            subtag_length = 0;
            continue;
        }
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if ((!alpha && !digit) || (subtag_index == 0 && !alpha)) return false;
        ++subtag_length;
        if (subtag_length > 8) return false;
    }
    return subtag_length > 0;
}

[[nodiscard]] bool parse_digits(std::string_view value, unsigned& result) noexcept {
    if (value.empty()) return false;
    result = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
        result = result * 10U + static_cast<unsigned>(ch - '0');
    }
    return true;
}

[[nodiscard]] bool valid_utc_timestamp(std::string_view value) noexcept {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') return false;
    unsigned year_value = 0;
    unsigned month_value = 0;
    unsigned day_value = 0;
    unsigned hour_value = 0;
    unsigned minute_value = 0;
    unsigned second_value = 0;
    if (!parse_digits(value.substr(0, 4), year_value) ||
        !parse_digits(value.substr(5, 2), month_value) ||
        !parse_digits(value.substr(8, 2), day_value) ||
        !parse_digits(value.substr(11, 2), hour_value) ||
        !parse_digits(value.substr(14, 2), minute_value) ||
        !parse_digits(value.substr(17, 2), second_value)) return false;
    const std::chrono::year_month_day date{
        std::chrono::year{static_cast<int>(year_value)},
        std::chrono::month{month_value}, std::chrono::day{day_value}};
    return year_value >= 1 && date.ok() && hour_value <= 23 && minute_value <= 59 &&
        second_value <= 60;
}

[[nodiscard]] std::optional<ValidationIssue> validate_metadata(const TextInputMetadata& metadata) {
    if (!metadata.run_id.valid() || !metadata.result_id.valid() ||
        !metadata.observation_id.valid() || !metadata.source_id.valid() ||
        !metadata.code_build_id.valid())
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "required provenance identifier is invalid"};
    if (!valid_metadata_text(metadata.original_input_ref, 4096))
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "original_input_ref must be non-empty valid UTF-8 without control characters"};
    if (!valid_utc_timestamp(metadata.observed_at_utc))
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "observed_at_utc must use YYYY-MM-DDTHH:MM:SSZ"};
    if (!valid_locale(metadata.locale))
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "locale must be a basic BCP 47 language tag"};
    if (contracts::to_string(metadata.partition) == "invalid_partition")
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "partition value is invalid"};
    if (metadata.partition == contracts::DataPartition::not_applicable) {
        if (metadata.partition_id)
            return ValidationIssue{contracts::Status::invalid_schema,
                contracts::DiagnosticCode::invalid_schema, "not_applicable partition must not carry partition_id"};
    } else if (!metadata.partition_id || !metadata.partition_id->valid()) {
        return ValidationIssue{contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_schema, "a named partition requires a valid partition_id"};
    }
    return std::nullopt;
}

}  // namespace

TextAdapter::TextAdapter(AdapterLimits limits) : limits_(limits) {
    if (limits_.max_content_bytes == 0 || limits_.max_content_bytes > kHardMaxContentBytes)
        throw std::invalid_argument("max_content_bytes must be within 1..4 MiB");
}

AdapterResult TextAdapter::receive(std::string_view content, TextInputMetadata metadata) const {
    if (content.size() > limits_.max_content_bytes)
        return {contracts::Status::resource_limit, std::nullopt, content.size(),
            "input exceeds the configured UTF-8 byte limit"};
    if (content.empty())
        return {contracts::Status::invalid_schema, std::nullopt, 0,
            "text input must not be empty"};
    if (!valid_utf8_text(content))
        return {contracts::Status::invalid_schema, std::nullopt, content.size(),
            "text must be well-formed UTF-8 encoding Unicode scalar values"};
    if (const auto issue = validate_metadata(metadata))
        return {issue->status, std::nullopt, content.size(), issue->message};

    TextObservation observation{std::move(metadata), std::string(content)};
    return {contracts::Status::success, std::move(observation), content.size(), {}};
}

contracts::RecordEnvelope to_record(const TextObservation& observation) {
    contracts::RecordEnvelope record;
    record.schema_version = kTextInputSchemaVersion;
    record.identity.run_id = observation.metadata.run_id;
    record.identity.result_id = observation.metadata.result_id;
    record.identity.schema_id = contracts::SchemaId{std::string(kTextInputSchemaId)};
    record.identity.code_build_id = observation.metadata.code_build_id;
    record.identity.partition = observation.metadata.partition;
    record.identity.partition_id = observation.metadata.partition_id;
    record.identity.observation_id = observation.metadata.observation_id;
    record.identity.source_id = observation.metadata.source_id;
    record.status = contracts::Status::success;
    record.result = Value{Object{
        {"content", Value{observation.content}},
        {"content_bytes", Value{static_cast<std::uint64_t>(observation.content.size())}},
        {"locale", Value{observation.metadata.locale}},
        {"media_type", Value{"text/plain; charset=utf-8"}},
        {"normalization", Value{"none"}},
        {"observed_at_utc", Value{observation.metadata.observed_at_utc}},
        {"original_input_ref", Value{observation.metadata.original_input_ref}}}};
    record.assumptions = {"Text is validated and preserved without semantic parsing or normalization."};
    record.lineage.push_back({contracts::AnyIdentifier{observation.metadata.observation_id},
        contracts::LineageRelation::observes});
    record.lineage.push_back({contracts::AnyIdentifier{observation.metadata.source_id},
        contracts::LineageRelation::cites});
    record.exactness = contracts::Exactness::not_applicable;
    record.reproducibility.algorithm = std::string(kTextAdapterVersion);
    return record;
}

std::string serialize(const TextObservation& observation) {
    return contracts::encode_record(to_record(observation));
}

}  // namespace xai::interaction
