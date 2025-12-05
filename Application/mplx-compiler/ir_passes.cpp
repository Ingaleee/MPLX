#include "ir_passes.hpp"
#include "ir_analysis.hpp"
#include "ssa.hpp"
#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>

namespace mplx {
    namespace {
        IRSize measure(const IRModule &module) {
            IRSize result;
            for (const auto &function : module.functions) {
                result.blocks += function.blocks.size();
                result.locals += function.locals.size();
                for (const auto &block : function.blocks) {
                    result.instructions += block.instructions.size();
                    for (const auto &instruction : block.instructions)
                        result.phis += instruction.op == IROp::Phi;
                }
            }
            return result;
        }
        enum class LatticeKind { Unknown, Constant, Overdefined };
        struct Lattice {
            LatticeKind kind{LatticeKind::Unknown};
            long long value{0};
            bool operator==(const Lattice &) const = default;
        };
        Lattice meet(Lattice first, Lattice second) {
            if (first.kind == LatticeKind::Unknown)
                return second;
            if (second.kind == LatticeKind::Unknown)
                return first;
            if (first.kind == LatticeKind::Overdefined || second.kind == LatticeKind::Overdefined ||
                first.value != second.value)
                return {LatticeKind::Overdefined, 0};
            return first;
        }
        void sparse_constants(IRFunction &function, const ControlFlow &cfg) {
            std::vector<Lattice> values(function.values.size());
            std::vector<bool> executable(function.blocks.size(), false);
            std::vector<std::vector<uint32_t>> executable_edges(function.blocks.size());
            executable[0] = true;
            bool changed = true;
            uint64_t work = 0;
            while (changed) {
                changed = false;
                auto edge = [&](uint32_t from, uint32_t to) {
                    auto &edges = executable_edges[from];
                    if (std::find(edges.begin(), edges.end(), to) == edges.end()) {
                        edges.push_back(to);
                        executable[to] = true;
                        changed = true;
                    }
                };
                for (auto index : cfg.reverse_postorder) {
                    if (!executable[index])
                        continue;
                    auto &block = function.blocks[index];
                    for (const auto &instruction : block.instructions) {
                        if (++work > 8'000'000)
                            throw std::runtime_error("SCCP exceeds work budget");
                        if (instruction.result == no_id)
                            continue;
                        Lattice candidate;
                        if (instruction.op == IROp::Constant) {
                            candidate = {LatticeKind::Constant, instruction.constant};
                        } else if (instruction.op == IROp::Phi) {
                            for (size_t input = 0; input < instruction.arguments.size(); ++input) {
                                auto from = instruction.incoming_blocks[input];
                                const auto &edges = executable_edges[from];
                                if (std::find(edges.begin(), edges.end(), index) != edges.end())
                                    candidate = meet(candidate, values[instruction.arguments[input]]);
                            }
                        } else if (instruction.op == IROp::Copy) {
                            candidate = values[instruction.arguments[0]];
                        } else if (instruction.op == IROp::Cast || instruction.op == IROp::Unary ||
                                   instruction.op == IROp::Binary) {
                            candidate = {LatticeKind::Constant, 0};
                            for (auto argument : instruction.arguments) {
                                if (values[argument].kind == LatticeKind::Overdefined) {
                                    candidate = {LatticeKind::Overdefined, 0};
                                    break;
                                }
                                if (values[argument].kind == LatticeKind::Unknown)
                                    candidate.kind = LatticeKind::Unknown;
                            }
                            if (candidate.kind == LatticeKind::Constant) {
                                try {
                                    candidate.value = evaluate_ir_constant(
                                        instruction, values[instruction.arguments[0]].value,
                                        instruction.arguments.size() == 2
                                            ? values[instruction.arguments[1]].value
                                            : 0);
                                } catch (const std::runtime_error &) {
                                    candidate = {LatticeKind::Overdefined, 0};
                                }
                            }
                        } else {
                            candidate = {LatticeKind::Overdefined, 0};
                        }
                        auto next = meet(values[instruction.result], candidate);
                        if (next != values[instruction.result]) {
                            values[instruction.result] = next;
                            changed = true;
                        }
                    }
                    const auto &terminator = block.terminator;
                    if (terminator.kind == TerminatorKind::Jump)
                        edge(index, terminator.first);
                    if (terminator.kind == TerminatorKind::Branch) {
                        const auto condition = values[terminator.value];
                        if (condition.kind == LatticeKind::Constant)
                            edge(index, condition.value ? terminator.first : terminator.second);
                        if (condition.kind == LatticeKind::Overdefined) {
                            edge(index, terminator.first);
                            edge(index, terminator.second);
                        }
                    }
                }
            }
            for (auto &block : function.blocks) {
                for (auto &instruction : block.instructions) {
                    if (instruction.result == no_id || instruction.op == IROp::Constant)
                        continue;
                    const auto value = values[instruction.result];
                    if (value.kind == LatticeKind::Constant) {
                        instruction.op = IROp::Constant;
                        instruction.constant = value.value;
                        instruction.arguments.clear();
                        instruction.incoming_blocks.clear();
                        instruction.operation.clear();
                        instruction.index = 0;
                    }
                }
                auto &terminator = block.terminator;
                if (terminator.kind == TerminatorKind::Branch &&
                    values[terminator.value].kind == LatticeKind::Constant) {
                    terminator.first = values[terminator.value].value ? terminator.first : terminator.second;
                    terminator.kind = TerminatorKind::Jump;
                    terminator.value = terminator.second = no_id;
                }
                // Folded phi nodes become ordinary instructions; the remaining
                // edge-selected definitions still have to lead the block.
                std::stable_partition(block.instructions.begin(), block.instructions.end(),
                                      [](const auto &instruction) { return instruction.op == IROp::Phi; });
            }
            remove_unreachable_blocks(function);
        }

