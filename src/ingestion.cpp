#include "xai/ingestion.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace xai::ingestion {
namespace {
using Object = contracts::CanonicalValue::Object;
using Array = contracts::CanonicalValue::Array;
using Value = contracts::CanonicalValue;
constexpr std::size_t kHardMaxRationalBits = 8192;
constexpr std::size_t kHardMaxRationalDigits = 2470;

[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument(message); }

[[nodiscard]] const Object& as_object(const Value& value) {
    const auto* result = std::get_if<Object>(&value.data);
    if (!result) fail("expected JSON object");
    return *result;
}
[[nodiscard]] const Array& as_array(const Value& value) {
    const auto* result = std::get_if<Array>(&value.data);
    if (!result) fail("expected JSON array");
    return *result;
}
[[nodiscard]] const std::string& as_string(const Value& value) {
    const auto* result = std::get_if<std::string>(&value.data);
    if (!result) fail("expected JSON string");
    return *result;
}
[[nodiscard]] std::uint64_t as_u64(const Value& value) {
    if (const auto* result = std::get_if<std::uint64_t>(&value.data)) return *result;
    if (const auto* result = std::get_if<std::int64_t>(&value.data); result && *result >= 0)
        return static_cast<std::uint64_t>(*result);
    fail("expected non-negative integer");
}
[[nodiscard]] const Value& member(const Object& object, std::string_view name) {
    const auto found = object.find(name);
    if (found == object.end()) fail("missing field: " + std::string(name));
    return found->second;
}
void check_keys(const Object& object, std::initializer_list<std::string_view> names) {
    if (object.size() != names.size()) fail("object has missing or unknown fields");
    for (const auto name : names)
        if (!object.contains(name)) fail("object has missing or unknown fields");
}
[[nodiscard]] bool is_null(const Value& value) {
    return std::holds_alternative<std::nullptr_t>(value.data);
}

template <typename Id>
[[nodiscard]] Id parse_id(const Value& value) { return Id{as_string(value)}; }

template <typename Id>
[[nodiscard]] Value id_array_value(const std::vector<Id>& identifiers) {
    Array result;
    result.reserve(identifiers.size());
    for (const auto& id : identifiers) result.emplace_back(id.value());
    return Value{std::move(result)};
}

[[nodiscard]] Value rational_value(const Rational& input) {
    Rational value = input;
    value.canonicalize();
    return Value{Object{{"$rational", Value{Array{
        value.get_num().get_str(), value.get_den().get_str()}}}}};
}
[[nodiscard]] mpz_class parse_canonical_integer(const std::string& text, bool positive_only) {
    if (text.empty() || text.size() > kHardMaxRationalDigits + 1)
        fail("rational integer is empty or exceeds the hard digit ceiling");
    mpz_class result;
    if (mpz_set_str(result.get_mpz_t(), text.c_str(), 10) != 0 || result.get_str() != text)
        fail("noncanonical rational integer");
    if (positive_only && result <= 0) fail("rational denominator must be positive");
    return result;
}
[[nodiscard]] bool rational_within_bits(const Rational& value, std::size_t maximum_bits) {
    return mpz_sizeinbase(value.get_num().get_mpz_t(), 2) <= maximum_bits &&
           mpz_sizeinbase(value.get_den().get_mpz_t(), 2) <= maximum_bits;
}
[[nodiscard]] Rational parse_rational(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"$rational"});
    const auto& pair = as_array(member(object, "$rational"));
    if (pair.size() != 2) fail("rational must contain numerator and denominator");
    const auto numerator = parse_canonical_integer(as_string(pair[0]), false);
    const auto denominator = parse_canonical_integer(as_string(pair[1]), true);
    Rational result{numerator, denominator};
    result.canonicalize();
    if (result.get_num() != numerator || result.get_den() != denominator)
        fail("rational pair is not reduced");
    if (!rational_within_bits(result, kHardMaxRationalBits))
        fail("rational exceeds the hard exact-arithmetic bit ceiling");
    return result;
}

[[nodiscard]] bool valid_text(std::string_view text, std::size_t maximum) {
    if (text.empty() || text.size() > maximum) return false;
    for (const unsigned char ch : text)
        if (ch < 0x20U || ch == 0x7fU) return false;
    return true;
}
[[nodiscard]] unsigned parse_fixed_digits(std::string_view text) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        fail("invalid timestamp field");
    return result;
}
[[nodiscard]] bool valid_timestamp(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != 'Z') return false;
    try {
        const unsigned year_value = parse_fixed_digits(text.substr(0, 4));
        const unsigned month_value = parse_fixed_digits(text.substr(5, 2));
        const unsigned day_value = parse_fixed_digits(text.substr(8, 2));
        const unsigned hour_value = parse_fixed_digits(text.substr(11, 2));
        const unsigned minute_value = parse_fixed_digits(text.substr(14, 2));
        const unsigned second_value = parse_fixed_digits(text.substr(17, 2));
        const std::chrono::year_month_day date{
            std::chrono::year{static_cast<int>(year_value)},
            std::chrono::month{month_value}, std::chrono::day{day_value}};
        return year_value >= 1 && date.ok() && hour_value <= 23 && minute_value <= 59 && second_value <= 60;
    } catch (const std::exception&) {
        return false;
    }
}

