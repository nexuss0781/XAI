#include "xai/learning.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
using namespace xai::contracts;
using namespace xai::learning;
using Value = CanonicalValue;
using Object = Value::Object;
using Array = Value::Array;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void near(long double actual, long double expected, long double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}
const Value& member(const Object& object, const std::string& key) {
    const auto found = object.find(key);
    if (found == object.end()) throw std::runtime_error("missing record field: " + key);
    return found->second;
}
const Object& object(const Value& value) {
    const auto* result = std::get_if<Object>(&value.data);
    if (!result) throw std::runtime_error("expected object in record");
    return *result;
}
const Array& array(const Value& value) {
    const auto* result = std::get_if<Array>(&value.data);
    if (!result) throw std::runtime_error("expected array in record");
    return *result;
}
const std::string& string(const Value& value) {
    const auto* result = std::get_if<std::string>(&value.data);
    if (!result) throw std::runtime_error("expected string in record");
    return *result;
}
long double real(const Value& value) {
    const auto& text = string(value);
    std::size_t used = 0;
    const auto parsed = std::stold(text, &used);
    if (used != text.size()) throw std::runtime_error("invalid real in record");
    return parsed;
}
PredictionInput example(std::string id, DataPartition partition = DataPartition::training,
                        std::uint64_t variables = 8, std::uint64_t clauses = 16,
                        std::uint64_t literals = 32) {
    PredictionInput input;
    input.observation_id = ObservationId{"obs:" + id};
    input.result_id = ResultId{"result:" + id};
    input.partition_id = PartitionId{"partition:" + std::string(to_string(partition))};
    input.partition = partition;
    input.phase0_eligible = true;
    input.variables = variables;
    input.clauses = clauses;
    input.literal_occurrences = literals;
    return input;
}

void test_known_mixture_updates_and_records() {
    SequentialPredictor predictor;
    auto first = predictor.observe(example("one"), CompletionOutcome::completed_exact);
    require(first.status == Status::approximate && first.state_updated && first.prediction.has_value(),
            "authorized training outcome did not produce a predictive update");
    near(first.prediction->completion_probability, 0.5L, 1e-15L,
         "uniform Beta(1,1) mixture prior is not 1/2");
    near(*first.prediction->observed_log_probability, std::log(0.5L), 1e-15L,
         "first predictive log score is incorrect");
    near(first.prediction->model_weights_after[0], 1.0L / 3.0L, 1e-15L,
         "symmetric experts changed weights after the first shared outcome");
    require(validate_record(first.record).empty() && first.record.exactness == Exactness::approximate &&
                first.record.identity.partition == DataPartition::training,
            "successful prediction/update record violates the shared envelope contract");

    auto second = predictor.observe(example("two"), CompletionOutcome::completed_exact);
    near(second.prediction->completion_probability, 11.0L / 18.0L, 1e-15L,
         "second prequential probability did not combine the three Beta experts");
    near(predictor.cumulative_mixture_log_score(), std::log(11.0L / 36.0L), 1e-15L,
         "cumulative mixture log score is incorrect for two successes");
    require(predictor.accepted_examples() == 2 && predictor.training_use_audit().size() == 2,
            "accepted observations or training-use decisions were not counted");

    const auto before = predictor.serialize();
    const auto read_only = predictor.predict(example("dev-preview", DataPartition::development));
    require(read_only.status == Status::approximate && !read_only.state_updated &&
                predictor.serialize() == before,
            "read-only development prediction modified model state");
    near(read_only.prediction->completion_probability, 8.0L / 11.0L, 1e-15L,
         "read-only prediction failed to use the accumulated posterior");

    const auto replayed = SequentialPredictor::restore(predictor.serialize());
    require(replayed.serialize() == predictor.serialize(), "predictive state did not round-trip canonically");
    require(validate_record(read_only.record).empty(), "read-only prediction record is invalid");
}

