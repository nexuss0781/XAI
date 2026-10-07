#include "xai/perception.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xai::perception {
namespace {

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t width = 0;
        std::uint32_t code = 0;
        if (lead <= 0x7fU) { ++i; continue; }
        if (lead >= 0xc2U && lead <= 0xdfU) { width = 2; code = lead & 0x1fU; }
        else if (lead >= 0xe0U && lead <= 0xefU) { width = 3; code = lead & 0x0fU; }
        else if (lead >= 0xf0U && lead <= 0xf4U) { width = 4; code = lead & 0x07U; }
        else return false;
        if (width > text.size() - i) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (byte & 0x3fU);
        }
        if ((width == 3 && code < 0x800U) || (width == 4 && code < 0x10000U) ||
            code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return false;
        i += width;
    }
    return true;
}

[[nodiscard]] bool valid_locale(std::string_view locale) noexcept {
    if (locale.empty() || locale.size() > 63) return false;
    std::size_t subtag = 0;
    std::size_t index = 0;
    for (const unsigned char ch : locale) {
        if (ch == '-') {
            if (subtag == 0 || subtag > 8) return false;
            ++index;
            subtag = 0;
            continue;
        }
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if ((!alpha && !digit) || (index == 0 && !alpha)) return false;
        if (++subtag > 8) return false;
    }
    return subtag > 0;
}