[[nodiscard]] Value identity_reference_value(const IdentityReference& subject) {
    return Value{Object{
        {"candidate_entity_ids", id_array_value(subject.candidate_entity_ids)},
        {"entity_id", subject.entity_id ? Value{subject.entity_id->value()} : Value{nullptr}},
        {"resolution", Value{std::string(to_string(subject.resolution))}}}};
}
[[nodiscard]] Value fact_value(const FactDefinition& fact) {
    return Value{Object{{"fact_id", Value{fact.fact_id.value()}},
        {"predicate", Value{fact.predicate}}, {"subject", identity_reference_value(fact.subject)}}};
}
[[nodiscard]] Value prior_value(const LiteralPrior& prior) {
    return Value{Object{{"fact_id", Value{prior.fact_id.value()}},
        {"false_weight", rational_value(prior.false_weight)},
        {"true_weight", rational_value(prior.true_weight)}}};
}
[[nodiscard]] Value evidence_value(const EvidenceRecord& evidence) {
    Array dependencies;
    dependencies.reserve(evidence.dependencies.size());
    for (const auto& dependency : evidence.dependencies) {
        dependencies.emplace_back(Object{{"evidence_id", Value{dependency.evidence_id.value()}},
            {"relation", Value{std::string(to_string(dependency.relation))}}});
    }
    Array likelihood;
    likelihood.reserve(evidence.likelihood.size());
    for (const auto& weight : evidence.likelihood) likelihood.push_back(rational_value(weight));
    return Value{Object{
        {"dependencies", Value{std::move(dependencies)}},
        {"dependence", Value{std::string(to_string(evidence.dependence))}},
        {"evidence_id", Value{evidence.evidence_id.value()}},
        {"extractor_version", Value{evidence.extractor_version}},
        {"likelihood", Value{std::move(likelihood)}},
        {"observed_at_utc", Value{evidence.observed_at_utc}},
        {"original_observation_ref", Value{evidence.original_observation_ref}},
        {"partition", Value{std::string(contracts::to_string(evidence.partition))}},
        {"scope", id_array_value(evidence.scope)},
        {"semantics", Value{std::string(to_string(evidence.semantics))}},
        {"source_id", Value{evidence.source_id.value()}}}};
}
[[nodiscard]] Value limits_value(const ResourceLimits& limits) {
    return Value{Object{{"max_evidence", Value{static_cast<std::uint64_t>(limits.max_evidence)}},
        {"max_factor_entries", Value{limits.max_factor_entries}},
        {"max_facts", Value{static_cast<std::uint64_t>(limits.max_facts)}},
        {"max_operations", Value{limits.max_operations}},
        {"max_rational_bits", Value{static_cast<std::uint64_t>(limits.max_rational_bits)}},
        {"max_worlds", Value{limits.max_worlds}}}};
}

[[nodiscard]] contracts::DataPartition parse_partition(std::string_view text) {
    for (const auto value : {contracts::DataPartition::training, contracts::DataPartition::development,
            contracts::DataPartition::calibration, contracts::DataPartition::final_test,
            contracts::DataPartition::streaming, contracts::DataPartition::not_applicable})
        if (contracts::to_string(value) == text) return value;
    fail("unknown data partition");
}
[[nodiscard]] IdentityResolution parse_identity_resolution(std::string_view text) {
    for (const auto value : {IdentityResolution::resolved, IdentityResolution::unresolved})
        if (to_string(value) == text) return value;
    fail("unknown identity-resolution state");
}
[[nodiscard]] EvidenceSemantics parse_evidence_semantics(std::string_view text) {
    for (const auto value : {EvidenceSemantics::likelihood, EvidenceSemantics::hard_constraint})
        if (to_string(value) == text) return value;
    fail("unknown evidence semantics");
}
[[nodiscard]] DependenceClass parse_dependence(std::string_view text) {
    for (const auto value : {DependenceClass::independent, DependenceClass::model_factor,
            DependenceClass::exact_duplicate, DependenceClass::correlated, DependenceClass::unknown})
        if (to_string(value) == text) return value;
    fail("unknown dependence classification");
}
[[nodiscard]] DependencyRelation parse_dependency_relation(std::string_view text) {
    for (const auto value : {DependencyRelation::exact_duplicate, DependencyRelation::correlated,
            DependencyRelation::unknown})
        if (to_string(value) == text) return value;
    fail("unknown dependency relation");
}
[[nodiscard]] WorldSemantics parse_world_semantics(std::string_view text) {
    if (text == to_string(WorldSemantics::open_world)) return WorldSemantics::open_world;
    fail("unknown world semantics");
}

[[nodiscard]] IdentityReference parse_identity_reference(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"candidate_entity_ids", "entity_id", "resolution"});
    IdentityReference subject;
    subject.resolution = parse_identity_resolution(as_string(member(object, "resolution")));
    const auto& entity = member(object, "entity_id");
    if (!is_null(entity)) subject.entity_id = parse_id<contracts::EntityId>(entity);
    for (const auto& candidate : as_array(member(object, "candidate_entity_ids")))
        subject.candidate_entity_ids.push_back(parse_id<contracts::EntityId>(candidate));
    return subject;
}
[[nodiscard]] FactDefinition parse_fact(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"fact_id", "predicate", "subject"});
    return {parse_id<contracts::FactId>(member(object, "fact_id")),
        as_string(member(object, "predicate")), parse_identity_reference(member(object, "subject"))};
}
[[nodiscard]] LiteralPrior parse_prior(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"fact_id", "false_weight", "true_weight"});
    return {parse_id<contracts::FactId>(member(object, "fact_id")),
        parse_rational(member(object, "false_weight")), parse_rational(member(object, "true_weight"))};
}
[[nodiscard]] EvidenceRecord parse_evidence(const Value& value) {
    const auto& object = as_object(value);
    check_keys(object, {"dependencies", "dependence", "evidence_id", "extractor_version",
        "likelihood", "observed_at_utc", "original_observation_ref", "partition", "scope",
        "semantics", "source_id"});
    EvidenceRecord result;
    result.evidence_id = parse_id<contracts::EvidenceId>(member(object, "evidence_id"));
    result.source_id = parse_id<contracts::SourceId>(member(object, "source_id"));
    result.observed_at_utc = as_string(member(object, "observed_at_utc"));
    result.original_observation_ref = as_string(member(object, "original_observation_ref"));
    result.extractor_version = as_string(member(object, "extractor_version"));
    result.partition = parse_partition(as_string(member(object, "partition")));
    for (const auto& fact : as_array(member(object, "scope")))
        result.scope.push_back(parse_id<contracts::FactId>(fact));
    result.semantics = parse_evidence_semantics(as_string(member(object, "semantics")));
    for (const auto& likelihood : as_array(member(object, "likelihood")))
        result.likelihood.push_back(parse_rational(likelihood));
    result.dependence = parse_dependence(as_string(member(object, "dependence")));
    for (const auto& dependency : as_array(member(object, "dependencies"))) {
        const auto& link = as_object(dependency);
        check_keys(link, {"evidence_id", "relation"});
        result.dependencies.push_back({parse_id<contracts::EvidenceId>(member(link, "evidence_id")),
            parse_dependency_relation(as_string(member(link, "relation")))});
    }
    return result;
}

