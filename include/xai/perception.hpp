#pragma once

#include "xai/contracts.hpp"
#include "xai/text_input.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xai::perception {

inline constexpr std::string_view kMeaningCandidatesSchemaId =
    "xai.perception.text-meaning-candidates";
inline constexpr std::uint32_t kMeaningCandidatesSchemaVersion = 1;
inline constexpr std::string_view kEnglishRulesBackendId =
    "xai.perception.english-rules-v1";

struct Limits {
    std::size_t max_input_bytes{64U * 1024U};
    std::size_t max_tokens{4096};
    std::size_t max_candidates{3};
    std::size_t max_nodes_per_candidate{64};
    std::size_t max_edges_per_candidate{128};
    std::size_t max_output_bytes{1U * 1024U * 1024U};
    std::size_t max_memory_bytes{16U * 1024U * 1024U};
    std::uint64_t max_wall_time_ms{1000};
};

struct Options {
    Limits limits{};
    std::optional<contracts::ResultId> output_result_id;
};

struct ByteSpan {
    std::uint64_t begin{0};
    std::uint64_t end{0};
    friend bool operator==(const ByteSpan&, const ByteSpan&) = default;
};

struct SurfaceToken {
    std::string folded;
    ByteSpan span;
    bool punctuation{false};
};

struct SurfaceAnalysis {
    std::vector<SurfaceToken> tokens;
    std::vector<ByteSpan> question_mark_spans;
    std::vector<ByteSpan> quote_spans;
    bool contains_quote{false};
};

struct LanguageHypothesis {
    std::string tag;
    std::uint32_t rank{1};
    std::string basis;
};

struct LanguageEvidence {
    std::string declared_locale;
    std::vector<LanguageHypothesis> hypotheses;
    bool unresolved{true};
};

struct CandidateNode {
    std::string id;
    std::string type;
    std::string concept_label;
    std::vector<ByteSpan> source_spans;
    bool implicit{false};
    std::string polarity{"positive"};
    std::string modality{"asserted"};
    bool quoted{false};
    std::vector<ByteSpan> cue_spans;
};

struct CandidateEdge {
    std::string id;
    std::string source;
    std::string target;
    std::string relation;
};

struct CandidateGraph {
    std::string candidate_id;
    std::uint32_t rank{1};
    std::string speech_act{"assertion"};
    std::vector<CandidateNode> nodes;
    std::vector<CandidateEdge> edges;
};

struct UnresolvedItem {
    std::string kind;
    std::vector<ByteSpan> source_spans;
};

struct ComponentResult {
    contracts::Status status{contracts::Status::unknown};
    std::string diagnostic;
    std::optional<interaction::TextObservation> observation;
};

struct SurfaceResult {
    contracts::Status status{contracts::Status::success};
    std::string diagnostic;
    std::optional<SurfaceAnalysis> analysis;
};

struct BackendResult {
    contracts::Status status{contracts::Status::success};
    std::string diagnostic;
    std::vector<CandidateGraph> candidates;
    std::vector<UnresolvedItem> unresolved;
    bool truncated{false};
    std::uint64_t operations{0};
};

class InputValidator {
public:
    explicit InputValidator(Limits limits = {});
    [[nodiscard]] ComponentResult validate(const interaction::TextObservation& observation) const;
    [[nodiscard]] ComponentResult decode_and_validate(const contracts::RecordEnvelope& record) const;
    [[nodiscard]] ComponentResult decode_and_validate(std::string_view canonical_json) const;
private:
    Limits limits_;
};

class SurfaceAnalyzer {
public:
    explicit SurfaceAnalyzer(Limits limits = {});
    [[nodiscard]] SurfaceResult analyze(std::string_view original_utf8) const;
private:
    Limits limits_;
};

class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;
    [[nodiscard]] virtual std::string_view identifier() const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::string_view> language_hint_for_locale(
        std::string_view declared_locale) const noexcept = 0;
    [[nodiscard]] virtual BackendResult generate(
        const interaction::TextObservation& observation,
        const SurfaceAnalysis& surface,
        std::size_t candidate_limit,
        const Limits& limits,
        std::chrono::steady_clock::time_point deadline) const = 0;
};

// A deterministic baseline for a small, declared subset of English. It is not
// a general parser and emits no calibrated probabilities.
class EnglishRuleBackend final : public InferenceBackend {
public:
    [[nodiscard]] std::string_view identifier() const noexcept override;
    [[nodiscard]] std::optional<std::string_view> language_hint_for_locale(
        std::string_view declared_locale) const noexcept override;
    [[nodiscard]] BackendResult generate(
        const interaction::TextObservation& observation,
        const SurfaceAnalysis& surface,
        std::size_t candidate_limit,
        const Limits& limits,
        std::chrono::steady_clock::time_point deadline) const override;
};

class GraphValidator {
public:
    [[nodiscard]] std::vector<std::string> validate(
        const CandidateGraph& graph,
        std::string_view original_utf8,
        const Limits& limits) const;
};

class Engine {
public:
    explicit Engine(Options options = {},
                    std::shared_ptr<const InferenceBackend> backend =
                        std::make_shared<EnglishRuleBackend>());

    [[nodiscard]] contracts::RecordEnvelope analyze(
        const interaction::TextObservation& observation) const;
    [[nodiscard]] contracts::RecordEnvelope analyze(
        const contracts::RecordEnvelope& input_record) const;
    [[nodiscard]] contracts::RecordEnvelope analyze_record_json(
        std::string_view canonical_json) const;
    [[nodiscard]] std::string serialize(
        const interaction::TextObservation& observation) const;
    [[nodiscard]] std::string serialize_record_json(
        std::string_view canonical_json) const;

private:
    Options options_;
    std::shared_ptr<const InferenceBackend> backend_;
};

}  // namespace xai::perception
