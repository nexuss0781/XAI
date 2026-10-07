#include "xai/perception.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>

namespace xai::perception {
namespace {

[[nodiscard]] bool utf8_boundary(std::string_view text, std::uint64_t offset) noexcept {
    if (offset > text.size()) return false;
    if (offset == 0 || offset == text.size()) return true;
    return (static_cast<unsigned char>(text[static_cast<std::size_t>(offset)]) & 0xc0U) != 0x80U;
}

[[nodiscard]] bool valid_span(ByteSpan span, std::string_view text) noexcept {
    return span.begin < span.end && span.end <= text.size() &&
        utf8_boundary(text, span.begin) && utf8_boundary(text, span.end);
}

[[nodiscard]] bool valid_local_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 64) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
          (first >= '0' && first <= '9'))) return false;
    for (const unsigned char ch : value) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

[[nodiscard]] bool valid_relation(std::string_view value) noexcept {
    constexpr std::string_view prefix{"xai:"};
    if (!value.starts_with(prefix) || value.size() == prefix.size() || value.size() > 128)
        return false;
    for (const unsigned char ch : value.substr(prefix.size())) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> GraphValidator::validate(
    const CandidateGraph& graph, std::string_view original_utf8, const Limits& limits) const {
    std::vector<std::string> issues;
    if (!valid_local_id(graph.candidate_id)) issues.emplace_back("candidate id is invalid");
    if (graph.rank == 0) issues.emplace_back("candidate rank must be positive");
    if (graph.speech_act != "assertion" && graph.speech_act != "question" &&
        graph.speech_act != "command" && graph.speech_act != "quoted-content" &&
        graph.speech_act != "unknown") issues.emplace_back("speech-act value is not recognized");
    if (graph.nodes.empty()) issues.emplace_back("candidate graph must contain at least one node");
    if (graph.nodes.size() > limits.max_nodes_per_candidate)
        issues.emplace_back("candidate graph exceeds the configured node limit");
    if (graph.edges.size() > limits.max_edges_per_candidate)
        issues.emplace_back("candidate graph exceeds the configured edge limit");

    std::set<std::string, std::less<>> node_ids;
    for (const auto& node : graph.nodes) {
        if (!valid_local_id(node.id) || !node_ids.insert(node.id).second)
            issues.emplace_back("node ids must be valid and unique within a candidate");
        if (node.type.empty() || !node.type.starts_with("xai:") || node.type.size() > 128)
            issues.emplace_back("node type must use the xai namespace");
        if (node.concept_label.empty()) issues.emplace_back("node concept must not be empty");
        if (node.source_spans.empty() && !node.implicit)
            issues.emplace_back("unanchored node must be explicitly marked implicit");
        if (!node.source_spans.empty() && node.implicit)
            issues.emplace_back("anchored node cannot also be marked implicit");
        for (const auto span : node.source_spans) {
            if (!valid_span(span, original_utf8))
                issues.emplace_back("node source span is out of range or not on UTF-8 scalar boundaries");
        }
        for (const auto span : node.cue_spans) {
            if (!valid_span(span, original_utf8))
                issues.emplace_back("node cue span is out of range or not on UTF-8 scalar boundaries");
        }
        if (node.polarity != "positive" && node.polarity != "negative" &&
            node.polarity != "unknown") issues.emplace_back("node polarity is not recognized");
        if (node.modality.empty()) issues.emplace_back("node modality must not be empty");
    }

    std::set<std::string, std::less<>> edge_ids;
    for (const auto& edge : graph.edges) {
        if (!valid_local_id(edge.id) || !edge_ids.insert(edge.id).second)
            issues.emplace_back("edge ids must be valid and unique within a candidate");
        if (!node_ids.contains(edge.source) || !node_ids.contains(edge.target))
            issues.emplace_back("edge endpoint does not refer to a node in this candidate");
        if (!valid_relation(edge.relation))
            issues.emplace_back("edge relation must be a namespaced xai relation");
    }
    return issues;
}

}  // namespace xai::perception