[[nodiscard]] bool same_factor(const EvidenceRecord& left, const EvidenceRecord& right) {
    return left.semantics == right.semantics && left.scope == right.scope &&
        left.likelihood == right.likelihood;
}
[[nodiscard]] contracts::DiagnosticCode diagnostic_code(contracts::Status status) {
    switch (status) {
    case contracts::Status::unsupported_input: return contracts::DiagnosticCode::unsupported_input;
    case contracts::Status::invalid_schema: return contracts::DiagnosticCode::invalid_schema;
    case contracts::Status::unknown: return contracts::DiagnosticCode::unknown;
    case contracts::Status::inconsistent: return contracts::DiagnosticCode::inconsistent;
    case contracts::Status::zero_normalizer: return contracts::DiagnosticCode::zero_normalizer;
    case contracts::Status::resource_limit: return contracts::DiagnosticCode::resource_limit;
    default: return contracts::DiagnosticCode::invalid_record;
    }
}

}  // namespace

std::string_view to_string(IdentityResolution value) noexcept {
    switch (value) {
    case IdentityResolution::resolved: return "resolved";
    case IdentityResolution::unresolved: return "unresolved";
    }
    return "invalid_identity_resolution";
}
std::string_view to_string(WorldSemantics value) noexcept {
    switch (value) {
    case WorldSemantics::open_world: return "open_world";
    }
    return "invalid_world_semantics";
}
std::string_view to_string(EvidenceSemantics value) noexcept {
    switch (value) {
    case EvidenceSemantics::likelihood: return "likelihood";
    case EvidenceSemantics::hard_constraint: return "hard_constraint";
    }
    return "invalid_evidence_semantics";
}
std::string_view to_string(DependenceClass value) noexcept {
    switch (value) {
    case DependenceClass::independent: return "independent";
    case DependenceClass::model_factor: return "model_factor";
    case DependenceClass::exact_duplicate: return "exact_duplicate";
    case DependenceClass::correlated: return "correlated";
    case DependenceClass::unknown: return "unknown";
    }
    return "invalid_dependence";
}
std::string_view to_string(DependencyRelation value) noexcept {
    switch (value) {
    case DependencyRelation::exact_duplicate: return "exact_duplicate";
    case DependencyRelation::correlated: return "correlated";
    case DependencyRelation::unknown: return "unknown";
    }
    return "invalid_dependency_relation";
}

BeliefModel::BeliefModel(ModelDefinition definition) : definition_(std::move(definition)) {
    const auto errors = validate_definition();
    if (!errors.empty()) throw std::invalid_argument("invalid belief model: " + errors.front());
    for (std::size_t i = 0; i < definition_.facts.size(); ++i)
        fact_indices_.emplace(definition_.facts[i].fact_id.value(), i);
}

std::vector<std::string> BeliefModel::validate_definition() const {
    std::vector<std::string> errors;
    if (!definition_.run_id.valid() || !definition_.model_id.valid() ||
        !definition_.schema_id.valid() || !definition_.code_build_id.valid() ||
        !definition_.partition_id.valid()) errors.emplace_back("model identity contains an invalid identifier");
    if (definition_.schema_id.value() != "xai.factual_model")
        errors.emplace_back("model schema must be xai.factual_model version 1");
    if (contracts::to_string(definition_.partition) == "invalid_partition" ||
        definition_.partition == contracts::DataPartition::not_applicable)
        errors.emplace_back("belief models require an explicit named data partition");
    if (definition_.world_semantics != WorldSemantics::open_world)
        errors.emplace_back("only explicit open-world semantics are supported");
    if (definition_.limits.max_facts > 63 || definition_.limits.max_worlds == 0 ||
        definition_.limits.max_evidence == 0 || definition_.limits.max_factor_entries == 0 ||
        definition_.limits.max_operations == 0 || definition_.limits.max_rational_bits == 0 ||
        definition_.limits.max_rational_bits > kHardMaxRationalBits)
        errors.emplace_back("resource limits are zero or exceed the supported finite-state ceiling");
    if (definition_.facts.size() != definition_.priors.size())
        errors.emplace_back("every fact must have exactly one prior potential");
    std::set<std::string> fact_ids;
    for (const auto& fact : definition_.facts) {
        if (!fact.fact_id.valid() || !fact_ids.insert(fact.fact_id.value()).second)
            errors.emplace_back("fact identifiers must be valid and unique");
        if (!contracts::valid_identifier(fact.predicate))
            errors.emplace_back("fact predicate must be a stable identifier token");
        if (to_string(fact.subject.resolution) == "invalid_identity_resolution")
            errors.emplace_back("fact identity-resolution enum is invalid");
        std::set<std::string> candidates;
        for (const auto& candidate : fact.subject.candidate_entity_ids) {
            if (!candidate.valid() || !candidates.insert(candidate.value()).second)
                errors.emplace_back("candidate entity identifiers must be valid and unique");
        }
        if (fact.subject.resolution == IdentityResolution::resolved) {
            if (!fact.subject.entity_id || !fact.subject.entity_id->valid() ||
                !fact.subject.candidate_entity_ids.empty())
                errors.emplace_back("resolved identity requires one entity and no candidate list");
        } else if (fact.subject.entity_id) {
            errors.emplace_back("unresolved identity cannot claim a resolved entity");
        }
    }
    std::set<std::string> prior_ids;
    for (const auto& prior : definition_.priors) {
        if (!prior.fact_id.valid() || !prior_ids.insert(prior.fact_id.value()).second ||
            !fact_ids.contains(prior.fact_id.value()))
            errors.emplace_back("prior fact references must be valid, unique, and declared");
        if (prior.false_weight < 0 || prior.true_weight < 0)
            errors.emplace_back("prior potentials must be nonnegative exact rationals");
        if (!rational_within_bits(prior.false_weight, definition_.limits.max_rational_bits) ||
            !rational_within_bits(prior.true_weight, definition_.limits.max_rational_bits))
            errors.emplace_back("prior potential exceeds the configured exact-rational bit limit");
    }
    if (prior_ids.size() != fact_ids.size())
        errors.emplace_back("every fact must have exactly one prior potential");
    return errors;
}

