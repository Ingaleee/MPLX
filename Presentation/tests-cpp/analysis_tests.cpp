#include "frontend.hpp"
#include "reference.hpp"
#include "vm.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <random>

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }
    template <class F> void rejected(F operation, const std::string &message) {
        try {
            operation();
        } catch (const std::exception &) {
            ++checks;
            return;
        }
        expect(false, message);
    }
    mplx::IRModule graph_module(const std::vector<std::vector<uint32_t>> &edges) {
        using namespace mplx;
        IRFunction function;
        function.name = "main";
        function.return_type = ValueType::Bool;
        function.values = {ValueType::Bool};
        for (uint32_t index = 0; index < edges.size(); ++index) {
            IRBlock block;
            block.id = index;
            if (index == 0)
                block.instructions = {{IROp::Constant, 0, ValueType::Bool, {}, 0, 1, "", {}}};
            if (edges[index].empty())
                block.terminator = {TerminatorKind::Return, 0, no_id, no_id, {}};
            else if (edges[index].size() == 1)
                block.terminator = {TerminatorKind::Jump, no_id, edges[index][0], no_id, {}};
            else
                block.terminator = {TerminatorKind::Branch, 0, edges[index][0], edges[index][1], {}};
            function.blocks.push_back(std::move(block));
        }
        return {{function}};
    }
    std::vector<bool> reach(const std::vector<std::vector<uint32_t>> &edges, uint32_t root,
                            uint32_t forbidden = mplx::no_id) {
        std::vector<bool> result(edges.size(), false);
        std::vector<uint32_t> pending;
        if (root != forbidden) {
            pending.push_back(root);
            result[root] = true;
        }
        while (!pending.empty()) {
            const auto block = pending.back();
            pending.pop_back();
            for (auto target : edges[block])
                if (target != forbidden && !result[target]) {
                    result[target] = true;
                    pending.push_back(target);
                }
        }
        return result;
    }
    void graph_oracles() {
        std::mt19937 random(0x43464732);
        for (unsigned sample = 0; sample < 180; ++sample) {
            const uint32_t count = 2 + random() % 17;
            std::vector<std::vector<uint32_t>> edges(count);
            for (auto &edge : edges)
                for (unsigned index = 0, degree = random() % 3; index < degree; ++index)
                    edge.push_back(random() % count);
            auto module = graph_module(edges);
            expect(mplx::verify_ir(module).empty(), "random valid CFG rejected");
            mplx::IRAnalysisManager analyses(module);
            const auto cfg = analyses.control_flow(0);
            const auto post = analyses.postdominance(0);
            const auto loops = analyses.loops(0);
            const auto reachable = reach(edges, 0);
            std::vector<std::vector<bool>> transitive(count);
            for (uint32_t block = 0; block < count; ++block)
                transitive[block] = reach(edges, block);
            for (uint32_t definition = 0; definition < count; ++definition) {
                const auto avoiding = reach(edges, 0, definition);
                for (uint32_t use = 0; use < count; ++use) {
                    expect(cfg->dominates(definition, use) ==
                               (reachable[definition] && reachable[use] && !avoiding[use]),
                           "dominator disagrees with node-removal oracle");
                    const auto paths = reach(edges, use, definition);
                    bool exits = false, avoiding_exit = false;
                    for (uint32_t block = 0; block < count; ++block)
                        if (edges[block].empty()) {
                            exits |= transitive[use][block];
                            avoiding_exit |= paths[block];
                        }
                    expect(post->postdominates(definition, use) ==
                               (reachable[use] && reachable[definition] && exits && !avoiding_exit),
                           "postdominator disagrees with finite-exit oracle");
                    if (reachable[definition] && reachable[use])
                        expect((loops->component_of[definition] == loops->component_of[use]) ==
                                   (transitive[definition][use] && transitive[use][definition]),
                               "SCC disagrees with mutual reachability");
                }
            }
        }
        const auto infinite = graph_module({{1, 2}, {1}, {}});
        mplx::IRAnalysisManager analyses(infinite);
        const auto post = analyses.postdominance(0);
        expect(!post->reaches_exit[1] && post->immediate[1] == mplx::no_id,
               "infinite component got a postdominator");
        expect(post->postdominates(2, 0), "finite-path postdominance model");
        expect(analyses.effects()->functions[0].may_diverge, "finite postdominance hid divergence");
        const auto irreducible = graph_module({{1, 2}, {2, 3}, {1, 3}, {}});
        mplx::IRAnalysisManager unusual(irreducible);
        auto loops = unusual.loops(0);
        expect(std::count(loops->irreducible.begin(), loops->irreducible.end(), true) == 1,
               "two-entry cycle not classified irreducible");
        expect(loops->loops.empty(), "irreducible cycle invented a dominating header");
        const auto nested_irreducible = graph_module({{1}, {2, 3}, {3, 1}, {2, 1}});
        mplx::IRAnalysisManager difficult(nested_irreducible);
        const auto inner = difficult.loops(0);
        expect(std::count(inner->irreducible.begin(), inner->irreducible.end(), true) == 1,
               "outer SCC concealed irreducible inner cycle");
        const auto nested = graph_module({{1}, {2, 6}, {3, 5}, {4, 2}, {3}, {1}, {}});
        mplx::IRAnalysisManager nest(nested);
        loops = nest.loops(0);
        expect(loops->loops.size() == 3, "nested natural loops missing");
        expect(loops->loops[0].depth == 1 && loops->loops[1].depth == 2 && loops->loops[2].depth == 3,
               "loop forest nesting");
        expect(loops->loops[2].latches == std::vector<uint32_t>{4}, "loop latch classification");
    }
    mplx::IRModule phi_module() {
        using namespace mplx;
        IRFunction function;
        function.name = "choose";
        function.parameters = function.locals = {ValueType::Bool};
        function.return_type = ValueType::I64;
        function.ssa = true;
        function.values = {ValueType::Bool, ValueType::I64, ValueType::I64, ValueType::I64};
        IRBlock entry;
        entry.instructions = {{IROp::Parameter, 0, ValueType::Bool, {}, 0, 0, "", {}}};
        entry.terminator = {TerminatorKind::Branch, 0, 1, 2, {}};
        IRBlock left;
        left.id = 1;
        left.instructions = {{IROp::Constant, 1, ValueType::I64, {}, 0, 17, "", {}}};
        left.terminator = {TerminatorKind::Jump, no_id, 3, no_id, {}};
        IRBlock right;
        right.id = 2;
        right.instructions = {{IROp::Constant, 2, ValueType::I64, {}, 0, 29, "", {}}};
        right.terminator = left.terminator;
        IRBlock merge;
        merge.id = 3;
        merge.instructions = {{IROp::Phi, 3, ValueType::I64, {1, 2}, 0, 0, "", {}, {1, 2}}};
        merge.terminator = {TerminatorKind::Return, 3, no_id, no_id, {}};
        function.blocks = {entry, left, right, merge};
        return {{function}};
    }
    void dataflow_and_cache() {
        auto module = phi_module();
        expect(mplx::verify_ir(module).empty(), "phi fixture");
        mplx::IRAnalysisManager analyses(module);
        const auto live = analyses.liveness(0);
        expect(live->edge_live.at({1, 3}) == std::set<uint32_t>{1}, "left phi edge liveness");
        expect(live->edge_live.at({2, 3}) == std::set<uint32_t>{2}, "right phi edge liveness");
        expect(live->live_in[3].empty() && live->live_out[0].empty(),
               "phi operands incorrectly live in merge");
        const auto def_use = analyses.def_use(0);
        expect(def_use->definitions[3] == mplx::ValueLocation{3, 0, mplx::no_id}, "phi definition location");
        expect(def_use->uses[1] == std::vector<mplx::ValueLocation>{{3, 0, 1}},
               "phi use must retain edge identity");
        expect(def_use->uses[3] == std::vector<mplx::ValueLocation>{{3, mplx::no_id, mplx::no_id}},
               "terminator use location");
        const auto cfg = analyses.control_flow(0);
        module.functions[0].blocks[1].instructions[0].constant = 31;
        expect(analyses.control_flow(0) == cfg, "value change unnecessarily invalidated topology");
        expect(analyses.def_use(0) != def_use, "value analysis reused across content revision");
        const auto before = module;
        module.functions[0].blocks[0].terminator.first = 2;
        rejected([&] { analyses.finish_pass(before, mplx::preserve_cfg); },
                 "false CFG preservation accepted");
        expect(analyses.control_flow(0) != cfg, "direct mutation returned stale CFG");
        const auto &statistics = analyses.statistics();
        expect(statistics[static_cast<size_t>(mplx::AnalysisKind::ControlFlow)].hits >= 1,
               "analysis cache never hit");
        expect(statistics[static_cast<size_t>(mplx::AnalysisKind::DefUse)].invalidations == 1,
               "analysis invalidation accounting");
    }
    void effects_and_elimination() {
        const std::string source = "fn total(x: i64) -> i64 { return x; }\n"
                                   "fn wrapper(x: i64) -> i64 { return total(x); }\n"
                                   "fn trap(x: i64) -> i64 { return 1 / x; }\n"
                                   "fn recursive() -> i64 { return recursive(); }\n"
                                   "fn main() -> i64 { wrapper(7); trap(1); return 42; }\n";
        auto result = mplx::analyze_source(source, false);
        expect(result.ok(), "effect fixture");
        mplx::IRAnalysisManager analyses(result.ir);
        const auto effect = analyses.effects();
        expect(effect->functions[0].can_discard() && effect->functions[1].can_discard(),
               "transitive total function incorrectly effectful");
        expect(effect->functions[2].may_trap && !effect->functions[2].can_discard(),
               "trapping call classified removable");
        expect(effect->recursive[3] && effect->functions[3].may_diverge, "recursive effect SCC");
        expect(effect->functions[4].may_trap, "callee trap did not propagate");
        const auto report = mplx::run_optimization_pipeline(result.ir);
        std::vector<uint32_t> targets;
        for (const auto &block : result.ir.functions[4].blocks)
            for (const auto &instruction : block.instructions)
                if (instruction.op == mplx::IROp::Call)
                    targets.push_back(instruction.index);
        expect(targets == std::vector<uint32_t>{2}, "DCE removed trap or retained total unused call");
        expect(report.passes.back().analyses[static_cast<size_t>(mplx::AnalysisKind::ControlFlow)].hits > 0,
               "pass pipeline failed to reuse preserved CFG");
        const auto bytecode = mplx::Compiler(false).compile(result.ir);
        mplx::VM vm(bytecode.bc);
        expect(vm.run() == 42, "interprocedural DCE changed value");
        auto diverging =
            mplx::analyze_source("fn recurse()->i64{return recurse();}fn main()->i64{recurse();return 42;}");
        expect(diverging.ok(), "divergence fixture");
        const auto summary = mplx::analyze_effects(diverging.ir);
        expect(summary.functions.back().may_diverge, "caller divergence not propagated");
        targets.clear();
        for (const auto &block : diverging.ir.functions.back().blocks)
            for (const auto &instruction : block.instructions)
                if (instruction.op == mplx::IROp::Call)
                    targets.push_back(instruction.index);
        expect(!targets.empty(), "unused diverging call eliminated");
    }
    void snapshots_and_transactions() {
        auto module = phi_module();
        const auto bytes = mplx::serialize_ir_snapshot(module);
        auto unused_value = module;
        unused_value.functions[0].blocks[1].terminator.value = 0x1000000;
        expect(!mplx::verify_ir(unused_value).empty(), "jump metadata bypassed value validation");
        rejected([&] { mplx::deserialize_ir_snapshot(mplx::serialize_ir_snapshot(unused_value)); },
                 "invalid jump snapshot accepted");
        auto unused_target = module;
        unused_target.functions[0].blocks[3].terminator.first = 42;
        expect(!mplx::verify_ir(unused_target).empty(), "return metadata bypassed target validation");
        expect(mplx::serialize_ir_snapshot(mplx::deserialize_ir_snapshot(bytes)) == bytes,
               "snapshot round trip not canonical");
        expect(mplx::ir_fingerprint(module) == mplx::ir_fingerprint(mplx::deserialize_ir_snapshot(bytes)),
               "fingerprint not deterministic");
        for (size_t length = 0; length < bytes.size(); ++length)
            rejected([&] { mplx::deserialize_ir_snapshot(std::string_view(bytes).substr(0, length)); },
                     "truncated snapshot accepted");
        rejected([&] { mplx::deserialize_ir_snapshot(bytes + '\0'); }, "snapshot trailing bytes accepted");
        auto version = bytes;
        version[8] = 2;
        rejected([&] { mplx::deserialize_ir_snapshot(version); }, "unknown snapshot version accepted");
        std::mt19937 random(0x534e4150);
        for (unsigned index = 0; index < 400; ++index) {
            auto mutation = bytes;
            mutation[random() % mutation.size()] ^= static_cast<char>(1 + random() % 255);
            std::optional<mplx::IRModule> accepted;
            try {
                accepted = mplx::deserialize_ir_snapshot(mutation);
            } catch (const std::runtime_error &) {
                ++checks;
            }
            if (accepted) {
                expect(mplx::verify_ir(*accepted).empty(), "accepted snapshot bypassed verifier");
                expect(mplx::serialize_ir_snapshot(*accepted) == mutation, "accepted noncanonical snapshot");
            }
        }
        mplx::IRPassManager manager;
        manager.add("invalid-result",
                    [](auto &candidate) { candidate.functions[0].blocks.back().terminator.value = 99; });
        try {
            manager.run(module, {true});
            expect(false, "invalid pass published candidate");
        } catch (const mplx::IRPassFailure &failure) {
            expect(failure.record().name == "invalid-result" && failure.record().before_snapshot == bytes,
                   "failure missing input reproducer");
            expect(!failure.record().after_snapshot.empty(), "failure missing candidate snapshot");
            expect(mplx::optimization_failure_json(failure).find("candidate_snapshot_hex") !=
                       std::string::npos,
                   "failure export");
        }
        expect(mplx::serialize_ir_snapshot(module) == bytes, "failed pass changed original module");
        mplx::IRPassManager dishonest;
        dishonest.add("false-preservation", [](mplx::IRModule &candidate, mplx::IRAnalysisManager &analyses) {
            analyses.control_flow(0);
            candidate.functions[0].blocks[0].terminator = {mplx::TerminatorKind::Branch, 0, 2, 1, {}};
            return mplx::preserve_cfg;
        });
        rejected([&] { dishonest.run(module); }, "false preservation did not abort pass");
        expect(mplx::serialize_ir_snapshot(module) == bytes, "preservation failure changed input");
        rejected([&] { manager.run(module, {true, 1}); }, "snapshot budget ignored");
        auto source =
            mplx::analyze_source("fn main()->i64{let x=1;let i=0;while(i<4){x=x+2;i=i+1;}return x;}", false);
        const auto report = mplx::run_optimization_pipeline(source.ir, {true});
        for (const auto &pass : report.passes) {
            auto replay = mplx::deserialize_ir_snapshot(pass.before_snapshot);
            mplx::run_optimization_pipeline(replay, {}, pass.name);
            expect(mplx::serialize_ir_snapshot(replay) == pass.after_snapshot, "isolated pass replay");
            expect(mplx::compare_ir(mplx::deserialize_ir_snapshot(pass.before_snapshot), replay).size() ==
                       pass.changes.size(),
                   "structural diff consistency");
        }
        expect(mplx::optimization_trace_json(report).find("before_snapshot_hex") != std::string::npos,
               "successful trace missing snapshots");
        rejected([&] { mplx::run_optimization_pipeline(source.ir, {}, "not-a-pass"); },
                 "unknown replay pass accepted");
    }
    void range_proofs() {
        const std::vector<std::pair<std::string, bool>> expressions{
            {"x + 0", true},   {"x - 0", true}, {"x * 0", true},   {"x * 1", true},
            {"x / 1", true},   {"x % 1", true}, {"x + 1", false},  {"x / -1", false},
            {"x % -1", false}, {"-x", false},   {"i32(x)", false}, {"x / 0", false},
        };
        const std::array<long long, 11> inputs{INT64_MIN, INT64_MIN + 1, -3037000500LL, INT32_MIN, -1, 0, 1,
                                               INT32_MAX, 3037000500LL,  INT64_MAX - 1, INT64_MAX};
        for (const auto &[expression, total] : expressions) {
            const auto source = "fn main(x: i64) -> i64 { return " + expression + "; }";
            const auto raw = mplx::analyze_source(source, false);
            const auto optimized = mplx::analyze_source(source);
            expect(raw.ok() && optimized.ok(), "range fixture compilation");
            const auto summary = mplx::analyze_effects(raw.ir);
            expect(summary.functions[0].can_discard() == total,
                   "incorrect checked range proof for " + expression);
            const auto ranges = mplx::analyze_integer_ranges(raw.ir.functions[0]);
            const auto returned = raw.ir.functions[0].blocks[0].terminator.value;
            mplx::reference::Program oracle(source);
            for (auto input : inputs) {
                const std::array<long long, 1> arguments{input};
                std::optional<long long> expected;
                try {
                    expected = oracle.invoke("main", arguments);
                    expect(ranges.values[returned].contains(*expected), "interval excludes actual result");
                } catch (const mplx::reference::Error &error) {
                    expect(error.kind() == mplx::reference::ErrorKind::Arithmetic && !total,
                           "proved-total operation trapped in independent oracle");
                }
                for (const auto *bytecode : {&raw.bytecode, &optimized.bytecode})
                    for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
                        mplx::VM machine(*bytecode);
                        machine.setJitMode(mode);
                        try {
                            const auto actual = machine.run("main", arguments);
                            expect(expected.has_value() && actual == *expected,
                                   "range optimization changed result");
                        } catch (const mplx::RuntimeError &error) {
                            expect(!expected && error.code() == mplx::RuntimeErrorCode::Arithmetic,
                                   "range optimization changed trap");
                        }
                    }
            }
        }
        const auto safe =
            mplx::analyze_source("fn safe(x:i32)->i64{return i64(x)+1;}fn main()->i64{safe(7);return 42;}");
        expect(safe.ok(), "widened i32 range fixture");
        const auto effects = mplx::analyze_effects(safe.ir);
        expect(effects.functions[0].can_discard(), "safe widening arithmetic not proven total");
        for (const auto &block : safe.ir.functions[1].blocks)
            for (const auto &instruction : block.instructions)
                expect(instruction.op != mplx::IROp::Call, "unused range-proved total call retained");
        const auto cyclic =
            mplx::analyze_source("fn main(n:i32)->i64{let i=0;while(i<i64(n)){i=i+1;}return i;}");
        expect(cyclic.ok(), "range widening loop fixture");
        const auto ranges = mplx::analyze_integer_ranges(cyclic.ir.functions[0]);
        expect(ranges.widenings > 0 && ranges.iterations < 100, "cyclic ranges failed to widen finitely");
        expect(mplx::analyze_effects(cyclic.ir).functions[0].may_diverge,
               "range widening incorrectly established termination");
    }
} // namespace
int main() {
    try {
        graph_oracles();
        dataflow_and_cache();
        effects_and_elimination();
        range_proofs();
        snapshots_and_transactions();
        std::cout << "Analysis and replay: " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