void test_regret_bound_relative_to_declared_library() {
    SequentialPredictor predictor;
    const std::array<CompletionOutcome, 8> outcomes{
        CompletionOutcome::completed_exact, CompletionOutcome::explicit_noncompletion,
        CompletionOutcome::explicit_noncompletion, CompletionOutcome::completed_exact,
        CompletionOutcome::completed_exact, CompletionOutcome::completed_exact,
        CompletionOutcome::explicit_noncompletion, CompletionOutcome::completed_exact};
    for (std::size_t i = 0; i < outcomes.size(); ++i) {
        const auto partition = i < 4 ? DataPartition::training : DataPartition::streaming;
        auto item = example("regret-" + std::to_string(i), partition,
                            i % 2 == 0 ? 8U : 1024U, 16U + i, 32U + i * 7U);
        const auto result = predictor.observe(item, outcomes[i]);
        require(result.status == Status::approximate && result.prediction,
                "valid finite fixture sequence was rejected");
    }
    const auto final = predictor.observe(example("regret-last", DataPartition::streaming, 8, 16, 32),
                                         CompletionOutcome::explicit_noncompletion);
    const auto& payload = object(*final.record.result);
    const auto models = array(member(payload, "models"));
    require(models.size() == 3, "versioned model library does not expose its three declared experts");
    const auto mixture_loss = -predictor.cumulative_mixture_log_score();
    const auto prior_penalty = std::log(3.0L);
    for (const auto& item : models) {
        const auto& model = object(item);
        const auto expert_loss = -real(member(model, "cumulative_model_log_score"));
        require(mixture_loss <= expert_loss + prior_penalty + 1e-12L,
                "sequential mixture exceeded its fixed-expert log-loss bound");
    }
    const auto hash1 = std::string(model_library_sha256());
    const auto hash2 = std::string(model_prior_sha256());
    require(hash1 == "7f39e62795eba78a74b2fe78bf1c0c5b83f726fbf226ca1418babcea527070d3" &&
                hash2 == "1cda1b584481138a803b29135576956b4f438e211add283fda6f5bb47a8b4e45",
            "SHA-256 identity does not match the independent library/prior digest");
}

void test_partition_enforcement_and_training_use_audit() {
    SequentialPredictor predictor;
    const auto initial = predictor.serialize();
    const std::array<DataPartition, 4> protected_partitions{
        DataPartition::development, DataPartition::calibration,
        DataPartition::final_test, DataPartition::not_applicable};
    for (std::size_t i = 0; i < protected_partitions.size(); ++i) {
        const auto attempt = predictor.observe(example("locked-" + std::to_string(i), protected_partitions[i]),
                                               CompletionOutcome::explicit_noncompletion);
        require(attempt.status == Status::unsupported_input && !attempt.state_updated && !attempt.record.result,
                "non-training partition was allowed to update the model");
        require(validate_record(attempt.record).empty(), "partition-rejection record violates shared contract");
    }
    require(predictor.accepted_examples() == 0 && predictor.training_use_audit().size() == 4,
            "partition decisions changed model counts or were not audited");
    const auto snapshot = predictor.serialize();
    require(snapshot != initial && snapshot.find("explicit_noncompletion") == std::string::npos,
            "a rejected partition's label leaked into persisted predictor state");
    const auto restored = SequentialPredictor::restore(snapshot);
    require(restored.training_use_audit().size() == 4 && restored.accepted_examples() == 0,
            "partition-denial audit did not survive state replay");

    const auto final_prediction = restored.predict(example("final-read-only", DataPartition::final_test));
    require(final_prediction.status == Status::approximate && !final_prediction.state_updated,
            "read-only final-test prediction was not available");
}