std::vector<std::string> BeliefModel::validate_evidence(const EvidenceRecord& evidence) const {
    std::vector<std::string> errors;
    if (!evidence.evidence_id.valid() || !evidence.source_id.valid())
        errors.emplace_back("evidence and source identifiers must be valid");
    if (!valid_timestamp(evidence.observed_at_utc))
        errors.emplace_back("observation time must be UTC RFC 3339 seconds (YYYY-MM-DDTHH:MM:SSZ)");
    if (!valid_text(evidence.original_observation_ref, 1024))
        errors.emplace_back("original observation reference must be nonempty printable text of at most 1024 bytes");
    if (!valid_text(evidence.extractor_version, 128))
        errors.emplace_back("extractor version must be nonempty printable text of at most 128 bytes");
    if (evidence.partition != definition_.partition)
        errors.emplace_back("evidence partition differs from the model partition");
    if (to_string(evidence.semantics) == "invalid_evidence_semantics" ||
        to_string(evidence.dependence) == "invalid_dependence")
        errors.emplace_back("evidence semantics or dependence enum is invalid");
    if (evidence.scope.size() >= std::numeric_limits<std::size_t>::digits)
        errors.emplace_back("evidence scope is too large to represent its finite likelihood table");
    else {
        const std::size_t expected = std::size_t{1} << evidence.scope.size();
        if (evidence.likelihood.size() != expected)
            errors.emplace_back("likelihood table size must equal 2 raised to its scope size");
    }
    std::set<std::string> scope_ids;
    for (const auto& fact_id : evidence.scope) {
        if (!fact_id.valid() || !fact_indices_.contains(fact_id.value()) ||
            !scope_ids.insert(fact_id.value()).second)
            errors.emplace_back("evidence scope must contain unique declared fact identifiers");
    }
    for (const auto& likelihood : evidence.likelihood) {
        if (likelihood < 0) errors.emplace_back("likelihood values must be nonnegative exact rationals");
        if (!rational_within_bits(likelihood, definition_.limits.max_rational_bits))
            errors.emplace_back("likelihood exceeds the configured exact-rational bit limit");
        if (evidence.semantics == EvidenceSemantics::hard_constraint &&
            likelihood != 0 && likelihood != 1)
            errors.emplace_back("hard-constraint tables must contain only zero or one");
    }
    if (evidence.evidence_id.valid()) {
        for (const auto& prior : evidence_)
            if (prior.evidence_id == evidence.evidence_id)
                errors.emplace_back("evidence identifiers are immutable and cannot be reused");
    }
    std::set<std::string> dependency_ids;
    for (const auto& link : evidence.dependencies) {
        if (!link.evidence_id.valid() || !dependency_ids.insert(link.evidence_id.value()).second)
            errors.emplace_back("dependency references must be valid and unique");
        const auto parent = std::find_if(evidence_.begin(), evidence_.end(), [&](const EvidenceRecord& item) {
            return item.evidence_id == link.evidence_id;
        });
        if (parent == evidence_.end()) errors.emplace_back("dependency must reference earlier ledger evidence");
        if (to_string(link.relation) == "invalid_dependency_relation")
            errors.emplace_back("dependency relation enum is invalid");
    }
    switch (evidence.dependence) {
    case DependenceClass::independent:
        if (!evidence.dependencies.empty()) errors.emplace_back("independent evidence cannot declare dependency links");
        break;
    case DependenceClass::exact_duplicate:
        if (evidence.dependencies.size() != 1 ||
            evidence.dependencies.front().relation != DependencyRelation::exact_duplicate)
            errors.emplace_back("duplicate evidence must link exactly one prior duplicate factor");
        else {
            const auto parent = std::find_if(evidence_.begin(), evidence_.end(), [&](const EvidenceRecord& item) {
                return item.evidence_id == evidence.dependencies.front().evidence_id;
            });
            if (parent != evidence_.end() && !same_factor(evidence, *parent))
                errors.emplace_back("duplicate evidence must have the identical scope and likelihood factor");
        }
        break;
    case DependenceClass::model_factor:
        if (evidence.semantics != EvidenceSemantics::hard_constraint || !evidence.dependencies.empty())
            errors.emplace_back("model factors must be standalone hard constraints in the declared finite model");
        break;
    case DependenceClass::correlated:
        if (evidence.dependencies.empty() || std::any_of(evidence.dependencies.begin(), evidence.dependencies.end(),
                [](const EvidenceLink& link) { return link.relation != DependencyRelation::correlated; }))
            errors.emplace_back("correlated evidence must link its related evidence explicitly");
        break;
    case DependenceClass::unknown:
        if (std::any_of(evidence.dependencies.begin(), evidence.dependencies.end(),
                [](const EvidenceLink& link) { return link.relation != DependencyRelation::unknown; }))
            errors.emplace_back("unresolved dependence may only use unknown dependency links");
        break;
    }
    return errors;
}

