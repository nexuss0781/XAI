#include "xai/contracts.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace {
using namespace xai::contracts;
static_assert(!std::is_same_v<RunId, SourceId>);
static_assert(!std::is_convertible_v<RunId, SourceId>);
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
RecordEnvelope valid_record() {
    RecordEnvelope record;
    record.identity.run_id = RunId{"run-01"};
    record.identity.result_id = ResultId{"result-01"};
    record.identity.schema_id = SchemaId{"xai.result"};
    record.identity.code_build_id = CodeBuildId{"git:a339b33"};
    record.identity.partition = DataPartition::development;
    record.identity.partition_id = PartitionId{"mcc24-public-even"};
    record.identity.observation_id = ObservationId{"obs-17"};
    record.identity.source_id = SourceId{"zenodo:14249068"};
    record.identity.evidence_id = EvidenceId{"ev-17"};
    record.identity.fact_id = FactId{"fact-17"};
    record.identity.entity_id = EntityId{"formula:random_034"};
    record.identity.model_id = ModelId{"wmc-reference-v1"};
    record.identity.query_id = QueryId{"exact-count"};
    record.status = Status::success;
    record.result = CanonicalValue::Object{
        {"count", CanonicalValue::Object{{"$rational", CanonicalValue::Array{"3", "5"}}}},
        {"satisfiable", true}};
    record.assumptions = {"exact rational weights; no projection"};
    record.lineage.push_back({AnyIdentifier{record.identity.observation_id.value()},
                              LineageRelation::observes});
    record.lineage.push_back({AnyIdentifier{record.identity.source_id.value()},
                              LineageRelation::cites});
    record.exactness = Exactness::exact;
    record.budget.cpu_time_ms = 600000;
    record.budget.memory_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    record.consumed.cpu_time_ms = 21;
    record.consumed.memory_bytes = 65536;
    record.consumed.operations = 120;
    record.reproducibility.algorithm = "exact-wmc-reference";
    record.reproducibility.seed = 0;
    record.reproducibility.artifact_sha256.emplace(
        "input.cnf", std::string(64, 'a'));
    return record;
}
void test_typed_identifiers_and_partitions() {
    require(valid_identifier("run-01") && valid_identifier("git:deadbeef"),
            "valid identifier syntax rejected");
    require(!valid_identifier("") && !valid_identifier("../secret") &&
                !valid_identifier("has space"),
            "invalid identifiers accepted");
    require(to_string(DataPartition::training) == "training" &&
                to_string(DataPartition::development) == "development" &&
                to_string(DataPartition::calibration) == "calibration" &&
                to_string(DataPartition::final_test) == "final_test" &&
                to_string(DataPartition::streaming) == "streaming" &&
                to_string(DataPartition::not_applicable) == "not_applicable",
            "partition labels are incomplete or unstable");
    auto record = valid_record();
    require(validate_record(record).empty(), "valid typed provenance record rejected");
    record.identity.partition = DataPartition::final_test;
    record.identity.partition_id.reset();
    require(!validate_record(record).empty(),
            "partitioned record without a typed partition ID was accepted");
}
void test_status_and_malformed_records() {
    auto record = valid_record();
    record.result.reset();
    require(!validate_record(record).empty(), "success without a result accepted");
    record = valid_record();
    record.errors.push_back({DiagnosticCode::invalid_record, "unexpected error", std::nullopt});
    require(!validate_record(record).empty(), "success with an error diagnostic accepted");
    record = valid_record();
    record.consumed.cpu_time_ms = 600001;
    require(!validate_record(record).empty(), "resource use above the declared budget accepted");
    record = valid_record();
    record.status = static_cast<Status>(255);
    require(!validate_record(record).empty(), "out-of-range status enum accepted");
    record = valid_record();
    record.identity.partition = static_cast<DataPartition>(255);
    require(!validate_record(record).empty(), "out-of-range partition enum accepted");
    record = valid_record();
    record.lineage.front().relation = static_cast<LineageRelation>(255);
    require(!validate_record(record).empty(), "out-of-range lineage enum accepted");
    record = valid_record();
    record.status = Status::timeout;
    record.result.reset();
    record.exactness = Exactness::not_applicable;
    record.errors.push_back({DiagnosticCode::timeout, "search deadline reached", "solver"});
    require(validate_record(record).empty(), "explicit timeout record rejected");
    const auto encoded = encode_record(record);
    const auto decoded = decode_record(encoded);
    require(std::holds_alternative<RecordEnvelope>(decoded),
            "explicit timeout did not survive decoding");
    const auto& recovered = std::get<RecordEnvelope>(decoded);
    require(recovered.status == Status::timeout && !recovered.result &&
                recovered.errors.front().code == DiagnosticCode::timeout,
            "timeout was converted to a successful or ambiguous result");
    const auto malformed = decode_record("{\"status\":\"success\",\"status\":\"unknown\"}");
    require(std::holds_alternative<DecodeFailure>(malformed),
            "duplicate-key malformed record accepted");
    const auto truncated = decode_record("{\"status\":");
    require(std::holds_alternative<DecodeFailure>(truncated),
            "truncated record accepted");
}
void test_canonical_round_trip_and_schema_mismatch() {
    auto record = valid_record();
    const std::string encoded = encode_record(record);
    require(encoded.find("\"status\":\"success\"") != std::string::npos,
            "status is not machine-readable in the canonical record");
    require(encoded.find("3.0") == std::string::npos,
            "canonical record unexpectedly contains a floating-point number");
    const auto decoded = decode_record(encoded, SchemaExpectation{SchemaId{"xai.result"}, 1});
    require(std::holds_alternative<RecordEnvelope>(decoded), "valid record failed round-trip");
    const auto& recovered = std::get<RecordEnvelope>(decoded);
    require(encode_record(recovered) == encoded,
            "canonical serialization did not round-trip byte-for-byte");
    require(recovered.identity.evidence_id == record.identity.evidence_id &&
                recovered.identity.partition == DataPartition::development &&
                recovered.lineage.size() == 2 && recovered.result == record.result,
            "typed IDs, partition, lineage, or structured result changed in round-trip");
    const auto wrong_schema = decode_record(
        encoded, SchemaExpectation{SchemaId{"xai.other"}, 1});
    require(std::holds_alternative<DecodeFailure>(wrong_schema) &&
                std::get<DecodeFailure>(wrong_schema).status == Status::invalid_schema,
            "schema mismatch did not return explicit invalid_schema");
    const auto wrong_version = decode_record(
        encoded, SchemaExpectation{SchemaId{"xai.result"}, 2});
    require(std::holds_alternative<DecodeFailure>(wrong_version) &&
                std::get<DecodeFailure>(wrong_version).status == Status::invalid_schema,
            "schema version mismatch did not return explicit invalid_schema");
    const auto noncanonical = decode_record("{ \"a\":1 }");
    require(std::holds_alternative<DecodeFailure>(noncanonical),
            "non-canonical JSON was accepted as canonical serialization");
    const auto floating = decode_record("{\"value\":1.25}");
    require(std::holds_alternative<DecodeFailure>(floating),
            "floating-point JSON number was accepted by the canonical parser");
}
void test_failure_status_catalog() {
    const Status statuses[] = {Status::success, Status::unsupported_input,
        Status::invalid_schema, Status::unknown, Status::inconsistent,
        Status::zero_normalizer, Status::timeout, Status::resource_limit,
        Status::approximate, Status::non_identified, Status::alarm_frozen,
        Status::certificate_rejected, Status::abstention};
    for (const auto status : statuses) {
        require(!to_string(status).empty(), "status lacks stable machine-readable name");
    }
    struct FailureCase { Status status; DiagnosticCode code; const char* message; };
    const FailureCase failures[] = {
        {Status::unsupported_input, DiagnosticCode::unsupported_input, "unsupported input"},
        {Status::invalid_schema, DiagnosticCode::invalid_schema, "schema rejected"},
        {Status::unknown, DiagnosticCode::unknown, "outcome unknown"},
        {Status::inconsistent, DiagnosticCode::inconsistent, "inconsistent evidence"},
        {Status::zero_normalizer, DiagnosticCode::zero_normalizer, "normalizer is zero"},
        {Status::timeout, DiagnosticCode::timeout, "deadline reached"},
        {Status::resource_limit, DiagnosticCode::resource_limit, "memory limit reached"},
        {Status::non_identified, DiagnosticCode::non_identified, "query not identified"},
        {Status::alarm_frozen, DiagnosticCode::alarm_frozen, "updates frozen"},
        {Status::certificate_rejected, DiagnosticCode::certificate_rejected, "certificate rejected"},
        {Status::abstention, DiagnosticCode::abstention, "system abstained"}};
    for (const auto& failure : failures) {
        auto failed = valid_record();
        failed.status = failure.status;
        failed.result.reset();
        failed.exactness = Exactness::not_applicable;
        failed.errors.push_back({failure.code, failure.message, std::nullopt});
        const auto round_trip = decode_record(encode_record(failed));
        require(std::holds_alternative<RecordEnvelope>(round_trip),
                "explicit failure status failed to serialize");
        const auto& restored = std::get<RecordEnvelope>(round_trip);
        require(restored.status == failure.status && !restored.result &&
                    restored.errors.front().code == failure.code,
                "failure status was silently converted or merged during replay");
    }
    auto record = valid_record();
    record.status = Status::approximate;
    record.exactness = Exactness::approximate;
    record.warnings.push_back({DiagnosticCode::approximation,
                               "bounded approximation used", std::nullopt});
    require(validate_record(record).empty(), "well-formed approximate result rejected");
    record.exactness = Exactness::exact;
    require(!validate_record(record).empty(), "approximate status with exact designation accepted");
}
}  // namespace

int main() {
    try {
        test_typed_identifiers_and_partitions();
        std::cout << "PASS identifiers_and_partitions\n";
        test_status_and_malformed_records();
        std::cout << "PASS status_and_malformed_records\n";
        test_canonical_round_trip_and_schema_mismatch();
        std::cout << "PASS canonical_round_trip_and_schema_mismatch\n";
        test_failure_status_catalog();
        std::cout << "PASS failure_status_catalog\n";
        std::cout << "RESULT: 4/4 contract test groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