void test_stream_order_duplicate_and_malformed_features() {
    SequentialPredictor predictor;
    const auto non_result = predictor.observe(example("external-kill"), CompletionOutcome::non_result);
    require(non_result.status == Status::unsupported_input && !non_result.state_updated &&
                predictor.accepted_examples() == 0,
            "external kill/non-result was incorrectly learned as a negative target");
    auto ineligible = example("ineligible");
    ineligible.phase0_eligible = false;
    const auto excluded = predictor.observe(ineligible, CompletionOutcome::explicit_noncompletion);
    require(excluded.status == Status::unsupported_input && !excluded.state_updated &&
                predictor.accepted_examples() == 0,
            "unconfirmed Phase 0 eligibility entered the learning state");
    auto missing = example("missing");
    missing.literal_occurrences.reset();
    const auto invalid = predictor.observe(missing, CompletionOutcome::completed_exact);
    require(invalid.status == Status::unsupported_input && predictor.accepted_examples() == 0,
            "missing required feature was silently imputed or counted");
    require(validate_record(invalid.record).empty() && predictor.training_use_audit().size() == 3,
            "unsupported training feature did not produce an auditable failure");

    const auto first = predictor.observe(example("train"), CompletionOutcome::explicit_noncompletion);
    require(first.state_updated, "valid training record was rejected");
    const auto duplicate = predictor.observe(example("train"), CompletionOutcome::completed_exact);
    require(duplicate.status == Status::invalid_schema && predictor.accepted_examples() == 1,
            "duplicate accepted observation was counted twice");
    const auto stream = predictor.observe(example("stream", DataPartition::streaming),
                                          CompletionOutcome::completed_exact);
    require(stream.state_updated && predictor.accepted_examples() == 2,
            "authorized streaming outcome did not update sequential state");
    const auto late_training = predictor.observe(example("late-training"), CompletionOutcome::completed_exact);
    require(late_training.status == Status::invalid_schema && predictor.accepted_examples() == 2,
            "training updates resumed after streaming started");
    require(predictor.training_use_audit().size() == 7,
            "accepted, malformed, duplicate, and blocked update decisions were not auditable");
}

