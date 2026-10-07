#include "xai/perception.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {
using namespace xai;
using namespace xai::perception;
using Value = contracts::CanonicalValue;
using Object = Value::Object;
using Array = Value::Array;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] interaction::TextInputMetadata metadata(std::string locale = "en-US") {
    interaction::TextInputMetadata value;
    value.run_id = contracts::RunId{"run:perception-test"};
    value.result_id = contracts::ResultId{"result:perception-input"};
    value.observation_id = contracts::ObservationId{"observation:perception-test"};
    value.source_id = contracts::SourceId{"source:perception-test"};
    value.code_build_id = contracts::CodeBuildId{"build:adapter-test"};
    value.original_input_ref = "fixture:perception";
    value.observed_at_utc = "2026-10-08T00:00:00Z";
    value.locale = std::move(locale);
    return value;
}

[[nodiscard]] interaction::TextObservation observation(std::string text,
                                                        std::string locale = "en-US") {
    const interaction::TextAdapter adapter;
    auto accepted = adapter.receive(text, metadata(std::move(locale)));
    require(accepted.accepted(), "fixture observation was rejected by the text adapter");
    return *accepted.observation;
}

[[nodiscard]] const Object& object(const Value& value, std::string_view label) {
    const auto* result = std::get_if<Object>(&value.data);
    require(result != nullptr, std::string(label) + " must be an object");
    return *result;
}

[[nodiscard]] const Value& field(const Object& value, std::string_view name) {
    const auto found = value.find(name);
    require(found != value.end(), "missing field: " + std::string(name));
    return found->second;
}

[[nodiscard]] const Array& array(const Value& value, std::string_view label) {
    const auto* result = std::get_if<Array>(&value.data);
    require(result != nullptr, std::string(label) + " must be an array");
    return *result;
}

