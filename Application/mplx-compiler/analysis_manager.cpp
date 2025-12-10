#include "analysis_manager.hpp"
#include "ir_snapshot.hpp"
#include <algorithm>
#include <deque>
#include <numeric>
#include <stdexcept>
#include <type_traits>

namespace mplx {
    namespace {
        constexpr uint64_t work_limit = 8'000'000;
        struct Budget {
            uint64_t work{0};
            void spend(uint64_t amount = 1) {
                if (amount > work_limit - work)
                    throw std::runtime_error("IR analysis exceeds work budget");
                work += amount;
            }
        };
        using Graph = std::vector<std::vector<uint32_t>>;
        std::vector<std::vector<uint32_t>> components(const Graph &graph, const std::vector<bool> &active) {
            Graph reversed(graph.size());
            for (uint32_t from = 0; from < graph.size(); ++from)
                if (active[from])
                    for (auto to : graph[from]) {
                        if (to >= graph.size())
                            throw std::runtime_error("invalid analysis graph edge");
                        if (active[to])
                            reversed[to].push_back(from);
                    }
            struct Visit {
                uint32_t block;
                size_t next;
            };
            std::vector<bool> seen(graph.size(), false);
            std::vector<uint32_t> order;
            for (uint32_t root = 0; root < graph.size(); ++root) {
                if (!active[root] || seen[root])
                    continue;
                std::vector<Visit> visits{{root, 0}};
                seen[root] = true;
                while (!visits.empty()) {
                    auto &visit = visits.back();
                    if (visit.next < graph[visit.block].size()) {
                        const auto target = graph[visit.block][visit.next++];
                        if (active[target] && !seen[target]) {
                            seen[target] = true;
                            visits.push_back({target, 0});
                        }
                    } else {
                        order.push_back(visit.block);
                        visits.pop_back();
                    }
                }
            }
            std::fill(seen.begin(), seen.end(), false);
            std::vector<std::vector<uint32_t>> result;
            for (auto root = order.rbegin(); root != order.rend(); ++root) {
                if (seen[*root])
                    continue;
                std::vector<uint32_t> members, pending{*root};
                seen[*root] = true;
                while (!pending.empty()) {
                    const auto block = pending.back();
                    pending.pop_back();
                    members.push_back(block);
                    for (auto predecessor : reversed[block])
                        if (!seen[predecessor]) {
                            seen[predecessor] = true;
                            pending.push_back(predecessor);
                        }
                }
                std::sort(members.begin(), members.end());
                result.push_back(std::move(members));
            }
            return result;
        }
        std::string graph_key(const IRFunction &function) {
            std::string result;
            auto number = [&](uint32_t value) {
                for (unsigned byte = 0; byte < 4; ++byte)
                    result.push_back(static_cast<char>(value >> (8 * byte)));
            };
            number(static_cast<uint32_t>(function.blocks.size()));
            for (const auto &block : function.blocks) {
                number(block.id);
                number(static_cast<uint32_t>(block.terminator.kind));
                // Values and source spans do not change the graph topology.
                number(block.terminator.first);
                number(block.terminator.second);
            }
            return result;
        }
        std::string analysis_key(const IRModule &module, AnalysisKind kind, uint32_t function) {
            if (kind == AnalysisKind::Effects)
                return serialize_ir_snapshot(module, ir_content_limit);
            if (function >= module.functions.size())
                throw std::out_of_range("analysis function index");
            if (kind == AnalysisKind::ControlFlow || kind == AnalysisKind::PostDominance ||
                kind == AnalysisKind::Loops)
                return graph_key(module.functions[function]);
            return serialize_ir_snapshot(IRModule{{module.functions[function]}}, ir_content_limit);
        }
        const char *analysis_name(AnalysisKind kind) {
            static constexpr const char *names[]{"cfg",      "postdominance", "loops", "def-use",
                                                 "liveness", "effects",       "ranges"};
            return names[static_cast<unsigned>(kind)];
        }
        template <class T> std::string array_json(const std::vector<T> &values) {
            std::string result = "[";
            for (size_t index = 0; index < values.size(); ++index) {
                if (index)
                    result += ',';
                if constexpr (std::is_same_v<T, bool>)
                    result += values[index] ? "true" : "false";
                else
                    result += values[index] == no_id ? "null" : std::to_string(values[index]);
            }
            return result + "]";
        }
        std::string sets_json(const std::vector<std::set<uint32_t>> &sets) {
            std::string result = "[";
            for (const auto &set : sets) {
                if (result.size() > 1)
                    result += ',';
                result += array_json(std::vector<uint32_t>(set.begin(), set.end()));
            }
            return result + "]";
        }
    } // namespace