void test_replay_continuation_and_hash_compatibility() {
    SequentialPredictor original;
    for (int i = 0; i < 5; ++i) {
        const auto outcome = i == 2 ? CompletionOutcome::explicit_noncompletion
                                    : CompletionOutcome::completed_exact;
        const auto part = i < 3 ? DataPartition::training : DataPartition::streaming;
        const auto result = original.observe(example("resume-" + std::to_string(i), part,
                                                     16U + static_cast<unsigned>(i), 40U, 128U + i), outcome);
        require(result.state_updated, "fixture update failed before snapshot");
    }
    const auto snapshot = original.serialize();
    auto resumed = SequentialPredictor::restore(snapshot);
    const auto next_input = example("continuation", DataPartition::streaming, 128, 240, 2048);
    const auto before = original.predict(next_input);
    const auto after = resumed.predict(next_input);
    require(encode_record(before.record) == encode_record(after.record),
            "same-runtime snapshot resume changed a read-only prediction record");
    const auto next_a = original.observe(next_input, CompletionOutcome::explicit_noncompletion);
    const auto next_b = resumed.observe(next_input, CompletionOutcome::explicit_noncompletion);
    require(encode_record(next_a.record) == encode_record(next_b.record) &&
                original.serialize() == resumed.serialize(),
            "same-runtime snapshot resume changed the next update or canonical state");

    auto damaged = snapshot;
    const auto digest = std::string(model_library_sha256());
    const auto position = damaged.find(digest);
    require(position != std::string::npos, "snapshot omitted the library hash");
    damaged[position] = damaged[position] == '0' ? '1' : '0';
    bool hash_rejected = false;
    try { (void)SequentialPredictor::restore(damaged); }
    catch (const std::invalid_argument&) { hash_rejected = true; }
    require(hash_rejected, "snapshot with an incompatible library hash was accepted");

    const auto schema_mismatch = decode_record(snapshot, SchemaExpectation{SchemaId{"xai.other_state"}, 1});
    require(std::holds_alternative<DecodeFailure>(schema_mismatch),
            "shared envelope schema mismatch was silently accepted");

    PredictorDefinition custom_schema;
    custom_schema.result_schema_id = SchemaId{"xai.predictive_prediction.custom"};
    custom_schema.state_schema_id = SchemaId{"xai.predictive_state.custom"};
    SequentialPredictor custom{custom_schema};
    require(custom.observe(example("custom-schema"), CompletionOutcome::completed_exact).state_updated,
            "custom-schema predictor fixture failed");
    const auto custom_replay = SequentialPredictor::restore(custom.serialize());
    require(custom_replay.serialize() == custom.serialize() &&
                custom_replay.predict(example("custom-output", DataPartition::development)).record.identity.schema_id ==
                    SchemaId{"xai.predictive_prediction.custom"},
            "configured output/state schema identifiers did not survive replay");

    auto wrong_library = PredictorDefinition{};
    wrong_library.library_id = ModelId{"library:unbound"};
    bool library_rejected = false;
    try { SequentialPredictor incompatible{wrong_library}; }
    catch (const std::invalid_argument&) { library_rejected = true; }
    require(library_rejected, "arbitrary model identity was allowed to claim the fixed library hash");

    SequentialPredictor one_example;
    require(one_example.observe(example("counter-seed"), CompletionOutcome::completed_exact).state_updated,
            "counter-corruption fixture failed to seed state");
    auto overflowed = one_example.serialize();
    const std::string ordinary_counts = "\"global_counts\":{\"negative\":0,\"positive\":1}";
    const std::string overflow_counts = "\"global_counts\":{\"negative\":2,\"positive\":18446744073709551615}";
    const auto count_position = overflowed.find(ordinary_counts);
    require(count_position != std::string::npos, "snapshot counter fixture not found");
    overflowed.replace(count_position, ordinary_counts.size(), overflow_counts);
    bool overflow_rejected = false;
    try { (void)SequentialPredictor::restore(overflowed); }
    catch (const std::invalid_argument&) { overflow_rejected = true; }
    require(overflow_rejected, "overflowing forged sufficient statistics passed restore validation");

    SequentialPredictor audited;
    require(audited.observe(example("audit-first"), CompletionOutcome::completed_exact).state_updated &&
                audited.observe(example("audit-second", DataPartition::streaming),
                                CompletionOutcome::explicit_noncompletion).state_updated,
            "audit-order fixture failed to create a training/streaming sequence");
    auto impossible_order = audited.serialize();
    const auto first_training = impossible_order.find("\"partition\":\"training\"");
    require(first_training != std::string::npos, "training audit partition not found");
    impossible_order.replace(first_training, std::string("\"partition\":\"training\"").size(),
                             "\"partition\":\"streaming\"");
    const auto last_streaming = impossible_order.rfind("\"partition\":\"streaming\"");
    require(last_streaming != std::string::npos && last_streaming > first_training,
            "streaming audit partition not found");
    impossible_order.replace(last_streaming, std::string("\"partition\":\"streaming\"").size(),
                             "\"partition\":\"training\"");
    bool order_rejected = false;
    try { (void)SequentialPredictor::restore(impossible_order); }
    catch (const std::invalid_argument&) { order_rejected = true; }
    require(order_rejected, "snapshot with training after streaming passed audit validation");

    auto duplicate_audit_id = audited.serialize();
    const std::string second_audit_id = "\"observation_id\":\"obs:audit-second\"";
    const auto duplicate_id_position = duplicate_audit_id.rfind(second_audit_id);
    require(duplicate_id_position != std::string::npos, "second accepted audit ID not found");
    duplicate_audit_id.replace(duplicate_id_position, second_audit_id.size(),
                               "\"observation_id\":\"obs:audit-first\"");
    bool duplicate_audit_rejected = false;
    try { (void)SequentialPredictor::restore(duplicate_audit_id); }
    catch (const std::invalid_argument&) { duplicate_audit_rejected = true; }
    require(duplicate_audit_rejected, "snapshot with a duplicated/missing accepted audit ID was accepted");

    auto wrong_numeric_runtime = one_example.serialize();
    const std::string runtime_field = "\"numeric_runtime_id\":\"";
    const auto runtime_position = wrong_numeric_runtime.find(runtime_field);
    require(runtime_position != std::string::npos, "snapshot omitted numeric runtime compatibility metadata");
    const auto runtime_value = runtime_position + runtime_field.size();
    wrong_numeric_runtime.replace(runtime_value, 1, "x");
    bool runtime_rejected = false;
    try { (void)SequentialPredictor::restore(wrong_numeric_runtime); }
    catch (const std::invalid_argument&) { runtime_rejected = true; }
    require(runtime_rejected, "snapshot from an incompatible numeric runtime was accepted");
}