        void simplify_phis(IRFunction &function) {
            std::vector<uint32_t> aliases(function.values.size(), no_id);
            auto resolve = [&](uint32_t value) {
                while (aliases[value] != no_id)
                    value = aliases[value];
                return value;
            };
            bool changed = true;
            uint64_t work = 0;
            while (changed) {
                changed = false;
                for (auto &block : function.blocks) {
                    for (auto &instruction : block.instructions) {
                        if (++work > 8'000'000)
                            throw std::runtime_error("phi simplification exceeds work budget");
                        if (instruction.op != IROp::Phi || aliases[instruction.result] != no_id)
                            continue;
                        auto candidate = no_id;
                        bool conflict = false;
                        for (auto argument : instruction.arguments) {
                            argument = resolve(argument);
                            if (argument == instruction.result)
                                continue;
                            if (candidate == no_id)
                                candidate = argument;
                            else if (candidate != argument)
                                conflict = true;
                        }
                        if (candidate != no_id && !conflict) {
                            aliases[instruction.result] = candidate;
                            changed = true;
                        }
                    }
                }
            }
            for (auto &block : function.blocks) {
                std::erase_if(block.instructions, [&](const auto &instruction) {
                    return instruction.op == IROp::Phi && aliases[instruction.result] != no_id;
                });
                for (auto &instruction : block.instructions)
                    for (auto &argument : instruction.arguments)
                        argument = resolve(argument);
                if (block.terminator.value != no_id)
                    block.terminator.value = resolve(block.terminator.value);
            }
        }
        void merge_linear_blocks(IRFunction &function, ControlFlow cfg) {
            std::vector<uint32_t> redirects(function.blocks.size(), no_id);
            auto resolve = [&](uint32_t block) {
                while (redirects[block] != no_id)
                    block = redirects[block];
                return block;
            };
            for (auto &block : function.blocks) {
                if (redirects[block.id] != no_id)
                    continue;
                while (block.terminator.kind == TerminatorKind::Jump) {
                    const auto target = block.terminator.first;
                    if (target == 0 || target == block.id || redirects[target] != no_id ||
                        cfg.predecessors[target] != std::vector<uint32_t>{block.id})
                        break;
                    auto &next = function.blocks[target];
                    if (!next.instructions.empty() && next.instructions.front().op == IROp::Phi)
                        break;
                    block.instructions.insert(block.instructions.end(),
                                              std::make_move_iterator(next.instructions.begin()),
                                              std::make_move_iterator(next.instructions.end()));
                    block.terminator = next.terminator;
                    redirects[target] = block.id;
                    cfg.successors[block.id] = cfg.successors[target];
                    for (auto successor : cfg.successors[target]) {
                        auto &incoming = cfg.predecessors[successor];
                        for (auto &predecessor : incoming)
                            if (predecessor == target)
                                predecessor = block.id;
                        std::sort(incoming.begin(), incoming.end());
                        incoming.erase(std::unique(incoming.begin(), incoming.end()), incoming.end());
                    }
                }
            }
            for (auto &block : function.blocks) {
                if (redirects[block.id] != no_id)
                    continue;
                for (auto &instruction : block.instructions)
                    for (auto &predecessor : instruction.incoming_blocks)
                        predecessor = resolve(predecessor);
            }
            remove_unreachable_blocks(function);
        }
        void eliminate_common_values(IRFunction &function, const ControlFlow &cfg) {
            std::vector<uint32_t> aliases(function.values.size(), no_id);
            auto resolve = [&](uint32_t value) {
                while (aliases[value] != no_id)
                    value = aliases[value];
                return value;
            };
            std::map<std::string, uint32_t> available;
            struct Undo {
                std::string key;
                std::optional<uint32_t> previous;
            };
            struct Visit {
                uint32_t block;
                bool leave;
                std::vector<Undo> undo;
            };
            std::vector<Visit> visits{{0, false, {}}};
            while (!visits.empty()) {
                auto visit = std::move(visits.back());
                visits.pop_back();
                if (visit.leave) {
                    for (auto undo = visit.undo.rbegin(); undo != visit.undo.rend(); ++undo) {
                        if (undo->previous)
                            available[undo->key] = *undo->previous;
                        else
                            available.erase(undo->key);
                    }
                    continue;
                }
                auto &block = function.blocks[visit.block];
                std::vector<IRInstruction> instructions;
                for (auto instruction : block.instructions) {
                    if (instruction.op != IROp::Phi)
                        for (auto &argument : instruction.arguments)
                            argument = resolve(argument);
                    if (instruction.op == IROp::Copy) {
                        aliases[instruction.result] = instruction.arguments[0];
                        continue;
                    }
                    const bool eligible = instruction.op != IROp::Phi && instruction.op != IROp::Load &&
                                          instruction.result != no_id && ir_operation_is_total(instruction);
                    if (eligible) {
                        std::string key = std::to_string(static_cast<unsigned>(instruction.op)) + ":" +
                                          std::to_string(static_cast<unsigned>(instruction.type)) + ":" +
                                          std::to_string(instruction.index) + ":" +
                                          std::to_string(instruction.constant) + ":" + instruction.operation;
                        for (auto argument : instruction.arguments)
                            key += ":" + std::to_string(argument);
                        const auto existing = available.find(key);
                        if (existing != available.end()) {
                            aliases[instruction.result] = existing->second;
                            continue;
                        }
                        visit.undo.push_back({key, std::nullopt});
                        available.emplace(std::move(key), instruction.result);
                    }
                    instructions.push_back(std::move(instruction));
                }
                block.instructions = std::move(instructions);
                if (block.terminator.value != no_id)
                    block.terminator.value = resolve(block.terminator.value);
                visit.leave = true;
                visits.push_back(std::move(visit));
                for (auto child = cfg.children[block.id].rbegin(); child != cfg.children[block.id].rend();
                     ++child)
                    visits.push_back({*child, false, {}});
            }
            for (auto &block : function.blocks) {
                for (auto &instruction : block.instructions)
                    for (auto &argument : instruction.arguments)
                        argument = resolve(argument);
            }
        }

