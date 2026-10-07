#include "xai/text_input.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {
using namespace xai;
using namespace xai::interaction;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] TextInputMetadata metadata() {
    TextInputMetadata value;
    value.run_id = contracts::RunId{"run:text-test"};
    value.result_id = contracts::ResultId{"result:text-test"};
    value.observation_id = contracts::ObservationId{"observation:text-test"};
    value.source_id = contracts::SourceId{"source:text-test"};
    value.code_build_id = contracts::CodeBuildId{"build:text-test"};
    value.original_input_ref = "test-fixture:text";
    value.observed_at_utc = "2026-10-07T00:00:00Z";
    value.locale = "en-US";
    return value;
}

void require_rejected(std::string_view content, std::string message,
                      contracts::Status expected = contracts::Status::invalid_schema) {
    const TextAdapter adapter;
    const auto result = adapter.receive(content, metadata());
    require(!result.accepted() && result.status == expected, message);
    require(!result.observation.has_value(), "rejected text must not be returned as accepted content");
    require(result.diagnostic.find("secret") == std::string::npos,
            "diagnostics must not include user content");
}

void test_valid_text_is_preserved_and_versioned() {
    const std::string content = "  Hello, \xE4\xB8\x96\xE7\x95\x8C!\n\t ";
    const TextAdapter adapter;
    const auto result = adapter.receive(content, metadata());
    require(result.accepted(), "valid UTF-8 text should be accepted");
    require(result.content_bytes == content.size(), "reported byte length mismatch");
    require(result.observation->content == content, "adapter changed whitespace or text bytes");
    require(result.observation->metadata.locale == "en-US", "locale metadata was not preserved");

    const std::string encoded = serialize(*result.observation);
    const auto decoded = contracts::decode_record(encoded,
        contracts::SchemaExpectation{contracts::SchemaId{std::string(kTextInputSchemaId)},
                                     kTextInputSchemaVersion});
    require(std::holds_alternative<contracts::RecordEnvelope>(decoded),
            "serialized text record failed contract round-trip");
    const auto& record = std::get<contracts::RecordEnvelope>(decoded);
    require(record.status == contracts::Status::success &&
                record.identity.observation_id == metadata().observation_id &&
                record.identity.source_id == metadata().source_id,
            "record status or provenance identity mismatch");
    require(record.assumptions.size() == 1 &&
                record.assumptions.front().find("without semantic parsing") != std::string::npos,
            "record must declare its no-interpretation boundary");
    const auto* result_object = std::get_if<contracts::CanonicalValue::Object>(&record.result->data);
    require(result_object != nullptr, "text-input record result must be an object");
    const auto content_field = result_object->find("content");
    require(content_field != result_object->end(), "record is missing preserved content");
    const auto* recorded_content = std::get_if<std::string>(&content_field->second.data);
    require(recorded_content != nullptr && *recorded_content == content,
            "canonical record did not preserve the original text");
}

void test_utf8_validation_and_unicode_preservation() {
    require_rejected("", "empty text must be rejected");
    require_rejected(std::string("\xC0\xAF", 2), "overlong UTF-8 must be rejected");
    require_rejected(std::string("\xF0\x9F", 2), "truncated UTF-8 must be rejected");
    require_rejected(std::string("\xED\xA0\x80", 3), "UTF-8 surrogate must be rejected");
    require_rejected(std::string("\xF4\x90\x80\x80", 4), "out-of-range Unicode must be rejected");

    const TextAdapter adapter;
    const std::string all_unicode =
        "Latin Ελληνικά Русский العربية עברית हिन्दी বাংলা 日本語 한국어 中文 "
        "\xF0\x9F\x8C\x8D \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB "
        "e\xCC\x81 \xC3\xA9 \xE2\x80\xAE";
    const auto unicode_result = adapter.receive(all_unicode, metadata());
    require(unicode_result.accepted() && unicode_result.observation->content == all_unicode,
            "Unicode scripts, supplementary characters, combining marks, or format controls changed");

    constexpr char control_bytes[] =
        "before\0\x01\x7f\xC2\x85\r\n\t\xE2\x80\xA8" "after";
    const std::string controls{control_bytes, sizeof(control_bytes) - 1};
    const auto control_result = adapter.receive(controls, metadata());
    require(control_result.accepted() && control_result.observation->content == controls,
            "valid Unicode control characters must be preserved rather than filtered");
    const std::string encoded = serialize(*control_result.observation);
    const auto decoded = contracts::decode_record(encoded,
        contracts::SchemaExpectation{contracts::SchemaId{std::string(kTextInputSchemaId)},
                                     kTextInputSchemaVersion});
    require(std::holds_alternative<contracts::RecordEnvelope>(decoded),
            "record with escaped text controls failed canonical JSON round-trip");
    const auto& record = std::get<contracts::RecordEnvelope>(decoded);
    const auto* object = std::get_if<contracts::CanonicalValue::Object>(&record.result->data);
    require(object != nullptr, "decoded text-input record result is not an object");
    const auto content_field = object->find("content");
    const auto* recorded = content_field == object->end()
        ? nullptr : std::get_if<std::string>(&content_field->second.data);
    require(recorded != nullptr && *recorded == controls,
            "control characters were not restored exactly after JSON decoding");
}