void test_resource_limits_and_explicit_statuses() {
    PredictorDefinition limits;
    limits.limits.max_examples = 1;
    limits.limits.max_audit_entries = 4;
    limits.limits.max_contexts = 1;
    SequentialPredictor predictor{limits};
    require(predictor.observe(example("at-limit"), CompletionOutcome::completed_exact).state_updated,
            "first example under resource limit was rejected");
    const auto over = predictor.observe(example("over-limit"), CompletionOutcome::completed_exact);
    require(over.status == Status::resource_limit && !over.record.result && predictor.accepted_examples() == 1,
            "example cap returned a partial or successful result");
    require(validate_record(over.record).empty(), "resource-limit record violates shared failure contract");

    PredictorDefinition context_limits;
    context_limits.limits.max_examples = 10;
    context_limits.limits.max_audit_entries = 10;
    context_limits.limits.max_contexts = 1;
    SequentialPredictor context_limited{context_limits};
    require(context_limited.observe(example("bucket-a", DataPartition::training, 1, 1, 1),
                                   CompletionOutcome::completed_exact).state_updated,
            "first structural context was rejected");
    const auto new_context = context_limited.observe(example("bucket-b", DataPartition::training,
        1024, 1024, 1024), CompletionOutcome::completed_exact);
    require(new_context.status == Status::resource_limit && context_limited.accepted_examples() == 1,
            "context cap did not fail closed");

    PredictorDefinition audit_limits;
    audit_limits.limits.max_examples = 10;
    audit_limits.limits.max_audit_entries = 1;
    audit_limits.limits.max_contexts = 10;
    SequentialPredictor audit_limited{audit_limits};
    require(audit_limited.observe(example("audit-one"), CompletionOutcome::completed_exact).state_updated,
            "audit-limit fixture failed first update");
    const auto no_audit_slot = audit_limited.observe(
        example("audit-two", DataPartition::final_test), CompletionOutcome::completed_exact);
    require(no_audit_slot.status == Status::resource_limit && audit_limited.accepted_examples() == 1 &&
                audit_limited.training_use_audit().size() == 1,
            "audit exhaustion allowed an unrecorded or state-changing update");
}

}  // namespace

int main() {
    try {
        test_known_mixture_updates_and_records();
        std::cout << "PASS known_mixture_updates_and_records\n";
        test_regret_bound_relative_to_declared_library();
        std::cout << "PASS regret_bound_relative_to_declared_library\n";
        test_partition_enforcement_and_training_use_audit();
        std::cout << "PASS partition_enforcement_and_training_use_audit\n";
        test_stream_order_duplicate_and_malformed_features();
        std::cout << "PASS stream_order_duplicate_and_malformed_features\n";
        test_replay_continuation_and_hash_compatibility();
        std::cout << "PASS replay_continuation_and_hash_compatibility\n";
        test_resource_limits_and_explicit_statuses();
        std::cout << "PASS resource_limits_and_explicit_statuses\n";
        std::cout << "RESULT: 6/6 predictive-learning test groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
