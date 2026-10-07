#include "xai/perception.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace xai::perception {
namespace {

[[nodiscard]] ByteSpan combine_span(const SurfaceAnalysis& surface,
                                    std::size_t begin, std::size_t end) {
    return {surface.tokens.at(begin).span.begin, surface.tokens.at(end).span.end};
}

[[nodiscard]] bool is_word(const SurfaceToken& token) noexcept {
    return !token.punctuation;
}

[[nodiscard]] bool is_clause_boundary(const SurfaceToken& token) noexcept {
    return token.punctuation && (token.folded == "." || token.folded == "!" ||
                                  token.folded == "?" || token.folded == ";");
}

[[nodiscard]] std::string concept_for(std::string_view verb) {
    if (verb == "saw" || verb == "see" || verb == "sees") return "see";
    if (verb == "liked" || verb == "likes" || verb == "like") return "like";
    if (verb == "loved" || verb == "loves" || verb == "love") return "love";
    if (verb == "ate" || verb == "eats" || verb == "eat") return "eat";
    if (verb == "had" || verb == "has" || verb == "have") return "have";
    if (verb == "wanted" || verb == "wants" || verb == "want") return "want";
    if (verb == "went" || verb == "goes" || verb == "go") return "go";
    if (verb == "arrived" || verb == "arrives" || verb == "arrive") return "arrive";
    if (verb == "said" || verb == "says" || verb == "say") return "say";
    if (verb == "believed" || verb == "believes" || verb == "believe") return "believe";
    return {};
}

[[nodiscard]] bool is_supported_verb(std::string_view verb) {
    return !concept_for(verb).empty();
}

[[nodiscard]] bool is_modal(std::string_view token) noexcept {
    constexpr std::array<std::string_view, 8> values{
        "may", "might", "could", "would", "can", "must", "should", "will"};
    return std::find(values.begin(), values.end(), token) != values.end();
}

[[nodiscard]] bool is_negative(std::string_view token) noexcept {
    return token == "not" || token == "never" || token == "no" || token == "n't";
}

[[nodiscard]] bool is_subject_skip_word(std::string_view token) noexcept {
    return is_negative(token) || is_modal(token) || token == "do" || token == "does" ||
        token == "did" || token == "be" || token == "am" || token == "is" ||
        token == "are" || token == "was" || token == "were";
}

[[nodiscard]] std::optional<std::size_t> find_subject(const std::vector<SurfaceToken>& tokens,
                                                       std::size_t verb_index) {
    if (verb_index == 0) return std::nullopt;
    std::size_t index = verb_index;
    while (index > 0) {
        --index;
        if (!is_word(tokens[index])) return std::nullopt;
        if (!is_subject_skip_word(tokens[index].folded)) return index;
    }
    return std::nullopt;
}

[[nodiscard]] CandidateGraph make_graph(const SurfaceAnalysis& surface,
                                        std::size_t verb_index,
                                        std::size_t subject_index,
                                        std::size_t object_begin,
                                        std::size_t object_end,
                                        std::optional<std::pair<std::size_t, std::size_t>> with_phrase,
                                        bool instrument_reading,
                                        std::uint32_t rank,
                                        std::string_view concept_name) {
    CandidateGraph graph;
    graph.candidate_id = "candidate-" + std::to_string(rank);
    graph.rank = rank;
    graph.speech_act = !surface.question_mark_spans.empty() ? "question" :
        (surface.contains_quote ? "quoted-content" : "assertion");

    CandidateNode event;
    event.id = "n1";
    event.type = "xai:event";
    event.concept_label = std::string(concept_name);
    event.source_spans.push_back(surface.tokens[verb_index].span);
    event.quoted = surface.contains_quote;
    for (std::size_t index = subject_index + 1; index < verb_index; ++index) {
        const auto& token = surface.tokens[index];
        if (is_negative(token.folded)) {
            event.polarity = "negative";
            event.cue_spans.push_back(token.span);
        } else if (is_modal(token.folded)) {
            event.modality = token.folded;
            event.cue_spans.push_back(token.span);
        }
    }
    graph.nodes.push_back(std::move(event));

    CandidateNode subject;
    subject.id = "n2";
    subject.type = "xai:surface-mention";
    subject.concept_label = "surface-mention";
    subject.source_spans.push_back(surface.tokens[subject_index].span);
    graph.nodes.push_back(std::move(subject));

    CandidateNode object;
    object.id = "n3";
    object.type = "xai:surface-mention";
    object.concept_label = "surface-mention";
    object.source_spans.push_back(combine_span(surface, object_begin, object_end));
    graph.nodes.push_back(std::move(object));

    graph.edges.push_back({"e1", "n1", "n2", "xai:agent"});
    graph.edges.push_back({"e2", "n1", "n3", "xai:patient"});

    if (with_phrase) {
        CandidateNode adjunct;
        adjunct.id = "n4";
        adjunct.type = "xai:surface-mention";
        adjunct.concept_label = "surface-mention";
        adjunct.source_spans.push_back(combine_span(surface, with_phrase->first,
                                                    with_phrase->second));
        graph.nodes.push_back(std::move(adjunct));
        if (instrument_reading)
            graph.edges.push_back({"e3", "n1", "n4", "xai:instrument"});
        else
            graph.edges.push_back({"e3", "n3", "n4", "xai:has-associated-modifier"});
    }
    return graph;
}

[[nodiscard]] bool expired(std::chrono::steady_clock::time_point deadline) noexcept {
    return std::chrono::steady_clock::now() >= deadline;
}

[[nodiscard]] bool is_english_hint(std::string_view locale) noexcept {
    if (locale.size() < 2) return false;
    const auto lower = [](char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
    };
    return lower(locale[0]) == 'e' && lower(locale[1]) == 'n' &&
        (locale.size() == 2 || locale[2] == '-');
}

}  // namespace