IngestionResult BeliefModel::add_evidence(EvidenceRecord evidence) {
    if (evidence_.size() >= definition_.limits.max_evidence)
        return {contracts::Status::resource_limit, "evidence-count limit reached"};
    if (evidence.likelihood.size() > definition_.limits.max_worlds)
        return {contracts::Status::resource_limit, "single evidence factor exceeds the finite world limit"};
    if (factor_entries_ > definition_.limits.max_factor_entries ||
        evidence.likelihood.size() > definition_.limits.max_factor_entries - factor_entries_)
        return {contracts::Status::resource_limit, "aggregate factor-table entry limit reached"};
    for (const auto& likelihood : evidence.likelihood)
        if (!rational_within_bits(likelihood, definition_.limits.max_rational_bits))
            return {contracts::Status::resource_limit, "evidence rational exceeds the exact-arithmetic bit limit"};
    const auto errors = validate_evidence(evidence);
    if (!errors.empty()) return {contracts::Status::invalid_schema, errors.front()};
    const auto appended_factor_entries = static_cast<std::uint64_t>(evidence.likelihood.size());
    evidence_.push_back(std::move(evidence));
    factor_entries_ += appended_factor_entries;
    return {contracts::Status::success, "evidence appended to immutable ledger"};
}

BeliefQuery BeliefModel::query(const contracts::FactId& fact_id) const {
    BeliefQuery result;
    for (const auto& evidence : evidence_) result.evidence_lineage.push_back(evidence.evidence_id);
    const auto target = fact_indices_.find(fact_id.value());
    if (!fact_id.valid() || target == fact_indices_.end()) {
        result.status = contracts::Status::unknown;
        result.diagnostic = "queried fact is absent; open-world absence is unknown";
        return result;
    }
    const auto fact_count = definition_.facts.size();
    if (fact_count > definition_.limits.max_facts || fact_count >= 64) {
        result.status = contracts::Status::resource_limit;
        result.diagnostic = "finite state-count limit exceeded";
        return result;
    }
    const std::uint64_t world_count = std::uint64_t{1} << fact_count;
    if (world_count > definition_.limits.max_worlds) {
        result.status = contracts::Status::resource_limit;
        result.diagnostic = "finite world-count limit exceeded";
        return result;
    }
    for (const auto& evidence : evidence_) {
        if (evidence.dependence == DependenceClass::correlated ||
            evidence.dependence == DependenceClass::unknown) {
            result.status = contracts::Status::unsupported_input;
            result.diagnostic = evidence.dependence == DependenceClass::correlated
                ? "correlated evidence requires a declared joint likelihood; separate factors were not multiplied"
                : "evidence dependence is unresolved; independent corroboration was not assumed";
            return result;
        }
    }
    std::vector<const LiteralPrior*> priors(fact_count, nullptr);
    for (const auto& prior : definition_.priors)
        priors[fact_indices_.at(prior.fact_id.value())] = &prior;
    auto consume_operation = [&]() {
        if (result.operations >= definition_.limits.max_operations) return false;
        ++result.operations;
        return true;
    };
    Rational prior_normalizer{1};
    for (const auto* prior : priors) {
        if (!consume_operation()) {
            result.status = contracts::Status::resource_limit;
            result.diagnostic = "exact-inference operation limit reached";
            return result;
        }
        const Rational variable_normalizer = prior->false_weight + prior->true_weight;
        if (!rational_within_bits(variable_normalizer, definition_.limits.max_rational_bits)) {
            result.status = contracts::Status::resource_limit;
            result.diagnostic = "prior normalizer exceeds the exact-arithmetic bit limit";
            return result;
        }
        prior_normalizer *= variable_normalizer;
        if (!rational_within_bits(prior_normalizer, definition_.limits.max_rational_bits)) {
            result.status = contracts::Status::resource_limit;
            result.diagnostic = "prior normalizer exceeds the exact-arithmetic bit limit";
            return result;
        }
    }
    result.prior_normalizer = prior_normalizer;
    if (prior_normalizer == 0) {
        result.status = contracts::Status::zero_normalizer;
        result.diagnostic = "prior potentials have a zero normalizer";
        return result;
    }
    const bool has_hard_constraint = std::any_of(evidence_.begin(), evidence_.end(),
        [](const EvidenceRecord& item) {
            return item.dependence != DependenceClass::exact_duplicate &&
                   item.semantics == EvidenceSemantics::hard_constraint;
    });
    Rational evidence_normalizer{0};
    Rational true_mass{0};
    for (std::uint64_t world = 0; world < world_count; ++world) {
        if (!consume_operation()) {
            result.status = contracts::Status::resource_limit;
            result.diagnostic = "exact-inference operation limit reached";
            return result;
        }
        Rational mass{1};
        for (std::size_t i = 0; i < fact_count; ++i) {
            if (!consume_operation()) {
                result.status = contracts::Status::resource_limit;
                result.diagnostic = "exact-inference operation limit reached";
                return result;
            }
            const bool value = ((world >> i) & 1U) != 0;
            mass *= value ? priors[i]->true_weight : priors[i]->false_weight;
            if (!rational_within_bits(mass, definition_.limits.max_rational_bits)) {
                result.status = contracts::Status::resource_limit;
                result.diagnostic = "world weight exceeds the exact-arithmetic bit limit";
                return result;
            }
            if (mass == 0) break;
        }
        if (mass == 0) continue;
        bool supported = true;
        for (const auto& evidence : evidence_) {
            if (evidence.dependence == DependenceClass::exact_duplicate) continue;
            if (!consume_operation()) {
                result.status = contracts::Status::resource_limit;
                result.diagnostic = "exact-inference operation limit reached";
                return result;
            }
            std::size_t local_index = 0;
            for (std::size_t bit = 0; bit < evidence.scope.size(); ++bit) {
                if (!consume_operation()) {
                    result.status = contracts::Status::resource_limit;
                    result.diagnostic = "exact-inference operation limit reached";
                    return result;
                }
                const auto fact_index = fact_indices_.at(evidence.scope[bit].value());
                if (((world >> fact_index) & 1U) != 0) local_index |= std::size_t{1} << bit;
            }
            mass *= evidence.likelihood[local_index];
            if (!rational_within_bits(mass, definition_.limits.max_rational_bits)) {
                result.status = contracts::Status::resource_limit;
                result.diagnostic = "evidence-updated world weight exceeds the exact-arithmetic bit limit";
                return result;
            }
            if (mass == 0) { supported = false; break; }
        }
        if (!supported) continue;
        evidence_normalizer += mass;
        if (!rational_within_bits(evidence_normalizer, definition_.limits.max_rational_bits)) {
            result.status = contracts::Status::resource_limit;
            result.diagnostic = "evidence normalizer exceeds the exact-arithmetic bit limit";
            return result;
        }
        if (((world >> target->second) & 1U) != 0) {
            true_mass += mass;
            if (!rational_within_bits(true_mass, definition_.limits.max_rational_bits)) {
                result.status = contracts::Status::resource_limit;
                result.diagnostic = "marginal numerator exceeds the exact-arithmetic bit limit";
                return result;
            }
        }
    }
    result.evidence_normalizer = evidence_normalizer;
    if (evidence_normalizer == 0) {
        result.status = has_hard_constraint ? contracts::Status::inconsistent
                                             : contracts::Status::zero_normalizer;
        result.diagnostic = has_hard_constraint
            ? "hard evidence is contradictory under the prior support"
            : "likelihood evidence has a zero normalizer";
        return result;
    }
    result.conditional_normalizer = evidence_normalizer / prior_normalizer;
    result.probability_true = true_mass / evidence_normalizer;
    if (!rational_within_bits(*result.conditional_normalizer, definition_.limits.max_rational_bits) ||
        !rational_within_bits(*result.probability_true, definition_.limits.max_rational_bits)) {
        result.conditional_normalizer.reset();
        result.probability_true.reset();
        result.status = contracts::Status::resource_limit;
        result.diagnostic = "normalized query result exceeds the exact-arithmetic bit limit";
        return result;
    }
    result.status = contracts::Status::success;
    result.diagnostic = "exact finite marginal computed";
    return result;
}

