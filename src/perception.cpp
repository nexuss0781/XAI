#include "xai/perception.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef XAI_BUILD_CODE_SHA256
#define XAI_BUILD_CODE_SHA256 "unconfigured-build"
#endif

namespace xai::perception {
namespace {
using Value = contracts::CanonicalValue;
using Object = Value::Object;
using Array = Value::Array;

[[nodiscard]] Value span_value(ByteSpan span) {
    return Object{{"begin_byte", Value{span.begin}}, {"end_byte", Value{span.end}}};
}

[[nodiscard]] Value graph_value(const CandidateGraph& graph) {
    Array nodes;
    nodes.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        Array spans;
        spans.reserve(node.source_spans.size());
        for (const auto span : node.source_spans) spans.push_back(span_value(span));
        Array cues;
        cues.reserve(node.cue_spans.size());
        for (const auto span : node.cue_spans) cues.push_back(span_value(span));
        nodes.emplace_back(Object{
            {"concept", Value{node.concept_label}},
            {"cue_spans", Value{std::move(cues)}},
            {"id", Value{node.id}},
            {"implicit", Value{node.implicit}},
            {"modality", Value{node.modality}},
            {"polarity", Value{node.polarity}},
            {"quoted", Value{node.quoted}},
            {"source_spans", Value{std::move(spans)}},
            {"type", Value{node.type}}});
    }
    Array edges;
    edges.reserve(graph.edges.size());
    for (const auto& edge : graph.edges) {
        edges.emplace_back(Object{{"id", Value{edge.id}}, {"relation", Value{edge.relation}},
                                  {"source", Value{edge.source}}, {"target", Value{edge.target}}});
    }
    return Object{{"edges", Value{std::move(edges)}},
                  {"nodes", Value{std::move(nodes)}},
                  {"speech_act", Value{graph.speech_act}},
                  {"vocabulary", Value{"xai.perception"}},
                  {"vocabulary_version", Value{1}}};
}

[[nodiscard]] Value candidate_value(const CandidateGraph& graph) {
    return Object{{"candidate_id", Value{graph.candidate_id}},
                  {"graph", graph_value(graph)},
                  {"rank", Value{graph.rank}}};
}

[[nodiscard]] Value unresolved_value(const UnresolvedItem& item) {
    Array spans;
    spans.reserve(item.source_spans.size());
    for (const auto span : item.source_spans) spans.push_back(span_value(span));
    return Object{{"kind", Value{item.kind}}, {"source_spans", Value{std::move(spans)}}};
}

[[nodiscard]] std::string build_id() {
    return "build:" + std::string(XAI_BUILD_CODE_SHA256);
}

[[nodiscard]] std::uint64_t configured_memory_estimate(const Limits& limits) noexcept {
    const auto input = static_cast<std::uint64_t>(limits.max_input_bytes);
    const auto tokens = static_cast<std::uint64_t>(limits.max_tokens);
    const auto candidates = static_cast<std::uint64_t>(limits.max_candidates);
    const auto nodes = static_cast<std::uint64_t>(limits.max_nodes_per_candidate);
    const auto edges = static_cast<std::uint64_t>(limits.max_edges_per_candidate);
    const auto output = static_cast<std::uint64_t>(limits.max_output_bytes);
    return input * 2U + tokens * (sizeof(SurfaceToken) + 32U) +
        candidates * (nodes * (sizeof(CandidateNode) + 128U) +
                      edges * (sizeof(CandidateEdge) + 128U) + 1024U) + output * 2U;
}

[[nodiscard]] std::uint64_t estimated_memory_use(
    std::size_t input_bytes, std::size_t token_count,
    const std::vector<CandidateGraph>& candidates, std::size_t serialized_bytes) noexcept {
    std::uint64_t estimate = static_cast<std::uint64_t>(input_bytes) * 2U +
        static_cast<std::uint64_t>(token_count) * (sizeof(SurfaceToken) + 32U) +
        static_cast<std::uint64_t>(serialized_bytes) * 2U;
    for (const auto& graph : candidates) {
        estimate += static_cast<std::uint64_t>(graph.nodes.size()) *
            (sizeof(CandidateNode) + 128U);
        estimate += static_cast<std::uint64_t>(graph.edges.size()) *
            (sizeof(CandidateEdge) + 128U);
        estimate += 1024U;
    }
    return estimate;
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, unsigned int shift) noexcept {
    return (value >> shift) | (value << (32U - shift));
}

[[nodiscard]] std::string sha256_hex(std::string_view input) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
    std::vector<unsigned char> message(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) message.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<unsigned char>((bit_length >> static_cast<unsigned int>(shift)) & 0xffU));

    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    for (std::size_t block = 0; block < message.size(); block += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t offset = block + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[offset]) << 24U) |
                (static_cast<std::uint32_t>(message[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(message[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(message[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto x = words[index - 15];
            const auto y = words[index - 2];
            const std::uint32_t small0 = rotate_right(x, 7U) ^ rotate_right(x, 18U) ^ (x >> 3U);
            const std::uint32_t small1 = rotate_right(y, 17U) ^ rotate_right(y, 19U) ^ (y >> 10U);
            words[index] = words[index - 16] + small0 + words[index - 7] + small1;
        }
        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t big1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + big1 + choose + constants[index] + words[index];
            const std::uint32_t big0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = big0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto word : state) output << std::setw(8) << word;
    return output.str();
}

[[nodiscard]] Value configuration_value(const Limits& limits, std::string_view backend_id) {
    return Object{{"backend", Value{std::string(backend_id)}},
                  {"max_candidates", Value{static_cast<std::uint64_t>(limits.max_candidates)}},
                  {"max_edges_per_candidate", Value{static_cast<std::uint64_t>(limits.max_edges_per_candidate)}},
                  {"max_input_bytes", Value{static_cast<std::uint64_t>(limits.max_input_bytes)}},
                  {"max_memory_bytes", Value{static_cast<std::uint64_t>(limits.max_memory_bytes)}},
                  {"max_nodes_per_candidate", Value{static_cast<std::uint64_t>(limits.max_nodes_per_candidate)}},
                  {"max_output_bytes", Value{static_cast<std::uint64_t>(limits.max_output_bytes)}},
                  {"max_tokens", Value{static_cast<std::uint64_t>(limits.max_tokens)}},
                  {"max_wall_time_ms", Value{limits.max_wall_time_ms}}};
}

[[nodiscard]] std::uint64_t fnv1a(std::string_view value) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] contracts::ResultId default_result_id(std::string_view run_id,
                                                     std::string_view input_result_id,
                                                     std::string_view observation_id) {
    std::string key;
    key.reserve(run_id.size() + input_result_id.size() + observation_id.size() + 2);
    key.append(run_id).push_back('\n');
    key.append(input_result_id).push_back('\n');
    key.append(observation_id);
    std::ostringstream output;
    output << "result:meaning-" << std::hex << std::setfill('0') << std::setw(16) << fnv1a(key);
    return contracts::ResultId{output.str()};
}

[[nodiscard]] bool utf8_boundary(std::string_view text, std::uint64_t offset) noexcept {
    if (offset > text.size()) return false;
    if (offset == 0 || offset == text.size()) return true;
    return (static_cast<unsigned char>(text[static_cast<std::size_t>(offset)]) & 0xc0U) != 0x80U;
}

[[nodiscard]] bool valid_span(ByteSpan span, std::string_view text) noexcept {
    return span.begin < span.end && span.end <= text.size() &&
        utf8_boundary(text, span.begin) && utf8_boundary(text, span.end);
}

[[nodiscard]] contracts::AnyIdentifier result_identifier(const contracts::ResultId& id) {
    return contracts::AnyIdentifier{id};
}

[[nodiscard]] contracts::RecordEnvelope failure_record(
    const Options& options, contracts::Status status, contracts::DiagnosticCode code,
    std::string message, const interaction::TextObservation* observation = nullptr) {
    contracts::RecordEnvelope record;
    record.schema_version = kMeaningCandidatesSchemaVersion;
    record.identity.schema_id = contracts::SchemaId{std::string(kMeaningCandidatesSchemaId)};
    record.identity.code_build_id = contracts::CodeBuildId{build_id()};
    record.identity.run_id = contracts::RunId{"run:perception"};
    record.identity.result_id = contracts::ResultId{"result:perception-failure"};
    if (observation != nullptr) {
        const auto& metadata = observation->metadata;
        if (metadata.run_id.valid()) record.identity.run_id = metadata.run_id;
        if (metadata.observation_id.valid()) record.identity.observation_id = metadata.observation_id;
        if (metadata.source_id.valid()) record.identity.source_id = metadata.source_id;
        if (contracts::to_string(metadata.partition) != "invalid_partition") {
            record.identity.partition = metadata.partition;
            if (metadata.partition_id && metadata.partition_id->valid())
                record.identity.partition_id = metadata.partition_id;
        }
        record.identity.result_id = options.output_result_id && options.output_result_id->valid()
            ? *options.output_result_id
            : default_result_id(metadata.run_id.value(), metadata.result_id.value(),
                                metadata.observation_id.value());
        if (metadata.result_id.valid())
            record.lineage.push_back({result_identifier(metadata.result_id),
                                      contracts::LineageRelation::derived_from});
        if (metadata.observation_id.valid())
            record.lineage.push_back({contracts::AnyIdentifier{metadata.observation_id},
                                      contracts::LineageRelation::observes});
        if (metadata.source_id.valid())
            record.lineage.push_back({contracts::AnyIdentifier{metadata.source_id},
                                      contracts::LineageRelation::cites});
        if (metadata.code_build_id.valid())
            record.lineage.push_back({contracts::AnyIdentifier{metadata.code_build_id},
                                      contracts::LineageRelation::produced_by});
    }
    record.lineage.push_back({contracts::AnyIdentifier{record.identity.code_build_id},
                              contracts::LineageRelation::produced_by});
    record.status = status;
    record.exactness = contracts::Exactness::not_applicable;
    record.budget.operations = static_cast<std::uint64_t>(options.limits.max_tokens) +
        static_cast<std::uint64_t>(options.limits.max_candidates) *
        static_cast<std::uint64_t>(options.limits.max_nodes_per_candidate +
                                   options.limits.max_edges_per_candidate);
    record.budget.memory_bytes = static_cast<std::uint64_t>(options.limits.max_memory_bytes);
    record.errors.push_back({code, std::move(message), std::nullopt});
    record.reproducibility.algorithm = "xai.perception.engine-v1";
    return record;
}

[[nodiscard]] Value build_result(const interaction::TextObservation& observation,
                                 const LanguageEvidence& language,
                                 const BackendResult& backend_result,
                                 std::size_t requested_candidates,
                                 std::string_view backend_id,
                                 const Limits& limits) {
    Array hypotheses;
    for (const auto& hypothesis : language.hypotheses) {
        hypotheses.emplace_back(Object{{"basis", Value{hypothesis.basis}},
                                       {"rank", Value{hypothesis.rank}},
                                       {"tag", Value{hypothesis.tag}}});
    }
    Array candidates;
    candidates.reserve(backend_result.candidates.size());
    for (const auto& graph : backend_result.candidates)
        candidates.push_back(candidate_value(graph));
    Array unresolved;
    unresolved.reserve(backend_result.unresolved.size());
    for (const auto& item : backend_result.unresolved)
        unresolved.push_back(unresolved_value(item));
    const Value configuration = configuration_value(limits, backend_id);
    const std::string configuration_hash = sha256_hex(contracts::encode_canonical_value(configuration));

    const auto& metadata = observation.metadata;
    return Object{
        {"ambiguity", Value{Object{
            {"exhaustive", Value{false}},
            {"requested_candidates", Value{static_cast<std::uint64_t>(requested_candidates)}},
            {"returned_candidates", Value{static_cast<std::uint64_t>(backend_result.candidates.size())}},
            {"set_kind", Value{"bounded_n_best"}},
            {"truncated", Value{backend_result.truncated}},
            {"unresolved", Value{std::move(unresolved)}}}}},
        {"candidates", Value{std::move(candidates)}},
        {"configuration", configuration},
        {"input", Value{Object{
            {"observation_id", Value{metadata.observation_id.value()}},
            {"result_id", Value{metadata.result_id.value()}},
            {"schema_id", Value{std::string(interaction::kTextInputSchemaId)}},
            {"schema_version", Value{interaction::kTextInputSchemaVersion}},
            {"source_id", Value{metadata.source_id.value()}}}}},
        {"language", Value{Object{
            {"declared_locale", Value{language.declared_locale}},
            {"hypotheses", Value{std::move(hypotheses)}},
            {"identification", Value{"hint_only_no_detector"}},
            {"unresolved", Value{language.unresolved}}}}},
        {"offset_convention", Value{"zero_based_half_open_utf8_bytes_into_original_unnormalized_content"}},
        {"reproducibility", Value{Object{
            {"algorithm", Value{std::string(backend_id)}},
            {"configuration_sha256", Value{configuration_hash}},
            {"model", Value{nullptr}},
            {"tokenizer", Value{"xai.perception.surface-tokenizer-v1"}}}}},
        {"source_content_included", Value{false}}};
}

}  // namespace

Engine::Engine(Options options, std::shared_ptr<const InferenceBackend> backend)
    : options_(std::move(options)), backend_(std::move(backend)) {
    const auto& limits = options_.limits;
    if (!backend_) throw std::invalid_argument("perception backend must not be null");
    if (limits.max_input_bytes == 0 ||
        limits.max_input_bytes > interaction::kHardMaxContentBytes ||
        limits.max_tokens == 0 || limits.max_tokens > 1'000'000 ||
        limits.max_candidates == 0 || limits.max_candidates > 64 ||
        limits.max_nodes_per_candidate == 0 || limits.max_nodes_per_candidate > 4096 ||
        limits.max_edges_per_candidate == 0 || limits.max_edges_per_candidate > 8192 || limits.max_output_bytes == 0 ||
        limits.max_output_bytes > 16U * 1024U * 1024U || limits.max_wall_time_ms == 0 ||
        limits.max_wall_time_ms > 600'000 || limits.max_memory_bytes < 1024 ||
        limits.max_memory_bytes > 128U * 1024U * 1024U)
        throw std::invalid_argument("perception limits are outside the supported safe ranges");
    if (configured_memory_estimate(limits) > limits.max_memory_bytes)
        throw std::invalid_argument("configured perception limits exceed the memory ceiling");
    if (options_.output_result_id && !options_.output_result_id->valid())
        throw std::invalid_argument("output result id is invalid");
}

contracts::RecordEnvelope Engine::analyze(
    const interaction::TextObservation& observation) const {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(options_.limits.max_wall_time_ms);
    const InputValidator input_validator{options_.limits};
    const auto checked = input_validator.validate(observation);
    if (checked.status != contracts::Status::success)
        return failure_record(options_, checked.status,
            checked.status == contracts::Status::resource_limit ? contracts::DiagnosticCode::resource_limit :
                contracts::DiagnosticCode::invalid_schema,
            checked.diagnostic, &observation);

    const auto language_tag = backend_->language_hint_for_locale(observation.metadata.locale);
    if (!language_tag)
        return failure_record(options_, contracts::Status::unsupported_input,
            contracts::DiagnosticCode::unsupported_input,
            "the configured backend does not support this declared-locale hint",
            &observation);

    const SurfaceAnalyzer surface_analyzer{options_.limits};
    const auto surface = surface_analyzer.analyze(observation.content);
    if (surface.status != contracts::Status::success || !surface.analysis)
        return failure_record(options_, surface.status,
            surface.status == contracts::Status::resource_limit ? contracts::DiagnosticCode::resource_limit :
                contracts::DiagnosticCode::malformed_input,
            surface.diagnostic, &observation);
    if (std::chrono::steady_clock::now() >= deadline)
        return failure_record(options_, contracts::Status::timeout,
            contracts::DiagnosticCode::timeout, "perception wall-time budget exceeded", &observation);

    const LanguageEvidence language{
        observation.metadata.locale,
        {LanguageHypothesis{std::string(*language_tag), 1, "declared_locale_hint"}}, true};
    const auto backend_result = backend_->generate(observation, *surface.analysis,
        options_.limits.max_candidates, options_.limits, deadline);
    if (backend_result.status != contracts::Status::success)
        return failure_record(options_, backend_result.status,
            backend_result.status == contracts::Status::resource_limit ? contracts::DiagnosticCode::resource_limit :
                backend_result.status == contracts::Status::timeout ? contracts::DiagnosticCode::timeout :
                backend_result.status == contracts::Status::unsupported_input ? contracts::DiagnosticCode::unsupported_input :
                contracts::DiagnosticCode::abstention,
            backend_result.diagnostic.empty() ? "backend did not produce a candidate meaning" :
                backend_result.diagnostic, &observation);
    if (backend_result.candidates.empty())
        return failure_record(options_, contracts::Status::abstention,
            contracts::DiagnosticCode::abstention,
            "backend returned no defensible candidate meaning", &observation);
    if (backend_result.candidates.size() > options_.limits.max_candidates)
        return failure_record(options_, contracts::Status::resource_limit,
            contracts::DiagnosticCode::resource_limit,
            "backend exceeded the configured candidate count limit", &observation);

    const GraphValidator graph_validator;
    for (const auto& graph : backend_result.candidates) {
        const auto issues = graph_validator.validate(graph, observation.content, options_.limits);
        if (!issues.empty()) {
            const bool graph_limit = issues.front().starts_with(
                "candidate graph exceeds the configured ");
            return failure_record(options_,
                graph_limit ? contracts::Status::resource_limit : contracts::Status::invalid_schema,
                graph_limit ? contracts::DiagnosticCode::resource_limit :
                    contracts::DiagnosticCode::invalid_record,
                "candidate graph validation failed: " + issues.front(), &observation);
        }
    }
    for (const auto& item : backend_result.unresolved) {
        for (const auto span : item.source_spans) {
            if (!valid_span(span, observation.content))
                return failure_record(options_, contracts::Status::invalid_schema,
                    contracts::DiagnosticCode::invalid_record,
                    "unresolved-item span is out of range or not on UTF-8 scalar boundaries", &observation);
        }
    }
    if (std::chrono::steady_clock::now() >= deadline)
        return failure_record(options_, contracts::Status::timeout,
            contracts::DiagnosticCode::timeout, "perception wall-time budget exceeded", &observation);

    contracts::RecordEnvelope record;
    const auto& metadata = observation.metadata;
    record.schema_version = kMeaningCandidatesSchemaVersion;
    record.identity.run_id = metadata.run_id;
    record.identity.result_id = options_.output_result_id.value_or(
        default_result_id(metadata.run_id.value(), metadata.result_id.value(),
                          metadata.observation_id.value()));
    record.identity.schema_id = contracts::SchemaId{std::string(kMeaningCandidatesSchemaId)};
    record.identity.code_build_id = contracts::CodeBuildId{build_id()};
    record.identity.partition = metadata.partition;
    record.identity.partition_id = metadata.partition_id;
    record.identity.observation_id = metadata.observation_id;
    record.identity.source_id = metadata.source_id;
    record.status = contracts::Status::approximate;
    record.result = build_result(observation, language, backend_result,
        options_.limits.max_candidates, backend_->identifier(), options_.limits);
    record.assumptions = {
        "Only this text observation was used; prior conversation, memory, retrieval, and external knowledge were not consulted.",
        "Candidate meanings are bounded rule-generated hypotheses, not facts or calibrated probabilities."};
    record.lineage.push_back({result_identifier(metadata.result_id), contracts::LineageRelation::derived_from});
    record.lineage.push_back({contracts::AnyIdentifier{metadata.observation_id}, contracts::LineageRelation::observes});
    record.lineage.push_back({contracts::AnyIdentifier{metadata.source_id}, contracts::LineageRelation::cites});
    record.lineage.push_back({contracts::AnyIdentifier{metadata.code_build_id}, contracts::LineageRelation::produced_by});
    record.lineage.push_back({contracts::AnyIdentifier{record.identity.code_build_id}, contracts::LineageRelation::produced_by});
    record.exactness = contracts::Exactness::approximate;
    record.budget.operations = static_cast<std::uint64_t>(options_.limits.max_tokens) +
        static_cast<std::uint64_t>(options_.limits.max_candidates) *
        static_cast<std::uint64_t>(options_.limits.max_nodes_per_candidate +
                                   options_.limits.max_edges_per_candidate);
    record.budget.memory_bytes = static_cast<std::uint64_t>(options_.limits.max_memory_bytes);
    record.consumed.operations = static_cast<std::uint64_t>(surface.analysis->tokens.size()) +
        backend_result.operations;
    for (const auto& graph : backend_result.candidates)
        record.consumed.operations += static_cast<std::uint64_t>(graph.nodes.size() + graph.edges.size());
    record.warnings.push_back({contracts::DiagnosticCode::approximation,
        "Backend-generated hypotheses are limited in coverage and may omit plausible readings; rank is ordinal only and is not a calibrated probability.",
        std::nullopt});
    record.reproducibility.algorithm = std::string(backend_->identifier());
    record.reproducibility.artifact_sha256.emplace("configuration",
        sha256_hex(contracts::encode_canonical_value(
            configuration_value(options_.limits, backend_->identifier()))));
    if (record.consumed.operations > *record.budget.operations)
        return failure_record(options_, contracts::Status::resource_limit,
            contracts::DiagnosticCode::resource_limit,
            "candidate generation exceeded the configured operation budget", &observation);

    try {
        const std::string encoded = contracts::encode_record(record);
        if (encoded.size() > options_.limits.max_output_bytes)
            return failure_record(options_, contracts::Status::resource_limit,
                contracts::DiagnosticCode::resource_limit,
                "serialized candidate record exceeds the configured output byte limit", &observation);
        record.consumed.memory_bytes = estimated_memory_use(observation.content.size(),
            surface.analysis->tokens.size(), backend_result.candidates, encoded.size());
        if (record.consumed.memory_bytes > *record.budget.memory_bytes)
            return failure_record(options_, contracts::Status::resource_limit,
                contracts::DiagnosticCode::resource_limit,
                "candidate generation exceeds the configured memory ceiling", &observation);
        if (contracts::encode_record(record).size() > options_.limits.max_output_bytes)
            return failure_record(options_, contracts::Status::resource_limit,
                contracts::DiagnosticCode::resource_limit,
                "serialized candidate record exceeds the configured output byte limit", &observation);
    } catch (const std::exception&) {
        return failure_record(options_, contracts::Status::invalid_schema,
            contracts::DiagnosticCode::invalid_record,
            "candidate record failed shared envelope validation", &observation);
    }
    return record;
}

contracts::RecordEnvelope Engine::analyze(const contracts::RecordEnvelope& input_record) const {
    const InputValidator validator{options_.limits};
    const auto checked = validator.decode_and_validate(input_record);
    if (checked.status != contracts::Status::success || !checked.observation)
        return failure_record(options_, checked.status,
            checked.status == contracts::Status::resource_limit ? contracts::DiagnosticCode::resource_limit :
                contracts::DiagnosticCode::invalid_schema,
            checked.diagnostic);
    return analyze(*checked.observation);
}

contracts::RecordEnvelope Engine::analyze_record_json(std::string_view canonical_json) const {
    const InputValidator validator{options_.limits};
    const auto checked = validator.decode_and_validate(canonical_json);
    if (checked.status != contracts::Status::success || !checked.observation)
        return failure_record(options_, checked.status,
            checked.status == contracts::Status::resource_limit ? contracts::DiagnosticCode::resource_limit :
                contracts::DiagnosticCode::invalid_schema,
            checked.diagnostic);
    return analyze(*checked.observation);
}

std::string Engine::serialize(const interaction::TextObservation& observation) const {
    return contracts::encode_record(analyze(observation));
}

std::string Engine::serialize_record_json(std::string_view canonical_json) const {
    return contracts::encode_record(analyze_record_json(canonical_json));
}

}  // namespace xai::perception