std::string_view EnglishRuleBackend::identifier() const noexcept {
    return kEnglishRulesBackendId;
}

std::optional<std::string_view> EnglishRuleBackend::language_hint_for_locale(
    std::string_view declared_locale) const noexcept {
    if (!is_english_hint(declared_locale)) return std::nullopt;
    return std::string_view{"en"};
}

BackendResult EnglishRuleBackend::generate(
    const interaction::TextObservation& observation,
    const SurfaceAnalysis& surface,
    std::size_t candidate_limit,
    const Limits& limits,
    std::chrono::steady_clock::time_point deadline) const {
    (void)observation;
    BackendResult result;
    if (candidate_limit == 0) {
        result.status = contracts::Status::resource_limit;
        result.diagnostic = "candidate budget must be positive";
        return result;
    }
    if (expired(deadline)) {
        result.status = contracts::Status::timeout;
        result.diagnostic = "perception deadline exceeded before hypothesis generation";
        return result;
    }

    const auto& tokens = surface.tokens;
    result.operations = static_cast<std::uint64_t>(tokens.size());
    std::optional<std::size_t> verb_index;
    std::optional<std::size_t> with_index;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (expired(deadline)) {
            result.status = contracts::Status::timeout;
            result.diagnostic = "perception deadline exceeded during hypothesis generation";
            return result;
        }
        if (!is_word(tokens[index])) continue;
        if (!verb_index && is_supported_verb(tokens[index].folded))
            verb_index = index;
        if (verb_index && tokens[index].folded == "with") {
            with_index = index;
            break;
        }
    }

    std::vector<CandidateGraph> proposed;
    std::optional<UnresolvedItem> attachment_ambiguity;
    if (verb_index && tokens[*verb_index].folded == "saw" && with_index &&
        *verb_index > 0 && *with_index > *verb_index + 1) {
        const auto subject_index = find_subject(tokens, *verb_index);
        std::size_t phrase_end = *with_index + 1;
        while (phrase_end < tokens.size() && !is_clause_boundary(tokens[phrase_end])) {
            if (is_word(tokens[phrase_end])) ++phrase_end;
            else if (tokens[phrase_end].punctuation && tokens[phrase_end].folded == ",") break;
            else ++phrase_end;
        }
        if (phrase_end > *with_index + 1 && subject_index) {
            const std::size_t last = phrase_end - 1;
            const auto meaning = concept_for(tokens[*verb_index].folded);
            proposed.push_back(make_graph(surface, *verb_index, *subject_index,
                *verb_index + 1, *with_index - 1,
                std::pair<std::size_t, std::size_t>{*with_index, last}, true, 1, meaning));
            proposed.push_back(make_graph(surface, *verb_index, *subject_index,
                *verb_index + 1, *with_index - 1,
                std::pair<std::size_t, std::size_t>{*with_index, last}, false, 2, meaning));
            attachment_ambiguity = UnresolvedItem{
                "prepositional_attachment",
                {combine_span(surface, *with_index, last)}};
        }
    }

    if (proposed.empty() && verb_index) {
        const auto subject_index = find_subject(tokens, *verb_index);
        std::size_t object_end = *verb_index + 1;
        while (object_end < tokens.size() && !is_clause_boundary(tokens[object_end]))
            ++object_end;
        if (subject_index && object_end > *verb_index + 1 && is_word(tokens[*verb_index + 1])) {
            --object_end;
            proposed.push_back(make_graph(surface, *verb_index, *subject_index,
                *verb_index + 1, object_end, std::nullopt, false, 1,
                concept_for(tokens[*verb_index].folded)));
        }
    }

    if (proposed.empty()) {
        result.status = contracts::Status::abstention;
        result.diagnostic = "the English rules backend found no supported clause pattern";
        result.unresolved.push_back({"clause_structure", {}});
        return result;
    }

    result.truncated = proposed.size() > candidate_limit;
    if (proposed.size() > candidate_limit) proposed.resize(candidate_limit);
    for (std::size_t index = 0; index < proposed.size(); ++index)
        proposed[index].rank = static_cast<std::uint32_t>(index + 1);
    result.candidates = std::move(proposed);
    if (attachment_ambiguity) result.unresolved.push_back(std::move(*attachment_ambiguity));
    if (surface.contains_quote) result.unresolved.push_back({"quotation_scope", surface.quote_spans});
    result.unresolved.push_back({"language_identification", {}});
    if (result.truncated) result.unresolved.push_back({"candidate_limit", {}});

    if (result.candidates.size() > limits.max_candidates) {
        result.status = contracts::Status::resource_limit;
        result.diagnostic = "backend exceeded the configured candidate limit";
        result.candidates.clear();
    }
    return result;
}

}  // namespace xai::perception