[[nodiscard]] std::string string(const Value& value, std::string_view label) {
    const auto* result = std::get_if<std::string>(&value.data);
    require(result != nullptr, std::string(label) + " must be a string");
    return *result;
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_integer(const Value& value) {
    if (const auto* result = std::get_if<std::uint64_t>(&value.data)) return *result;
    if (const auto* result = std::get_if<std::int64_t>(&value.data); result && *result >= 0)
        return static_cast<std::uint64_t>(*result);
    return std::nullopt;
}

void test_attachment_generates_bounded_source_linked_alternatives() {
    const std::string text = "I saw the man with a telescope.";
    const auto input = observation(text);
    const Engine engine;
    const auto record = engine.analyze(input);
    require(record.status == contracts::Status::approximate && record.result.has_value(),
            "supported English clause should yield an approximate candidate record");
    require(record.identity.schema_id.value() == kMeaningCandidatesSchemaId &&
                record.schema_version == kMeaningCandidatesSchemaVersion,
            "perception output schema/version mismatch");
    require(record.identity.observation_id == input.metadata.observation_id &&
                record.identity.source_id == input.metadata.source_id,
            "output identity lost source provenance");
    require(record.lineage.size() >= 4, "output is missing input or build lineage");
    require(record.budget.memory_bytes && record.consumed.memory_bytes > 0 &&
                record.consumed.memory_bytes <= *record.budget.memory_bytes,
            "memory budget or conservative component-use estimate is missing");

    const auto& payload = object(*record.result, "candidate payload");
    const auto& reproducibility = object(field(payload, "reproducibility"), "reproducibility");
    const auto configuration_hash = string(field(reproducibility, "configuration_sha256"),
                                            "configuration hash");
    const auto envelope_hash = record.reproducibility.artifact_sha256.find("configuration");
    require(configuration_hash.size() == 64 &&
                envelope_hash != record.reproducibility.artifact_sha256.end() &&
                envelope_hash->second == configuration_hash,
            "configuration SHA-256 is missing or inconsistent across output metadata");
    const auto& candidate_values = array(field(payload, "candidates"), "candidates");
    require(candidate_values.size() == 2,
            "prepositional attachment should preserve both supported readings");
    const auto& first_candidate = object(candidate_values[0], "first candidate");
    const auto& second_candidate = object(candidate_values[1], "second candidate");
    const auto& first_graph = object(field(first_candidate, "graph"), "first graph");
    const auto& second_graph = object(field(second_candidate, "graph"), "second graph");
    const auto& first_edges = array(field(first_graph, "edges"), "first graph edges");
    const auto& second_edges = array(field(second_graph, "edges"), "second graph edges");
    bool has_instrument = false;
    bool has_modifier = false;
    for (const auto& edge_value : first_edges) {
        const auto& edge = object(edge_value, "edge");
        has_instrument = has_instrument || string(field(edge, "relation"), "relation") == "xai:instrument";
    }
    for (const auto& edge_value : second_edges) {
        const auto& edge = object(edge_value, "edge");
        has_modifier = has_modifier || string(field(edge, "relation"), "relation") ==
            "xai:has-associated-modifier";
    }
    require(has_instrument && has_modifier, "attachment graph relations do not represent both readings");
    const auto& ambiguity = object(field(payload, "ambiguity"), "ambiguity");
    require(std::get<bool>(field(ambiguity, "exhaustive").data) == false,
            "rule backend must not claim exhaustive interpretations");
    require(std::get<bool>(field(ambiguity, "truncated").data) == false,
            "two-candidate example should not be marked truncated at default limit");
    require(string(field(payload, "offset_convention"), "offset convention").find("utf8_bytes") !=
                std::string::npos,
            "payload must declare original UTF-8 byte offsets");
    const auto encoded = contracts::encode_record(record);
    require(encoded.find(text) == std::string::npos,
            "candidate output must not copy the full input text");
    const auto decoded = contracts::decode_record(encoded,
        contracts::SchemaExpectation{contracts::SchemaId{std::string(kMeaningCandidatesSchemaId)}, 1});
    require(std::holds_alternative<contracts::RecordEnvelope>(decoded),
            "candidate record failed canonical shared-contract round trip");
}

void test_surface_cues_and_byte_spans() {
    const std::string text = "I may not see him.";
    const auto record = Engine{}.analyze(observation(text));
    require(record.status == contracts::Status::approximate && record.result,
            "supported negated/modal clause should produce a candidate");
    const auto& payload = object(*record.result, "payload");
    const auto& candidate = object(array(field(payload, "candidates"), "candidates").front(), "candidate");
    const auto& graph = object(field(candidate, "graph"), "graph");
    const auto& nodes = array(field(graph, "nodes"), "nodes");
    const auto& event = object(nodes.front(), "event node");
    require(string(field(event, "polarity"), "polarity") == "negative",
            "negation cue was not preserved in the event node");
    require(string(field(event, "modality"), "modality") == "may",
            "modal cue was not preserved in the event node");

    CandidateGraph invalid;
    invalid.candidate_id = "candidate-1";
    CandidateNode invalid_node;
    invalid_node.id = "n1";
    invalid_node.type = "xai:surface-mention";
    invalid_node.concept_label = "surface-mention";
    invalid_node.source_spans = {ByteSpan{1, 2}};
    invalid.nodes.push_back(std::move(invalid_node));
    const auto issues = GraphValidator{}.validate(invalid, "é", Limits{});
    require(!issues.empty(), "graph validator accepted a span inside a multibyte UTF-8 scalar");
}

void test_offsets_are_original_utf8_bytes() {
    const std::string text = "\xF0\x9F\x8C\x8D e\xCC\x81 I saw tea.";
    const auto record = Engine{}.analyze(observation(text));
    require(record.status == contracts::Status::approximate && record.result,
            "English clause following multibyte text should be parsed");
    const auto& payload = object(*record.result, "payload");
    const auto& candidate = object(array(field(payload, "candidates"), "candidates").front(), "candidate");
    const auto& graph = object(field(candidate, "graph"), "graph");
    const auto& nodes = array(field(graph, "nodes"), "nodes");
    const auto& event = object(nodes.front(), "event node");
    const auto& spans = array(field(event, "source_spans"), "event source spans");
    const auto& span = object(spans.front(), "event source span");
    const auto start = unsigned_integer(field(span, "begin_byte"));
    const auto end = unsigned_integer(field(span, "end_byte"));
    const auto expected = text.find("saw");
    require(start && end && *start == expected && *end == expected + 3,
            "generated span is not a zero-based byte span into the unchanged UTF-8 source");
}

void test_truncation_unsupported_and_abstention_are_explicit() {
    Options one_candidate;
    one_candidate.limits.max_candidates = 1;
    const auto truncated = Engine{one_candidate}.analyze(
        observation("I saw the man with a telescope."));
    require(truncated.status == contracts::Status::approximate && truncated.result,
            "candidate truncation must still return an approximate record");
    const auto& payload = object(*truncated.result, "payload");
    const auto& ambiguity = object(field(payload, "ambiguity"), "ambiguity");
    require(std::get<bool>(field(ambiguity, "truncated").data),
            "candidate limit must be recorded as truncation");
    require(array(field(payload, "candidates"), "candidates").size() == 1,
            "configured N-best limit was not applied");

    const auto unsupported = Engine{}.analyze(observation("Bonjour tout le monde.", "fr-FR"));
    require(unsupported.status == contracts::Status::unsupported_input && !unsupported.result &&
                !unsupported.errors.empty(),
            "unsupported language must fail closed with a typed diagnostic");
    const auto unknown = Engine{}.analyze(observation("Hello there."));
    require(unknown.status == contracts::Status::abstention && !unknown.result &&
                !unknown.errors.empty(),
            "unsupported clause structure must abstain rather than emit an empty success");
}

void test_exact_record_validation_and_resource_limits() {
    const auto input = observation("I like tea.");
    auto adapter_record = interaction::to_record(input);
    const auto from_record = Engine{}.analyze(adapter_record);
    require(from_record.status == contracts::Status::approximate,
            "compatible text-input envelope was not accepted");
    const auto from_json = Engine{}.analyze_record_json(interaction::serialize(input));
    require(from_json.status == contracts::Status::approximate,
            "canonical text-input JSON was not accepted by the record path");
    adapter_record.schema_version = 2;
    const auto wrong_version = Engine{}.analyze(adapter_record);
    require(wrong_version.status == contracts::Status::invalid_schema && !wrong_version.result,
            "incompatible input version must not be guessed or coerced");

    auto extra_field_record = interaction::to_record(input);
    auto* input_payload = std::get_if<Object>(&extra_field_record.result->data);
    require(input_payload != nullptr, "adapter input result should be an object");
    input_payload->emplace("unrecognized", Value{"not allowed"});
    const auto extra_field = Engine{}.analyze(extra_field_record);
    require(extra_field.status == contracts::Status::invalid_schema && !extra_field.result,
            "task input payload must reject unexpected fields in v1");

    Options small_input;
    small_input.limits.max_input_bytes = 4;
    const auto oversized = Engine{small_input}.analyze(input);
    require(oversized.status == contracts::Status::resource_limit && !oversized.result,
            "input byte limit must return a resource-limit record");

    Options small_graph;
    small_graph.limits.max_nodes_per_candidate = 2;
    const auto graph_limited = Engine{small_graph}.analyze(input);
    require(graph_limited.status == contracts::Status::resource_limit && !graph_limited.result,
            "candidate graph node ceiling must be reported as resource_limit");

    Options small_output;
    small_output.limits.max_output_bytes = 32;
    const auto output_limited = Engine{small_output}.analyze(input);
    require(output_limited.status == contracts::Status::resource_limit && !output_limited.result,
            "serialized output limit must return a resource-limit record");

    const auto malformed = Engine{}.analyze_record_json("not canonical JSON");
    require(malformed.status == contracts::Status::invalid_schema && !malformed.result,
            "malformed input record must return an invalid-schema record");
    (void)contracts::encode_record(malformed);
}

}  // namespace

int main() {
    try {
        test_attachment_generates_bounded_source_linked_alternatives();
        std::cout << "PASS perception: bounded alternative graphs, provenance, and canonical v1 output\n";
        test_surface_cues_and_byte_spans();
        std::cout << "PASS perception: negation/modality cues and UTF-8 scalar-aligned byte spans\n";
        test_offsets_are_original_utf8_bytes();
        std::cout << "PASS perception: offsets remain original UTF-8 byte offsets with multibyte prefixes\n";
        test_truncation_unsupported_and_abstention_are_explicit();
        std::cout << "PASS perception: truncation, unsupported locale, and abstention behavior\n";
        test_exact_record_validation_and_resource_limits();
        std::cout << "PASS perception: exact input schema, malformed records, and size budgets\n";
        std::cout << "RESULT: software contract checks passed; this is not evidence of general language understanding.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