contracts::RecordEnvelope BeliefModel::query_record(
    const contracts::QueryId& query_id,
    const contracts::ResultId& result_id,
    const contracts::FactId& fact_id) const {
    const auto answer = query(fact_id);
    contracts::RecordEnvelope record;
    record.identity.run_id = definition_.run_id;
    record.identity.result_id = result_id;
    record.identity.schema_id = contracts::SchemaId{"xai.belief_query"};
    record.identity.code_build_id = definition_.code_build_id;
    record.identity.partition = definition_.partition;
    record.identity.partition_id = definition_.partition_id;
    record.identity.model_id = definition_.model_id;
    record.identity.query_id = query_id;
    if (fact_id.valid() && fact_indices_.contains(fact_id.value())) record.identity.fact_id = fact_id;
    if (evidence_.size() == 1) record.identity.evidence_id = evidence_.front().evidence_id;
    record.assumptions = {
        "open-world semantics: absent facts remain unknown; no closed-world rule is inferred",
        "prior potentials and likelihood factors are exact rationals",
        "independence is applied only when explicitly declared; exact duplicates are counted once"};
    if (fact_id.valid() && fact_indices_.contains(fact_id.value())) {
        const auto& subject = definition_.facts[fact_indices_.at(fact_id.value())].subject;
        record.lineage.push_back({contracts::AnyIdentifier{fact_id}, contracts::LineageRelation::derived_from});
        if (subject.resolution == IdentityResolution::resolved && subject.entity_id) {
            record.identity.entity_id = subject.entity_id;
            record.lineage.push_back({contracts::AnyIdentifier{*subject.entity_id}, contracts::LineageRelation::observes});
        } else {
            record.assumptions.push_back("queried proposition retains unresolved entity identity; the marginal is only for this fact identifier");
            for (const auto& candidate : subject.candidate_entity_ids)
                record.lineage.push_back({contracts::AnyIdentifier{candidate}, contracts::LineageRelation::observes});
        }
    }
    record.lineage.push_back({contracts::AnyIdentifier{definition_.model_id}, contracts::LineageRelation::derived_from});
    record.lineage.push_back({contracts::AnyIdentifier{definition_.schema_id}, contracts::LineageRelation::validates});
    record.lineage.push_back({contracts::AnyIdentifier{definition_.code_build_id}, contracts::LineageRelation::produced_by});
    for (const auto& evidence : evidence_) {
        record.lineage.push_back({contracts::AnyIdentifier{evidence.evidence_id}, contracts::LineageRelation::derived_from});
        record.lineage.push_back({contracts::AnyIdentifier{evidence.source_id}, contracts::LineageRelation::cites});
    }
    record.exactness = contracts::Exactness::not_applicable;
    record.budget.operations = definition_.limits.max_operations;
    record.consumed.operations = answer.operations;
    record.reproducibility.algorithm = "xai-finite-exact-marginal-v1";
    if (answer.status == contracts::Status::success) {
        record.status = contracts::Status::success;
        record.exactness = contracts::Exactness::exact;
        record.result = Value{Object{
            {"conditional_normalizer", rational_value(*answer.conditional_normalizer)},
            {"evidence_normalizer", rational_value(*answer.evidence_normalizer)},
            {"fact_id", Value{fact_id.value()}},
            {"marginal_true", rational_value(*answer.probability_true)},
            {"prior_normalizer", rational_value(*answer.prior_normalizer)}}};
    } else {
        record.status = answer.status;
        record.errors.push_back({diagnostic_code(answer.status), answer.diagnostic, std::nullopt});
    }
    return record;
}