    bool PostDominance::postdominates(uint32_t definition, uint32_t use) const {
        return definition < reaches_exit.size() && use < reaches_exit.size() && reaches_exit[definition] &&
               reaches_exit[use] && enter[definition] <= enter[use] && leave[use] <= leave[definition];
    }
    PostDominance analyze_postdominance(const IRFunction &function, const ControlFlow &cfg) {
        const auto count = static_cast<uint32_t>(function.blocks.size());
        PostDominance result;
        result.virtual_exit = count;
        result.immediate.assign(count + 1, no_id);
        result.enter.assign(count + 1, no_id);
        result.leave.assign(count + 1, no_id);
        result.reaches_exit.assign(count + 1, false);
        Graph graph = cfg.predecessors, incoming = cfg.successors;
        graph.emplace_back();
        incoming.emplace_back();
        for (const auto &block : function.blocks)
            if (cfg.reachable[block.id] && block.terminator.kind == TerminatorKind::Return) {
                graph[count].push_back(block.id);
                incoming[block.id].push_back(count);
            }
        struct Visit {
            uint32_t block;
            size_t next;
        };
        std::vector<Visit> stack{{count, 0}};
        std::vector<uint32_t> order;
        result.reaches_exit[count] = true;
        while (!stack.empty()) {
            auto &visit = stack.back();
            if (visit.next < graph[visit.block].size()) {
                const auto next = graph[visit.block][visit.next++];
                if (cfg.reachable[next] && !result.reaches_exit[next]) {
                    result.reaches_exit[next] = true;
                    stack.push_back({next, 0});
                }
            } else {
                order.push_back(visit.block);
                stack.pop_back();
            }
        }
        std::reverse(order.begin(), order.end());
        std::vector<uint32_t> position(count + 1, no_id);
        for (uint32_t index = 0; index < order.size(); ++index)
            position[order[index]] = index;
        result.immediate[count] = count;
        Budget budget;
        auto intersect = [&](uint32_t first, uint32_t second) {
            while (first != second) {
                budget.spend();
                while (position[first] > position[second]) {
                    budget.spend();
                    first = result.immediate[first];
                }
                while (position[second] > position[first]) {
                    budget.spend();
                    second = result.immediate[second];
                }
            }
            return first;
        };
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto block : order) {
                if (block == count)
                    continue;
                budget.spend();
                uint32_t dominator = no_id;
                for (auto from : incoming[block])
                    if (result.immediate[from] != no_id)
                        dominator = dominator == no_id ? from : intersect(dominator, from);
                if (dominator != result.immediate[block]) {
                    result.immediate[block] = dominator;
                    changed = true;
                }
            }
        }
        Graph children(count + 1);
        for (auto block : order)
            if (block != count)
                children[result.immediate[block]].push_back(block);
        stack = {{count, 0}};
        uint32_t clock = 0;
        result.enter[count] = clock++;
        while (!stack.empty()) {
            auto &visit = stack.back();
            if (visit.next < children[visit.block].size()) {
                const auto child = children[visit.block][visit.next++];
                result.enter[child] = clock++;
                stack.push_back({child, 0});
            } else {
                result.leave[visit.block] = clock++;
                stack.pop_back();
            }
        }
        return result;
    }
    LoopAnalysis analyze_loops(const IRFunction &function, const ControlFlow &cfg) {
        LoopAnalysis result;
        result.components = components(cfg.successors, cfg.reachable);
        result.component_of.assign(function.blocks.size(), no_id);
        result.cyclic.resize(result.components.size(), false);
        result.irreducible.resize(result.components.size(), false);
        for (uint32_t index = 0; index < result.components.size(); ++index)
            for (auto block : result.components[index])
                result.component_of[block] = index;
        for (uint32_t index = 0; index < result.components.size(); ++index) {
            const auto &members = result.components[index];
            const auto first = members.front();
            result.cyclic[index] =
                members.size() > 1 || std::find(cfg.successors[first].begin(), cfg.successors[first].end(),
                                                first) != cfg.successors[first].end();
            std::set<uint32_t> entries;
            for (auto block : members) {
                if (block == 0)
                    entries.insert(block);
                for (auto from : cfg.predecessors[block])
                    if (cfg.reachable[from] && result.component_of[from] != index)
                        entries.insert(block);
            }
            result.irreducible[index] = result.cyclic[index] && entries.size() > 1;
        }
        // An outer SCC can have one entry and still contain an irreducible inner
        // cycle. Classify DFS backedges as well; reducible backedges target a
        // dominator, independently of the selected DFS traversal.
        struct Visit {
            uint32_t block;
            size_t next;
        };
        std::vector<uint8_t> color(function.blocks.size(), 0);
        std::vector<Visit> visits{{0, 0}};
        color[0] = 1;
        while (!visits.empty()) {
            auto &visit = visits.back();
            if (visit.next < cfg.successors[visit.block].size()) {
                const auto target = cfg.successors[visit.block][visit.next++];
                if (color[target] == 1 && !cfg.dominates(target, visit.block))
                    result.irreducible[result.component_of[target]] = true;
                if (color[target] == 0) {
                    color[target] = 1;
                    visits.push_back({target, 0});
                }
            } else {
                color[visit.block] = 2;
                visits.pop_back();
            }
        }
        std::map<uint32_t, std::set<uint32_t>> members, latches;
        Budget budget;
        for (auto from : cfg.reverse_postorder)
            for (auto header : cfg.successors[from]) {
                if (!cfg.dominates(header, from))
                    continue;
                auto &loop = members[header];
                loop.insert(header);
                latches[header].insert(from);
                std::vector<uint32_t> pending;
                if (loop.insert(from).second)
                    pending.push_back(from);
                while (!pending.empty()) {
                    const auto block = pending.back();
                    pending.pop_back();
                    for (auto predecessor : cfg.predecessors[block]) {
                        budget.spend();
                        if (cfg.reachable[predecessor] && cfg.dominates(header, predecessor) &&
                            loop.insert(predecessor).second)
                            pending.push_back(predecessor);
                    }
                }
            }
        for (const auto &[header, blocks] : members) {
            NaturalLoop loop;
            loop.header = header;
            loop.blocks.assign(blocks.begin(), blocks.end());
            loop.latches.assign(latches[header].begin(), latches[header].end());
            for (auto block : blocks)
                for (auto to : cfg.successors[block])
                    if (!blocks.contains(to))
                        loop.exits.emplace_back(block, to);
            result.loops.push_back(std::move(loop));
        }
        for (uint32_t child = 0; child < result.loops.size(); ++child)
            for (uint32_t parent = 0; parent < result.loops.size(); ++parent) {
                const auto &outer = result.loops[parent].blocks;
                const auto &inner = result.loops[child].blocks;
                budget.spend(outer.size() + inner.size());
                if (outer.size() > inner.size() &&
                    std::includes(outer.begin(), outer.end(), inner.begin(), inner.end()) &&
                    (result.loops[child].parent == no_id ||
                     outer.size() < result.loops[result.loops[child].parent].blocks.size()))
                    result.loops[child].parent = parent;
            }
        for (auto &loop : result.loops)
            for (auto parent = loop.parent; parent != no_id; parent = result.loops[parent].parent)
                ++loop.depth;
        return result;
    }
    DefUseAnalysis analyze_def_use(const IRFunction &function) {
        DefUseAnalysis result;
        result.definitions.resize(function.values.size());
        result.uses.resize(function.values.size());
        for (const auto &block : function.blocks) {
            for (uint32_t index = 0; index < block.instructions.size(); ++index) {
                const auto &instruction = block.instructions[index];
                if (instruction.result != no_id)
                    result.definitions.at(instruction.result) = {block.id, index, no_id};
                for (size_t input = 0; input < instruction.arguments.size(); ++input)
                    result.uses.at(instruction.arguments[input])
                        .push_back(
                            {block.id, index,
                             instruction.op == IROp::Phi ? instruction.incoming_blocks.at(input) : no_id});
            }
            if (block.terminator.value != no_id)
                result.uses.at(block.terminator.value).push_back({block.id, no_id, no_id});
        }
        return result;
    }
    ValueLiveness analyze_liveness(const IRFunction &function, const ControlFlow &cfg) {
        const auto count = function.blocks.size();
        ValueLiveness result;
        result.live_in.resize(count);
        result.live_out.resize(count);
        std::vector<std::set<uint32_t>> definitions(count), uses(count), phis(count);
        for (const auto &block : function.blocks) {
            for (const auto &instruction : block.instructions) {
                if (instruction.op == IROp::Phi)
                    phis[block.id].insert(instruction.result);
                else
                    for (auto input : instruction.arguments)
                        if (!definitions[block.id].contains(input))
                            uses[block.id].insert(input);
                if (instruction.result != no_id)
                    definitions[block.id].insert(instruction.result);
            }
            if (block.terminator.value != no_id && !definitions[block.id].contains(block.terminator.value))
                uses[block.id].insert(block.terminator.value);
        }
        Budget budget;
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto item = cfg.reverse_postorder.rbegin(); item != cfg.reverse_postorder.rend(); ++item) {
                const auto block = *item;
                std::set<uint32_t> out;
                for (auto to : cfg.successors[block]) {
                    auto edge = result.live_in[to];
                    for (auto phi : phis[to])
                        edge.erase(phi);
                    for (const auto &instruction : function.blocks[to].instructions)
                        if (instruction.op == IROp::Phi)
                            for (size_t index = 0; index < instruction.arguments.size(); ++index)
                                if (instruction.incoming_blocks[index] == block)
                                    edge.insert(instruction.arguments[index]);
                    budget.spend(edge.size() + 1);
                    out.insert(edge.begin(), edge.end());
                    result.edge_live[{block, to}] = std::move(edge);
                }
                auto in = out;
                for (auto definition : definitions[block])
                    in.erase(definition);
                in.insert(uses[block].begin(), uses[block].end());
                budget.spend(in.size() + definitions[block].size() + 1);
                if (in != result.live_in[block] || out != result.live_out[block]) {
                    result.live_in[block] = std::move(in);
                    result.live_out[block] = std::move(out);
                    changed = true;
                }
            }
        }
        return result;
    }
    bool EffectSummary::can_discard() const {
        return !may_trap && !may_diverge && !reads_external && !writes_external && !unknown_call;
    }
    EffectSummary instruction_effects(const IRInstruction &instruction) {
        EffectSummary result;
        result.reads_local = instruction.op == IROp::Load;
        result.writes_local = instruction.op == IROp::Store;
        result.unknown_call = instruction.op == IROp::Call;
        result.may_trap = !ir_operation_is_total(instruction) && instruction.op != IROp::Store &&
                          instruction.op != IROp::Call;
        // An unrecognized future opcode cannot silently become a pure operation.
        if (static_cast<unsigned>(instruction.op) > static_cast<unsigned>(IROp::Copy)) {
            result.unknown_call = result.reads_external = result.writes_external = true;
            result.may_trap = result.may_diverge = true;
        }
        return result;
    }
    static ModuleEffects compute_effects(const IRModule &module, IRAnalysisManager *analyses) {
        const auto count = module.functions.size();
        ModuleEffects result;
        result.callees.resize(count);
        result.callers.resize(count);
        result.recursive.resize(count, false);
        result.functions.resize(count);
        for (uint32_t index = 0; index < count; ++index) {
            const auto cfg =
                analyses ? analyses->control_flow(index)
                         : std::make_shared<const ControlFlow>(analyze_control_flow(module.functions[index]));
            const auto loops =
                analyses ? analyses->loops(index)
                         : std::make_shared<const LoopAnalysis>(analyze_loops(module.functions[index], *cfg));
            auto &effect = result.functions[index];
            const auto ranges =
                analyses
                    ? analyses->ranges(index)
                    : std::make_shared<const RangeAnalysis>(analyze_integer_ranges(module.functions[index]));
            effect.may_diverge =
                std::find(loops->cyclic.begin(), loops->cyclic.end(), true) != loops->cyclic.end();
            std::set<uint32_t> targets;
            for (auto block : cfg->reverse_postorder)
                for (const auto &instruction : module.functions[index].blocks[block].instructions) {
                    auto local = instruction_effects(instruction);
                    if (instruction.result != no_id && ranges->proven_total[instruction.result])
                        local.may_trap = false;
                    effect.reads_local |= local.reads_local;
                    effect.writes_local |= local.writes_local;
                    effect.may_trap |= local.may_trap;
                    if (instruction.op == IROp::Call) {
                        if (instruction.index < count)
                            targets.insert(instruction.index);
                        else
                            effect.unknown_call = effect.may_trap = effect.may_diverge = true;
                    } else {
                        effect.unknown_call |= local.unknown_call;
                        effect.reads_external |= local.reads_external;
                        effect.writes_external |= local.writes_external;
                    }
                }
            result.callees[index].assign(targets.begin(), targets.end());
            for (auto target : targets)
                result.callers[target].push_back(index);
        }
        result.components = components(result.callees, std::vector<bool>(count, true));
        for (const auto &component : result.components) {
            const auto first = component.front();
            const bool recursive =
                component.size() > 1 || std::find(result.callees[first].begin(), result.callees[first].end(),
                                                  first) != result.callees[first].end();
            for (auto member : component) {
                result.recursive[member] = recursive;
                result.functions[member].may_diverge |= recursive;
            }
        }
        std::deque<uint32_t> pending;
        std::vector<bool> queued(count, true);
        for (uint32_t index = 0; index < count; ++index)
            pending.push_back(index);
        Budget budget;
        while (!pending.empty()) {
            const auto callee = pending.front();
            pending.pop_front();
            queued[callee] = false;
            for (auto caller : result.callers[callee]) {
                budget.spend();
                const auto before = result.functions[caller];
                auto &next = result.functions[caller];
                const auto &dependency = result.functions[callee];
                // A callee's frame-local reads/writes are not caller-visible memory.
                next.may_trap |= dependency.may_trap;
                next.may_diverge |= dependency.may_diverge;
                next.reads_external |= dependency.reads_external;
                next.writes_external |= dependency.writes_external;
                next.unknown_call |= dependency.unknown_call;
                if (before != next && !queued[caller]) {
                    queued[caller] = true;
                    pending.push_back(caller);
                }
            }
        }
        return result;
    }

    ModuleEffects analyze_effects(const IRModule &module) {
        return compute_effects(module, nullptr);
    }
    IRAnalysisManager::IRAnalysisManager(const IRModule &module) : module_(module) {
    }
    IRAnalysisManager::IRAnalysisManager(const IRModule &module, const IRAnalysisManager &previous)
        : module_(module), cache_(previous.cache_) {
    }
    std::string IRAnalysisManager::key(AnalysisKind kind, uint32_t function) const {
        return analysis_key(module_, kind, function);
    }
    template <class T, class Compute>
    std::shared_ptr<const T> IRAnalysisManager::get(AnalysisKind kind, uint32_t function, Compute compute) {
        auto &statistics = statistics_[static_cast<size_t>(kind)];
        ++statistics.requests;
        auto content = key(kind, function);
        auto found = cache_.find({kind, function});
        if (found != cache_.end() && found->second.key == content) {
            ++statistics.hits;
            return std::static_pointer_cast<const T>(found->second.value);
        }
        if (found != cache_.end()) {
            cache_.erase(found);
            ++statistics.invalidations;
        }
        auto result = std::make_shared<const T>(compute());
        ++statistics.computations;
        cache_[{kind, function}] = {std::move(content), result};
        return result;
    }
    std::shared_ptr<const ControlFlow> IRAnalysisManager::control_flow(uint32_t function) {
        return get<ControlFlow>(AnalysisKind::ControlFlow, function,
                                [&] { return analyze_control_flow(module_.functions.at(function)); });
    }
    std::shared_ptr<const PostDominance> IRAnalysisManager::postdominance(uint32_t function) {
        return get<PostDominance>(AnalysisKind::PostDominance, function, [&] {
            return analyze_postdominance(module_.functions.at(function), *control_flow(function));
        });
    }
    std::shared_ptr<const LoopAnalysis> IRAnalysisManager::loops(uint32_t function) {
        return get<LoopAnalysis>(AnalysisKind::Loops, function, [&] {
            return analyze_loops(module_.functions.at(function), *control_flow(function));
        });
    }
    std::shared_ptr<const DefUseAnalysis> IRAnalysisManager::def_use(uint32_t function) {
        return get<DefUseAnalysis>(AnalysisKind::DefUse, function,
                                   [&] { return analyze_def_use(module_.functions.at(function)); });
    }
    std::shared_ptr<const ValueLiveness> IRAnalysisManager::liveness(uint32_t function) {
        return get<ValueLiveness>(AnalysisKind::Liveness, function, [&] {
            return analyze_liveness(module_.functions.at(function), *control_flow(function));
        });
    }
    std::shared_ptr<const ModuleEffects> IRAnalysisManager::effects() {
        return get<ModuleEffects>(AnalysisKind::Effects, no_id,
                                  [&] { return compute_effects(module_, this); });
    }
    std::shared_ptr<const RangeAnalysis> IRAnalysisManager::ranges(uint32_t function) {
        return get<RangeAnalysis>(AnalysisKind::Ranges, function,
                                  [&] { return analyze_integer_ranges(module_.functions.at(function)); });
    }
    void IRAnalysisManager::finish_pass(const IRModule &before, PreservedAnalyses preserved) {
        if (preserved >> static_cast<unsigned>(AnalysisKind::Count))
            throw std::runtime_error("invalid analysis preservation mask");
        for (unsigned index = 0; index < static_cast<unsigned>(AnalysisKind::Count); ++index) {
            const auto kind = static_cast<AnalysisKind>(index);
            if (!(preserved & preserve(kind)))
                continue;
            if (before.functions.size() != module_.functions.size())
                throw std::runtime_error("false preservation claim for " + std::string(analysis_name(kind)));
            const auto count = kind == AnalysisKind::Effects ? 1 : module_.functions.size();
            for (uint32_t function = 0; function < count; ++function) {
                const auto identity = kind == AnalysisKind::Effects ? no_id : function;
                if (analysis_key(before, kind, identity) != key(kind, identity))
                    throw std::runtime_error("false preservation claim for " +
                                             std::string(analysis_name(kind)));
            }
        }
        for (auto entry = cache_.begin(); entry != cache_.end();) {
            const auto [kind, function] = entry->first;
            if (!(preserved & preserve(kind)) ||
                (function != no_id && function >= module_.functions.size()) ||
                entry->second.key != key(kind, function)) {
                ++statistics_[static_cast<size_t>(kind)].invalidations;
                entry = cache_.erase(entry);
            } else
                ++entry;
        }
    }
    const AnalysisStatistics &IRAnalysisManager::statistics() const {
        return statistics_;
    }
    void IRAnalysisManager::adopt_cache(const IRAnalysisManager &previous) {
        cache_ = previous.cache_;
    }
    std::string analysis_statistics_json(const AnalysisStatistics &statistics) {
        std::string result = "{";
        for (unsigned index = 0; index < statistics.size(); ++index) {
            if (index)
                result += ',';
            const auto &item = statistics[index];
            result += json_quote(analysis_name(static_cast<AnalysisKind>(index))) +
                      ":{\"requests\":" + std::to_string(item.requests) +
                      ",\"computations\":" + std::to_string(item.computations) +
                      ",\"hits\":" + std::to_string(item.hits) +
                      ",\"invalidations\":" + std::to_string(item.invalidations) + "}";
        }
        return result + "}";
    }
    std::string dump_analyses_json(const IRModule &module) {
        const auto errors = verify_ir(module);
        if (!errors.empty())
            throw std::runtime_error("invalid analysis input: " + errors.front().message);
        IRAnalysisManager manager(module);
        const auto effects = manager.effects();
        std::string result =
            "{\"schema_version\":1,\"postdominance_model\":\"finite-exit-paths\",\"functions\":[";
        for (uint32_t index = 0; index < module.functions.size(); ++index) {
            if (index)
                result += ',';
            const auto cfg = manager.control_flow(index);
            const auto post = manager.postdominance(index);
            const auto loop = manager.loops(index);
            const auto live = manager.liveness(index);
            const auto uses = manager.def_use(index);
            const auto ranges = manager.ranges(index);
            const auto &effect = effects->functions[index];
            result += "{\"index\":" + std::to_string(index) +
                      ",\"name\":" + json_quote(module.functions[index].name) +
                      ",\"idom\":" + array_json(cfg->immediate_dominator) +
                      ",\"ipostdom\":" + array_json(post->immediate) +
                      ",\"virtual_exit\":" + std::to_string(post->virtual_exit) +
                      ",\"reaches_exit\":" + array_json(post->reaches_exit) +
                      ",\"callees\":" + array_json(effects->callees[index]) +
                      ",\"recursive\":" + (effects->recursive[index] ? "true" : "false") +
                      ",\"effects\":{\"may_trap\":" + (effect.may_trap ? "true" : "false") +
                      ",\"may_diverge\":" + (effect.may_diverge ? "true" : "false") +
                      ",\"can_discard_call\":" + (effect.can_discard() ? "true" : "false") +
                      "},\"live_in\":" + sets_json(live->live_in) +
                      ",\"live_out\":" + sets_json(live->live_out) + ",\"components\":[";
            for (size_t component = 0; component < loop->components.size(); ++component) {
                if (component)
                    result += ',';
                result += "{\"blocks\":" + array_json(loop->components[component]) +
                          ",\"cyclic\":" + (loop->cyclic[component] ? "true" : "false") +
                          ",\"irreducible\":" + (loop->irreducible[component] ? "true" : "false") + "}";
            }
            result += "],\"loops\":[";
            for (size_t item = 0; item < loop->loops.size(); ++item) {
                if (item)
                    result += ',';
                const auto &natural = loop->loops[item];
                result += "{\"header\":" + std::to_string(natural.header) + ",\"parent\":" +
                          (natural.parent == no_id ? "null" : std::to_string(natural.parent)) +
                          ",\"depth\":" + std::to_string(natural.depth) +
                          ",\"blocks\":" + array_json(natural.blocks) +
                          ",\"latches\":" + array_json(natural.latches) + "}";
            }
            result += "],\"values\":[";
            for (size_t value = 0; value < uses->definitions.size(); ++value) {
                if (value)
                    result += ',';
                const auto &location = uses->definitions[value];
                const auto range = ranges->values[value];
                result +=
                    "{\"id\":" + std::to_string(value) + ",\"definition_block\":" +
                    (location.block == no_id ? "null" : std::to_string(location.block)) +
                    ",\"uses\":" + std::to_string(uses->uses[value].size()) +
                    ",\"range_min\":" + (range.bottom ? "null" : json_quote(std::to_string(range.minimum))) +
                    ",\"range_max\":" + (range.bottom ? "null" : json_quote(std::to_string(range.maximum))) +
                    ",\"proven_total\":" + (ranges->proven_total[value] ? "true" : "false") + "}";
            }
            result += "]}";
        }
        return result + "],\"analysis_statistics\":" + analysis_statistics_json(manager.statistics()) + "}";
    }
} // namespace mplx
