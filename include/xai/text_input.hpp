#pragma once

#include "xai/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xai::interaction {

inline constexpr std::string_view kTextInputSchemaId = "xai.interaction.text-input";
inline constexpr std::uint32_t kTextInputSchemaVersion = 1;
inline constexpr std::string_view kTextAdapterVersion = "xai-text-pass-through-v1";
inline constexpr std::size_t kHardMaxContentBytes = 4U * 1024U * 1024U;

// Caller-supplied provenance and task-partition metadata. Raw user text belongs
// in TextObservation::content, never in these identifiers or reference fields.
struct TextInputMetadata {
    contracts::RunId run_id;
    contracts::ResultId result_id;
    contracts::ObservationId observation_id;
    contracts::SourceId source_id;
    contracts::CodeBuildId code_build_id;
    std::string original_input_ref;
    std::string observed_at_utc;
    std::string locale{"und"};
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    std::optional<contracts::PartitionId> partition_id;
};

// Accepted input is preserved byte-for-byte as well-formed UTF-8. The adapter
// accepts all Unicode scalar values, including control characters, and does not
// trim, normalize, translate, tokenize, or interpret content.
struct TextObservation {
    TextInputMetadata metadata;
    std::string content;
};

struct AdapterLimits {
    std::size_t max_content_bytes{1U * 1024U * 1024U};
};

struct AdapterResult {
    contracts::Status status{contracts::Status::unknown};
    std::optional<TextObservation> observation;
    std::size_t content_bytes{0};
    std::string diagnostic;

    [[nodiscard]] bool accepted() const noexcept {
        return status == contracts::Status::success && observation.has_value();
    }
};

class TextAdapter {
public:
    explicit TextAdapter(AdapterLimits limits = {});

    // Validates metadata, well-formed UTF-8, and the byte ceiling. On rejection,
    // the original text is not returned in diagnostics.
    [[nodiscard]] AdapterResult receive(std::string_view content,
                                        TextInputMetadata metadata) const;

private:
    AdapterLimits limits_;
};

// Wrap an accepted observation in XAI's shared versioned record contract.
// The content is present in the result by design: callers must apply their own
// access, retention, and logging policies to the returned record.
[[nodiscard]] contracts::RecordEnvelope to_record(const TextObservation& observation);
[[nodiscard]] std::string serialize(const TextObservation& observation);

}  // namespace xai::interaction