void test_limits_and_metadata_validation() {
    const TextAdapter four_bytes{AdapterLimits{4}};
    require(four_bytes.receive("1234", metadata()).accepted(), "input at the byte limit rejected");
    const auto too_large = four_bytes.receive("12345", metadata());
    require(!too_large.accepted() && too_large.status == contracts::Status::resource_limit,
            "input above the byte limit must fail with resource_limit");

    bool hard_cap_rejected = false;
    try {
        const TextAdapter excessive{AdapterLimits{kHardMaxContentBytes + 1}};
        (void)excessive;
    } catch (const std::invalid_argument&) {
        hard_cap_rejected = true;
    }
    require(hard_cap_rejected, "adapter configuration above the hard cap must be rejected");

    const std::string control_heavy(kHardMaxContentBytes, '\0');
    const TextAdapter hard_limit{AdapterLimits{kHardMaxContentBytes}};
    const auto large_text = hard_limit.receive(control_heavy, metadata());
    require(large_text.accepted() && large_text.content_bytes == kHardMaxContentBytes,
            "control-heavy text at the hard content ceiling must be accepted");
    const std::string large_record = serialize(*large_text.observation);
    require(large_record.size() < 32U * 1024U * 1024U,
            "escaped text record must fit within the 32 MiB canonical JSON ceiling");
    const auto large_decoded = contracts::decode_record(large_record,
        contracts::SchemaExpectation{contracts::SchemaId{std::string(kTextInputSchemaId)},
                                     kTextInputSchemaVersion});
    require(std::holds_alternative<contracts::RecordEnvelope>(large_decoded),
            "maximum-size escaped text failed canonical record round-trip");
    const auto& large_envelope = std::get<contracts::RecordEnvelope>(large_decoded);
    const auto* large_object = std::get_if<contracts::CanonicalValue::Object>(
        &large_envelope.result->data);
    require(large_object != nullptr, "maximum-size text result is not an object");
    const auto large_content_field = large_object->find("content");
    const auto* large_content = large_content_field == large_object->end()
        ? nullptr : std::get_if<std::string>(&large_content_field->second.data);
    require(large_content != nullptr && *large_content == control_heavy,
            "maximum-size control-heavy content changed during JSON round-trip");

    auto bad_time = metadata();
    bad_time.observed_at_utc = "2026-02-30T00:00:00Z";
    const auto time_result = TextAdapter{}.receive("text", bad_time);
    require(!time_result.accepted(), "invalid calendar date must be rejected");

    auto bad_locale = metadata();
    bad_locale.locale = "en--US";
    const auto locale_result = TextAdapter{}.receive("text", bad_locale);
    require(!locale_result.accepted(), "malformed locale tag must be rejected");

    auto bad_partition = metadata();
    bad_partition.partition = contracts::DataPartition::streaming;
    const auto partition_result = TextAdapter{}.receive("text", bad_partition);
    require(!partition_result.accepted(), "named partition without partition_id must be rejected");
}

}  // namespace

int main() {
    try {
        test_valid_text_is_preserved_and_versioned();
        std::cout << "PASS text adapter: byte-preserving UTF-8 input and canonical record round-trip\n";
        test_utf8_validation_and_unicode_preservation();
        std::cout << "PASS text adapter: malformed UTF-8 rejected; Unicode scripts, formatting, and controls preserved\n";
        test_limits_and_metadata_validation();
        std::cout << "PASS text adapter: byte ceiling, timestamp, locale, and partition metadata validated\n";
        std::cout << "RESULT: all text-input adapter tests passed. These are software checks, not evidence of language understanding.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