        void eliminate_dead_values(IRFunction &function, const ModuleEffects &effects,
                                   const DefUseAnalysis &def_use, const RangeAnalysis &ranges) {
            auto removable = [&](const IRInstruction &instruction) {
                if (instruction.op == IROp::Call)
                    return instruction.index < effects.functions.size() &&
                           effects.functions[instruction.index].can_discard();
                return ir_operation_is_total(instruction) ||
                       (instruction.result != no_id && ranges.proven_total[instruction.result]);
            };
            std::vector<const IRInstruction *> definitions(function.values.size(), nullptr);
            for (size_t value = 0; value < def_use.definitions.size(); ++value) {
                const auto &location = def_use.definitions[value];
                if (location.block != no_id)
                    definitions[value] = &function.blocks[location.block].instructions[location.instruction];
            }
            std::vector<bool> live(function.values.size(), false);
            std::vector<uint32_t> work;
            for (const auto &block : function.blocks) {
                if (block.terminator.value != no_id)
                    work.push_back(block.terminator.value);
                for (const auto &instruction : block.instructions) {
                    if (!removable(instruction)) {
                        if (instruction.result != no_id)
                            work.push_back(instruction.result);
                        work.insert(work.end(), instruction.arguments.begin(), instruction.arguments.end());
                    }
                }
            }
            while (!work.empty()) {
                auto value = work.back();
                work.pop_back();
                if (live[value])
                    continue;
                live[value] = true;
                const auto definition = definitions[value];
                if (definition)
                    work.insert(work.end(), definition->arguments.begin(), definition->arguments.end());
            }
            for (auto &block : function.blocks)
                std::erase_if(block.instructions, [&](const auto &instruction) {
                    return instruction.result != no_id && !live[instruction.result] && removable(instruction);
                });
        }
    } // namespace