contracts::RecordEnvelope BeliefModel::model_record() const {
    Array facts;
    facts.reserve(definition_.facts.size());
    for (const auto& fact : definition_.facts) facts.push_back(fact_value(fact));
    Array priors;
    priors.reserve(definition_.priors.size());
    for (const auto& prior : definition_.priors) priors.push_back(prior_value(prior));
    Array evidence;
    evidence.reserve(evidence_.size());
    for (const auto& item : evidence_) evidence.push_back(evidence_value(item));
    Object result{{"evidence", Value{std::move(evidence)}},
        {"facts", Value{std::move(facts)}}, {"limits", limits_value(definition_.limits)},
        {"partition_id", Value{definition_.partition_id.value()}},
        {"priors", Value{std::move(priors)}},
        {"world_semantics", Value{std::string(to_string(definition_.world_semantics))}}};
    contracts::RecordEnvelope record;
    record.identity.run_id = definition_.run_id;
    record.identity.result_id = contracts::ResultId{"model-snapshot"};
    record.identity.schema_id = definition_.schema_id;
    record.identity.code_build_id = definition_.code_build_id;
    record.identity.partition = definition_.partition;
    record.identity.partition_id = definition_.partition_id;
    record.identity.model_id = definition_.model_id;
    record.status = contracts::Status::success;
    record.result = Value{std::move(result)};
    record.assumptions = {
        "finite Boolean facts with explicit exact prior potentials",
        "open-world semantics: absence does not imply false",
        "source dependence is recorded and unresolved/correlated evidence is not multiplied"};
    for (const auto& item : evidence_) {
        record.lineage.push_back({contracts::AnyIdentifier{item.evidence_id}, contracts::LineageRelation::derived_from});
        record.lineage.push_back({contracts::AnyIdentifier{item.source_id}, contracts::LineageRelation::cites});
    }
    record.exactness = contracts::Exactness::exact;
    record.budget.operations = definition_.limits.max_operations;
    record.reproducibility.algorithm = "xai-finite-belief-state-snapshot-v1";
    return record;
}

std::string BeliefModel::serialize() const { return contracts::encode_record(model_record()); }

FormulaIngestionResult ingest_weighted_cnf(
    const xai::wmc::Instance& instance,
    ModelDefinition model_metadata,
    const FormulaProvenance& provenance) {
    const auto invalid = [](std::string message) {
        return FormulaIngestionResult{contracts::Status::invalid_schema, std::nullopt, std::move(message)};
    };
    const auto limited = [](std::string message) {
        return FormulaIngestionResult{contracts::Status::resource_limit, std::nullopt, std::move(message)};
    };
    if (!model_metadata.facts.empty() || !model_metadata.priors.empty())
        return invalid("formula adapter requires empty fact/prior fields in model metadata");
    if (!provenance.source_id.valid()) return invalid("formula source identifier is invalid");
    if (instance.clauses.size() != instance.declared_clauses || instance.weights.size() != instance.variables)
        return invalid("parsed WMC instance has inconsistent variable, weight, or clause counts");
    if (instance.variables > model_metadata.limits.max_facts || instance.variables >= 64)
        return limited("formula variable count exceeds the exact finite fact limit");
    if (instance.clauses.size() > model_metadata.limits.max_evidence)
        return limited("formula clause count exceeds the configured evidence-record limit");
    const std::uint64_t worlds = std::uint64_t{1} << instance.variables;
    if (worlds > model_metadata.limits.max_worlds)
        return limited("formula world count exceeds the exact finite state limit");

    for (std::size_t i = 0; i < instance.variables; ++i) {
        const std::string number = std::to_string(i + 1);
        model_metadata.facts.push_back({contracts::FactId{"var:" + number}, "wmc.variable",
            IdentityReference{IdentityResolution::resolved, contracts::EntityId{"variable:" + number}, {}}});
        model_metadata.priors.push_back({contracts::FactId{"var:" + number},
            instance.weights[i].negative, instance.weights[i].positive});
    }

    try {
        BeliefModel model{std::move(model_metadata)};
        std::map<std::vector<xai::wmc::Literal>, contracts::EvidenceId> seen_clauses;
        std::uint64_t factor_entries = 0;
        for (std::size_t clause_index = 0; clause_index < instance.clauses.size(); ++clause_index) {
            auto clause = instance.clauses[clause_index];
            std::sort(clause.begin(), clause.end(), [](xai::wmc::Literal left, xai::wmc::Literal right) {
                const auto left_wide = static_cast<std::int64_t>(left);
                const auto right_wide = static_cast<std::int64_t>(right);
                const auto left_variable = left_wide < 0 ? -left_wide : left_wide;
                const auto right_variable = right_wide < 0 ? -right_wide : right_wide;
                return left_variable == right_variable ? left < right : left_variable < right_variable;
            });
            clause.erase(std::unique(clause.begin(), clause.end()), clause.end());
            std::set<std::size_t> variables;
            for (const auto literal : clause) {
                const auto wide_literal = static_cast<std::int64_t>(literal);
                const auto variable = static_cast<std::size_t>(wide_literal < 0 ? -wide_literal : wide_literal);
                if (literal == 0 || variable == 0 || variable > instance.variables)
                    return invalid("clause contains a zero or out-of-range literal");
                variables.insert(variable);
            }
            if (variables.size() >= std::numeric_limits<std::size_t>::digits)
                return limited("single-clause state table exceeds representable size");
            const std::size_t table_size = std::size_t{1} << variables.size();
            if (table_size > model.definition().limits.max_worlds)
                return limited("single-clause factor table exceeds the configured finite limit");
            if (factor_entries > model.definition().limits.max_factor_entries ||
                table_size > model.definition().limits.max_factor_entries - factor_entries)
                return limited("aggregate clause-factor table limit reached");
            factor_entries += static_cast<std::uint64_t>(table_size);
            std::vector<std::size_t> variable_list(variables.begin(), variables.end());
            EvidenceRecord evidence;
            evidence.evidence_id = contracts::EvidenceId{"clause:" + std::to_string(clause_index + 1)};
            evidence.source_id = provenance.source_id;
            evidence.observed_at_utc = provenance.observed_at_utc;
            evidence.original_observation_ref = provenance.original_observation_ref;
            evidence.extractor_version = provenance.extractor_version;
            evidence.partition = model.definition().partition;
            evidence.semantics = EvidenceSemantics::hard_constraint;
            const auto duplicate = seen_clauses.find(clause);
            if (duplicate == seen_clauses.end()) {
                evidence.dependence = DependenceClass::model_factor;
                seen_clauses.emplace(clause, evidence.evidence_id);
            } else {
                evidence.dependence = DependenceClass::exact_duplicate;
                evidence.dependencies.push_back({duplicate->second, DependencyRelation::exact_duplicate});
            }
            for (const auto variable : variable_list)
                evidence.scope.emplace_back("var:" + std::to_string(variable));
            evidence.likelihood.resize(table_size, Rational{0});
            for (std::size_t assignment = 0; assignment < table_size; ++assignment) {
                bool satisfied = false;
                for (const auto literal : clause) {
                    const auto wide_literal = static_cast<std::int64_t>(literal);
                    const auto variable = static_cast<std::size_t>(wide_literal < 0 ? -wide_literal : wide_literal);
                    const auto position = static_cast<std::size_t>(
                        std::lower_bound(variable_list.begin(), variable_list.end(), variable) - variable_list.begin());
                    const bool value = ((assignment >> position) & 1U) != 0;
                    if ((literal > 0 && value) || (literal < 0 && !value)) {
                        satisfied = true;
                        break;
                    }
                }
                if (satisfied) evidence.likelihood[assignment] = Rational{1};
            }
            const auto inserted = model.add_evidence(std::move(evidence));
            if (!inserted.accepted()) {
                if (inserted.status == contracts::Status::resource_limit)
                    return limited(inserted.diagnostic);
                return invalid(inserted.diagnostic);
            }
        }
        return {contracts::Status::success, std::move(model), "weighted CNF mapped to exact Boolean priors and clause factors"};
    } catch (const std::invalid_argument& error) {
        return invalid(error.what());
    }
}

