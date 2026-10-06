#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <utility>

namespace xai::contracts {

template <typename Tag>
class Identifier {
public:
    Identifier() = default;
    explicit Identifier(std::string value);
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept;
    friend bool operator==(const Identifier&, const Identifier&) = default;
    friend bool operator<(const Identifier& left, const Identifier& right) {
        return left.value_ < right.value_;
    }
private:
    std::string value_;
};

struct RunTag; struct ObservationTag; struct SourceTag; struct EvidenceTag;
struct FactTag; struct EntityTag; struct ModelTag; struct SchemaTag;
struct CodeBuildTag; struct PartitionTag; struct QueryTag; struct CertificateTag;
struct ResultTag;
using RunId = Identifier<RunTag>;
using ObservationId = Identifier<ObservationTag>;
using SourceId = Identifier<SourceTag>;
using EvidenceId = Identifier<EvidenceTag>;
using FactId = Identifier<FactTag>;
using EntityId = Identifier<EntityTag>;
using ModelId = Identifier<ModelTag>;
using SchemaId = Identifier<SchemaTag>;
using CodeBuildId = Identifier<CodeBuildTag>;
using PartitionId = Identifier<PartitionTag>;
using QueryId = Identifier<QueryTag>;
using CertificateId = Identifier<CertificateTag>;
using ResultId = Identifier<ResultTag>;
using AnyIdentifier = std::variant<ObservationId, SourceId, EvidenceId, FactId,
                                   EntityId, ModelId, SchemaId, CodeBuildId,
                                   PartitionId, QueryId, CertificateId, ResultId>;

enum class Status {
    success, unsupported_input, invalid_schema, unknown, inconsistent,
    zero_normalizer, timeout, resource_limit, approximate, non_identified,
    alarm_frozen, certificate_rejected, abstention
};
enum class Exactness { exact, approximate, not_applicable };
enum class DataPartition { training, development, calibration, final_test,
                            streaming, not_applicable };
enum class LineageRelation { derived_from, observes, cites, produced_by, validates };
enum class DiagnosticCode {
    invalid_record, malformed_input, unsupported_input, invalid_schema, unknown,
    inconsistent, zero_normalizer, timeout, resource_limit, approximation,
    non_identified, alarm_frozen, certificate_rejected, abstention
};

[[nodiscard]] std::string_view to_string(Status value) noexcept;
[[nodiscard]] std::string_view to_string(Exactness value) noexcept;
[[nodiscard]] std::string_view to_string(DataPartition value) noexcept;
[[nodiscard]] std::string_view to_string(LineageRelation value) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticCode value) noexcept;

struct CanonicalValue {
    using Array = std::vector<CanonicalValue>;
    using Object = std::map<std::string, CanonicalValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t,
                                 std::string, Array, Object>;
    Storage data{nullptr};
    CanonicalValue() = default;
    CanonicalValue(std::nullptr_t) : data(nullptr) {}
    CanonicalValue(bool value) : data(value) {}
    CanonicalValue(int value) : data(static_cast<std::int64_t>(value)) {}
    CanonicalValue(unsigned int value) : data(static_cast<std::uint64_t>(value)) {}
    CanonicalValue(std::int64_t value) : data(value) {}
    CanonicalValue(std::uint64_t value);
    CanonicalValue(const char* value) : data(std::string(value)) {}
    CanonicalValue(std::string value) : data(std::move(value)) {}
    CanonicalValue(Array value) : data(std::move(value)) {}
    CanonicalValue(Object value) : data(std::move(value)) {}
    friend bool operator==(const CanonicalValue&, const CanonicalValue&) = default;
};

struct RecordIdentity {
    RunId run_id;
    ResultId result_id;
    SchemaId schema_id;
    CodeBuildId code_build_id;
    DataPartition partition{DataPartition::not_applicable};
    std::optional<ObservationId> observation_id;
    std::optional<SourceId> source_id;
    std::optional<EvidenceId> evidence_id;
    std::optional<FactId> fact_id;
    std::optional<EntityId> entity_id;
    std::optional<ModelId> model_id;
    std::optional<PartitionId> partition_id;
    std::optional<QueryId> query_id;
    std::optional<CertificateId> certificate_id;
};

struct LineageEntry {
    AnyIdentifier identifier;
    LineageRelation relation{LineageRelation::derived_from};
};
struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::invalid_record};
    std::string message;
    std::optional<std::string> field_path;
};
struct ResourceBudget {
    std::optional<std::uint64_t> cpu_time_ms;
    std::optional<std::uint64_t> memory_bytes;
    std::optional<std::uint64_t> operations;
};
struct ResourceUsage {
    std::uint64_t cpu_time_ms{0};
    std::uint64_t memory_bytes{0};
    std::uint64_t operations{0};
};
struct ReproducibilityMetadata {
    std::uint32_t format_version{1};
    std::string algorithm;
    std::optional<std::uint64_t> seed;
    std::map<std::string, std::string, std::less<>> artifact_sha256;
};
struct RecordEnvelope {
    std::uint32_t envelope_version{1};
    std::uint32_t schema_version{1};
    RecordIdentity identity;
    Status status{Status::unknown};
    std::optional<CanonicalValue> result;
    std::vector<std::string> assumptions;
    std::vector<LineageEntry> lineage;
    Exactness exactness{Exactness::not_applicable};
    ResourceBudget budget;
    ResourceUsage consumed;
    std::vector<Diagnostic> warnings;
    std::vector<Diagnostic> errors;
    ReproducibilityMetadata reproducibility;
};

struct DecodeFailure {
    Status status{Status::invalid_schema};
    std::string message;
};
struct SchemaExpectation {
    SchemaId schema_id;
    std::uint32_t schema_version{1};
};
using DecodeResult = std::variant<RecordEnvelope, DecodeFailure>;

[[nodiscard]] bool valid_identifier(std::string_view value) noexcept;
[[nodiscard]] std::vector<std::string> validate_record(const RecordEnvelope& record);
[[nodiscard]] std::string encode_record(const RecordEnvelope& record);
[[nodiscard]] DecodeResult decode_record(
    std::string_view json,
    const std::optional<SchemaExpectation>& expected = std::nullopt);

}  // namespace xai::contracts