    void IRPassManager::add(std::string name, Transformation transformation) {
        if (!transformation)
            throw std::invalid_argument("invalid IR pass");
        add(std::move(name),
            [transformation = std::move(transformation)](IRModule &module, IRAnalysisManager &) {
                transformation(module);
                return PreservedAnalyses{0};
            });
    }
    void IRPassManager::add(std::string name, AnalyzedTransformation transformation) {
        if (name.empty() || !transformation)
            throw std::invalid_argument("invalid IR pass");
        for (const auto &pass : passes_)
            if (pass.first == name)
                throw std::invalid_argument("duplicate IR pass name");
        passes_.emplace_back(std::move(name), std::move(transformation));
    }
    IRPassFailure::IRPassFailure(PassRecord record)
        : std::runtime_error("IR pass " + record.name + " failed: " + record.failure),
          record_(std::move(record)) {
    }
    const PassRecord &IRPassFailure::record() const {
        return record_;
    }
    OptimizationReport IRPassManager::run(IRModule &module, PassRunOptions options,
                                          const std::string &only) const {
        if (!only.empty() && std::none_of(passes_.begin(), passes_.end(),
                                          [&](const auto &pass) { return pass.first == only; }))
            throw std::invalid_argument("unknown IR pass: " + only);
        if (options.snapshot_budget > 256 * 1024 * 1024)
            throw std::invalid_argument("IR trace budget exceeds 256 MiB");
        auto errors = verify_ir(module);
        if (!errors.empty())
            throw std::runtime_error("invalid pass input: " + errors.front().message);
        OptimizationReport report;
        IRAnalysisManager analyses(module);
        size_t captured_bytes = 0;
        for (const auto &[name, transformation] : passes_) {
            if (!only.empty() && name != only)
                continue;
            auto candidate = module;
            IRAnalysisManager candidate_analyses(candidate, analyses);
            PassRecord record;
            record.name = name;
            record.before = measure(module);
            record.before_fingerprint = ir_fingerprint(module);
            if (options.capture_snapshots) {
                record.before_snapshot = serialize_ir_snapshot(module);
                if (record.before_snapshot.size() >
                    options.snapshot_budget - std::min(captured_bytes, options.snapshot_budget))
                    throw std::runtime_error("IR trace exceeds snapshot budget");
                captured_bytes += record.before_snapshot.size();
            }
            auto start = std::chrono::steady_clock::now();
            try {
                record.preserved = transformation(candidate, candidate_analyses);
                errors = verify_ir(candidate);
                if (!errors.empty())
                    throw std::runtime_error("verification: " + errors.front().message);
                candidate_analyses.finish_pass(module, record.preserved);
                record.after_fingerprint = ir_fingerprint(candidate);
                record.changes = compare_ir(module, candidate);
                record.after = measure(candidate);
                if (options.capture_snapshots) {
                    record.after_snapshot = serialize_ir_snapshot(candidate);
                    if (record.after_snapshot.size() >
                        options.snapshot_budget - std::min(captured_bytes, options.snapshot_budget))
                        throw std::runtime_error("IR trace exceeds snapshot budget");
                    captured_bytes += record.after_snapshot.size();
                }
            } catch (const std::exception &error) {
                record.failure = error.what();
                record.analyses = candidate_analyses.statistics();
                // Best-effort candidate capture cannot replace the original failure.
                if (options.capture_snapshots && record.after_snapshot.empty()) {
                    try {
                        auto bytes = serialize_ir_snapshot(candidate);
                        if (bytes.size() <=
                            options.snapshot_budget - std::min(captured_bytes, options.snapshot_budget))
                            record.after_snapshot = std::move(bytes);
                    } catch (const std::exception &) {
                    }
                }
                throw IRPassFailure(std::move(record));
            }
            record.elapsed_microseconds =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count());
            record.analyses = candidate_analyses.statistics();
            module = std::move(candidate);
            analyses.adopt_cache(candidate_analyses);
            report.passes.push_back(std::move(record));
        }
        return report;
    }
    OptimizationReport run_optimization_pipeline(IRModule &module, PassRunOptions options,
                                                 const std::string &only) {
        IRPassManager manager;
        manager.add("reachable-cfg", [](auto &candidate) {
            for (auto &function : candidate.functions)
                remove_unreachable_blocks(function);
        });
        manager.add("promote-locals-to-ssa", promote_locals_to_ssa);
        manager.add("sparse-conditional-constants", [](IRModule &candidate, IRAnalysisManager &analyses) {
            for (uint32_t index = 0; index < candidate.functions.size(); ++index)
                sparse_constants(candidate.functions[index], *analyses.control_flow(index));
            return PreservedAnalyses{0};
        });
        manager.add("simplify-phi-values", [](IRModule &candidate, IRAnalysisManager &) {
            for (auto &function : candidate.functions)
                simplify_phis(function);
            return preserve_cfg;
        });
        manager.add("dominator-common-values", [](IRModule &candidate, IRAnalysisManager &analyses) {
            for (uint32_t index = 0; index < candidate.functions.size(); ++index)
                eliminate_common_values(candidate.functions[index], *analyses.control_flow(index));
            return preserve_cfg;
        });
        manager.add("effect-aware-dead-values", [](IRModule &candidate, IRAnalysisManager &analyses) {
            const auto effects = analyses.effects();
            for (uint32_t index = 0; index < candidate.functions.size(); ++index)
                eliminate_dead_values(candidate.functions[index], *effects, *analyses.def_use(index),
                                      *analyses.ranges(index));
            return preserve_cfg;
        });
        manager.add("merge-linear-cfg", [](IRModule &candidate, IRAnalysisManager &analyses) {
            for (uint32_t index = 0; index < candidate.functions.size(); ++index)
                merge_linear_blocks(candidate.functions[index], *analyses.control_flow(index));
            return PreservedAnalyses{0};
        });
        return manager.run(module, options, only);
    }
    void optimize_ir(IRModule &module) {
        (void)run_optimization_pipeline(module);
    }
    std::string optimization_report_json(const OptimizationReport &report) {
        auto size = [](const IRSize &value) {
            return "{\"blocks\":" + std::to_string(value.blocks) +
                   ",\"instructions\":" + std::to_string(value.instructions) +
                   ",\"locals\":" + std::to_string(value.locals) + ",\"phis\":" + std::to_string(value.phis) +
                   "}";
        };
        std::string result =
            "{\"schema_version\":1,\"reused\":" + std::string(report.reused ? "true" : "false") +
            ",\"passes\":[";
        for (size_t index = 0; index < report.passes.size(); ++index) {
            if (index)
                result += ',';
            const auto &pass = report.passes[index];
            result += "{\"name\":" + json_quote(pass.name) + ",\"before\":" + size(pass.before) +
                      ",\"after\":" + size(pass.after) +
                      ",\"elapsed_us\":" + std::to_string(pass.elapsed_microseconds) +
                      ",\"before_fingerprint\":" + json_quote(pass.before_fingerprint) +
                      ",\"after_fingerprint\":" + json_quote(pass.after_fingerprint) +
                      ",\"preserved_analyses\":" + std::to_string(pass.preserved) +
                      ",\"changes\":" + ir_changes_json(pass.changes) +
                      ",\"analyses\":" + analysis_statistics_json(pass.analyses) + "}";
        }
        return result + "]}";
    }
    std::string optimization_trace_json(const OptimizationReport &report) {
        std::string result =
            "{\"schema_version\":1,\"kind\":\"mplx.optimization-trace\",\"snapshot_version\":1,\"report\":" +
            optimization_report_json(report) + ",\"snapshots\":[";
        for (const auto &pass : report.passes) {
            if (pass.before_snapshot.empty() || pass.after_snapshot.empty())
                throw std::runtime_error("optimization snapshots were not captured");
            if (result.back() != '[')
                result += ',';
            result += "{\"pass\":" + json_quote(pass.name) +
                      ",\"before_ir\":" + dump_ir_json(deserialize_ir_snapshot(pass.before_snapshot)) +
                      ",\"after_ir\":" + dump_ir_json(deserialize_ir_snapshot(pass.after_snapshot)) +
                      ",\"before_snapshot_hex\":" + json_quote(snapshot_hex(pass.before_snapshot)) +
                      ",\"after_snapshot_hex\":" + json_quote(snapshot_hex(pass.after_snapshot)) + "}";
        }
        return result + "]}";
    }
    std::string optimization_failure_json(const IRPassFailure &failure) {
        const auto &record = failure.record();
        return "{\"schema_version\":1,\"kind\":\"mplx.pass-failure\",\"pass\":" + json_quote(record.name) +
               ",\"message\":" + json_quote(record.failure) +
               ",\"before_snapshot_hex\":" + json_quote(snapshot_hex(record.before_snapshot)) +
               ",\"candidate_snapshot_hex\":" + json_quote(snapshot_hex(record.after_snapshot)) + "}";
    }
} // namespace mplx