BeliefModel BeliefModel::restore(std::string_view canonical_record) {
    const auto decoded = contracts::decode_record(canonical_record,
        contracts::SchemaExpectation{contracts::SchemaId{"xai.factual_model"}, 1});
    if (!std::holds_alternative<contracts::RecordEnvelope>(decoded))
        fail("model snapshot failed shared-record validation: " + std::get<contracts::DecodeFailure>(decoded).message);
    const auto& record = std::get<contracts::RecordEnvelope>(decoded);
    if (record.status != contracts::Status::success || record.exactness != contracts::Exactness::exact ||
        !record.result || !record.identity.model_id || !record.identity.partition_id)
        fail("snapshot is not a successful exact factual-model record");
    const auto& root = as_object(*record.result);
    check_keys(root, {"evidence", "facts", "limits", "partition_id", "priors", "world_semantics"});
    ModelDefinition definition;
    definition.run_id = record.identity.run_id;
    definition.model_id = *record.identity.model_id;
    definition.schema_id = record.identity.schema_id;
    definition.code_build_id = record.identity.code_build_id;
    definition.partition_id = *record.identity.partition_id;
    definition.partition = record.identity.partition;
    if (parse_id<contracts::PartitionId>(member(root, "partition_id")) != definition.partition_id)
        fail("snapshot partition identity disagrees with its record envelope");
    definition.world_semantics = parse_world_semantics(as_string(member(root, "world_semantics")));
    for (const auto& fact : as_array(member(root, "facts"))) definition.facts.push_back(parse_fact(fact));
    for (const auto& prior : as_array(member(root, "priors"))) definition.priors.push_back(parse_prior(prior));
    const auto& limits = as_object(member(root, "limits"));
    check_keys(limits, {"max_evidence", "max_factor_entries", "max_facts", "max_operations",
        "max_rational_bits", "max_worlds"});
    const auto max_facts = as_u64(member(limits, "max_facts"));
    const auto max_evidence = as_u64(member(limits, "max_evidence"));
    const auto max_rational_bits = as_u64(member(limits, "max_rational_bits"));
    if (max_facts > std::numeric_limits<std::size_t>::max() ||
        max_evidence > std::numeric_limits<std::size_t>::max() ||
        max_rational_bits > std::numeric_limits<std::size_t>::max())
        fail("snapshot resource limit exceeds host size range");
    definition.limits.max_facts = static_cast<std::size_t>(max_facts);
    definition.limits.max_evidence = static_cast<std::size_t>(max_evidence);
    definition.limits.max_factor_entries = as_u64(member(limits, "max_factor_entries"));
    definition.limits.max_operations = as_u64(member(limits, "max_operations"));
    definition.limits.max_rational_bits = static_cast<std::size_t>(max_rational_bits);
    definition.limits.max_worlds = as_u64(member(limits, "max_worlds"));
    BeliefModel model{std::move(definition)};
    for (const auto& evidence : as_array(member(root, "evidence"))) {
        const auto inserted = model.add_evidence(parse_evidence(evidence));
        if (!inserted.accepted()) fail("snapshot contains invalid evidence: " + inserted.diagnostic);
    }
    if (model.serialize() != canonical_record) fail("snapshot is not a canonical replay of its typed state");
    return model;
}

}  // namespace xai::ingestion