[[nodiscard]] const contracts::CanonicalValue* field(
    const contracts::CanonicalValue::Object& object, std::string_view name) {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

[[nodiscard]] const std::string* string_value(const contracts::CanonicalValue* value) {
    return value == nullptr ? nullptr : std::get_if<std::string>(&value->data);
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_value(
    const contracts::CanonicalValue* value) {
    if (value == nullptr) return std::nullopt;
    if (const auto* number = std::get_if<std::uint64_t>(&value->data)) return *number;
    if (const auto* number = std::get_if<std::int64_t>(&value->data); number && *number >= 0)
        return static_cast<std::uint64_t>(*number);
    return std::nullopt;
}

[[nodiscard]] bool has_lineage(const contracts::RecordEnvelope& record,
                               const contracts::AnyIdentifier& identifier,
                               contracts::LineageRelation relation) {
    return std::any_of(record.lineage.begin(), record.lineage.end(),
        [&](const contracts::LineageEntry& entry) {
            return entry.relation == relation && entry.identifier == identifier;
        });
}

[[nodiscard]] std::optional<contracts::Status> validate_observation(
    const interaction::TextObservation& observation, const Limits& limits,
    std::string& message) {
    const auto& metadata = observation.metadata;
    if (!metadata.run_id.valid() || !metadata.result_id.valid() ||
        !metadata.observation_id.valid() || !metadata.source_id.valid() ||
        !metadata.code_build_id.valid()) {
        message = "required provenance identifier is invalid";
        return contracts::Status::invalid_schema;
    }
    if (observation.content.empty()) {
        message = "text content must not be empty";
        return contracts::Status::invalid_schema;
    }
    if (observation.content.size() > limits.max_input_bytes) {
        message = "input exceeds the configured perception byte limit";
        return contracts::Status::resource_limit;
    }
    if (observation.content.size() > interaction::kHardMaxContentBytes) {
        message = "input exceeds the text adapter hard byte limit";
        return contracts::Status::resource_limit;
    }
    if (!valid_utf8(observation.content)) {
        message = "text content is not well-formed UTF-8";
        return contracts::Status::invalid_schema;
    }
    if (!valid_locale(metadata.locale)) {
        message = "declared locale is malformed";
        return contracts::Status::invalid_schema;
    }
    if (metadata.original_input_ref.empty() || metadata.original_input_ref.size() > 4096 ||
        !valid_utf8(metadata.original_input_ref)) {
        message = "original input reference is missing or invalid";
        return contracts::Status::invalid_schema;
    }
    if (metadata.observed_at_utc.empty() || !valid_utf8(metadata.observed_at_utc)) {
        message = "observation timestamp is missing or invalid";
        return contracts::Status::invalid_schema;
    }
    if (contracts::to_string(metadata.partition) == "invalid_partition") {
        message = "data partition is invalid";
        return contracts::Status::invalid_schema;
    }
    if ((metadata.partition == contracts::DataPartition::not_applicable && metadata.partition_id) ||
        (metadata.partition != contracts::DataPartition::not_applicable &&
         (!metadata.partition_id || !metadata.partition_id->valid()))) {
        message = "partition label and partition identifier do not match";
        return contracts::Status::invalid_schema;
    }
    return std::nullopt;
}

}  // namespace

InputValidator::InputValidator(Limits limits) : limits_(limits) {
    if (limits_.max_input_bytes == 0 ||
        limits_.max_input_bytes > interaction::kHardMaxContentBytes)
        throw std::invalid_argument("max_input_bytes must be within 1..4 MiB");
}

ComponentResult InputValidator::validate(const interaction::TextObservation& observation) const {
    std::string message;
    if (const auto status = validate_observation(observation, limits_, message))
        return {*status, std::move(message), std::nullopt};
    return {contracts::Status::success, {}, observation};
}

ComponentResult InputValidator::decode_and_validate(
    const contracts::RecordEnvelope& record) const {
    if (!contracts::validate_record(record).empty())
        return {contracts::Status::invalid_schema,
                "input text record violates the shared envelope contract", std::nullopt};
    if (record.identity.schema_id.value() != interaction::kTextInputSchemaId ||
        record.schema_version != interaction::kTextInputSchemaVersion)
        return {contracts::Status::invalid_schema,
                "input record must use the exact xai.interaction.text-input v1 schema", std::nullopt};
    if (record.status != contracts::Status::success || !record.result)
        return {contracts::Status::invalid_schema,
                "input text record must be a successful record with a result", std::nullopt};
    if (!record.identity.run_id.valid() || !record.identity.result_id.valid() ||
        !record.identity.code_build_id.valid() || !record.identity.observation_id ||
        !record.identity.observation_id->valid() || !record.identity.source_id ||
        !record.identity.source_id->valid())
        return {contracts::Status::invalid_schema,
                "input text record is missing valid required provenance identifiers", std::nullopt};
    if (!has_lineage(record, contracts::AnyIdentifier{*record.identity.observation_id},
                     contracts::LineageRelation::observes) ||
        !has_lineage(record, contracts::AnyIdentifier{*record.identity.source_id},
                     contracts::LineageRelation::cites))
        return {contracts::Status::invalid_schema,
                "input text record is missing required observation or source lineage", std::nullopt};

    const auto* object = std::get_if<contracts::CanonicalValue::Object>(&record.result->data);
    if (object == nullptr)
        return {contracts::Status::invalid_schema, "input text result must be an object", std::nullopt};
    const auto* content = string_value(field(*object, "content"));
    const auto* locale = string_value(field(*object, "locale"));
    const auto* original_ref = string_value(field(*object, "original_input_ref"));
    const auto* observed_at = string_value(field(*object, "observed_at_utc"));
    const auto* media_type = string_value(field(*object, "media_type"));
    const auto* normalization = string_value(field(*object, "normalization"));
    const auto content_bytes = unsigned_value(field(*object, "content_bytes"));
    if (object->size() != 7 || content == nullptr || locale == nullptr || original_ref == nullptr || observed_at == nullptr ||
        media_type == nullptr || normalization == nullptr || !content_bytes ||
        *content_bytes != content->size() || *media_type != "text/plain; charset=utf-8" ||
        *normalization != "none")
        return {contracts::Status::invalid_schema,
                "input text result fields do not match the v1 adapter contract", std::nullopt};

    interaction::TextInputMetadata metadata;
    metadata.run_id = record.identity.run_id;
    metadata.result_id = record.identity.result_id;
    metadata.observation_id = *record.identity.observation_id;
    metadata.source_id = *record.identity.source_id;
    metadata.code_build_id = record.identity.code_build_id;
    metadata.original_input_ref = *original_ref;
    metadata.observed_at_utc = *observed_at;
    metadata.locale = *locale;
    metadata.partition = record.identity.partition;
    metadata.partition_id = record.identity.partition_id;
    return validate(interaction::TextObservation{std::move(metadata), *content});
}

ComponentResult InputValidator::decode_and_validate(std::string_view canonical_json) const {
    const auto decoded = contracts::decode_record(canonical_json,
        contracts::SchemaExpectation{contracts::SchemaId{std::string(interaction::kTextInputSchemaId)},
                                     interaction::kTextInputSchemaVersion});
    if (const auto* failure = std::get_if<contracts::DecodeFailure>(&decoded)) {
        (void)failure;
        return {contracts::Status::invalid_schema,
                "input is not a valid canonical text-input v1 record", std::nullopt};
    }
    return decode_and_validate(std::get<contracts::RecordEnvelope>(decoded));
}

}  // namespace xai::perception
