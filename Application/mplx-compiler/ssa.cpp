#include "ssa.hpp"
#include "ir_analysis.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace mplx {
    namespace {
        using Slots = std::set<uint32_t>;
        void promote(IRFunction &function) {
            if (function.ssa)
                return;
            remove_unreachable_blocks(function);
            // A separate invocation entry gives even an entry-block backedge an
            // ordinary predecessor and keeps parameter definitions outside loops.
            for (auto &block : function.blocks) {
                ++block.id;
                auto &terminator = block.terminator;
                if (terminator.kind == TerminatorKind::Jump || terminator.kind == TerminatorKind::Branch)
                    ++terminator.first;
                if (terminator.kind == TerminatorKind::Branch)
                    ++terminator.second;
            }
            IRBlock invocation;
            invocation.terminator = {TerminatorKind::Jump, no_id, 1, no_id, function.span};
            for (uint32_t parameter = 0; parameter < function.parameters.size(); ++parameter) {
                auto value = static_cast<uint32_t>(function.values.size());
                function.values.push_back(function.parameters[parameter]);
                invocation.instructions.push_back({IROp::Parameter,
                                                   value,
                                                   function.parameters[parameter],
                                                   {},
                                                   parameter,
                                                   0,
                                                   "",
                                                   function.span});
            }
            function.blocks.insert(function.blocks.begin(), std::move(invocation));
            const auto cfg = analyze_control_flow(function);
            const auto count = function.blocks.size();
            const auto locals = function.locals.size();
            std::vector<Slots> uses(count), definitions(count), live_in(count), live_out(count);
            std::vector<Slots> defining_blocks(locals);
            for (const auto &block : function.blocks) {
                for (const auto &instruction : block.instructions) {
                    if (instruction.op == IROp::Load && !definitions[block.id].contains(instruction.index))
                        uses[block.id].insert(instruction.index);
                    if (instruction.op == IROp::Store || instruction.op == IROp::Parameter) {
                        definitions[block.id].insert(instruction.index);
                        defining_blocks[instruction.index].insert(block.id);
                    }
                }
            }
            bool changed = true;
            uint64_t work = 0;
            while (changed) {
                changed = false;
                for (auto it = cfg.reverse_postorder.rbegin(); it != cfg.reverse_postorder.rend(); ++it) {
                    auto block = *it;
                    Slots output;
                    for (auto successor : cfg.successors[block])
                        output.insert(live_in[successor].begin(), live_in[successor].end());
                    Slots input = uses[block];
                    for (auto slot : output)
                        if (!definitions[block].contains(slot))
                            input.insert(slot);
                    if (input != live_in[block] || output != live_out[block]) {
                        live_in[block] = std::move(input);
                        live_out[block] = std::move(output);
                        changed = true;
                    }
                    if ((work += 1 + output.size() + live_in[block].size()) > 8'000'000)
                        throw std::runtime_error("SSA liveness exceeds work budget");
                }
            }
            std::vector<Slots> phi_slots(count);
            for (uint32_t slot = 0; slot < locals; ++slot) {
                auto visited = defining_blocks[slot];
                std::vector<uint32_t> queue(visited.begin(), visited.end());
                while (!queue.empty()) {
                    const auto block = queue.back();
                    queue.pop_back();
                    for (auto frontier : cfg.frontier[block]) {
                        if (!live_in[frontier].contains(slot) || !phi_slots[frontier].insert(slot).second)
                            continue;
                        if (visited.insert(frontier).second)
                            queue.push_back(frontier);
                    }
                }
            }
            for (auto &block : function.blocks) {
                std::vector<IRInstruction> phis;
                for (auto slot : phi_slots[block.id]) {
                    auto value = static_cast<uint32_t>(function.values.size());
                    function.values.push_back(function.locals[slot]);
                    IRInstruction phi{IROp::Phi,
                                      value,
                                      function.locals[slot],
                                      std::vector<uint32_t>(cfg.predecessors[block.id].size(), no_id),
                                      slot,
                                      0,
                                      "",
                                      function.span};
                    phi.incoming_blocks = cfg.predecessors[block.id];
                    phis.push_back(std::move(phi));
                }
                phis.insert(phis.end(), std::make_move_iterator(block.instructions.begin()),
                            std::make_move_iterator(block.instructions.end()));
                block.instructions = std::move(phis);
            }
            std::vector<std::vector<uint32_t>> stacks(locals);
            std::vector<uint32_t> aliases(function.values.size(), no_id);
            auto resolve = [&](uint32_t value) {
                if (value >= aliases.size())
                    throw std::runtime_error("SSA predecessor did not supply a value");
                size_t hops = 0;
                while (aliases[value] != no_id) {
                    value = aliases[value];
                    if (++hops > aliases.size())
                        throw std::runtime_error("cyclic SSA value replacement");
                }
                return value;
            };
            struct Visit {
                uint32_t block;
                bool leave;
                std::vector<uint32_t> pushed;
            };
            std::vector<Visit> visits{{0, false, {}}};
            while (!visits.empty()) {
                auto visit = std::move(visits.back());
                visits.pop_back();
                if (visit.leave) {
                    for (auto slot : visit.pushed)
                        stacks[slot].pop_back();
                    continue;
                }
                auto &block = function.blocks[visit.block];
                std::vector<IRInstruction> instructions;
                auto assign = [&](uint32_t slot, uint32_t value) {
                    stacks[slot].push_back(value);
                    visit.pushed.push_back(slot);
                };
                for (auto instruction : block.instructions) {
                    if (instruction.op == IROp::Phi || instruction.op == IROp::Parameter) {
                        assign(instruction.index, instruction.result);
                        instructions.push_back(std::move(instruction));
                        continue;
                    }
                    for (auto &argument : instruction.arguments)
                        argument = resolve(argument);
                    if (instruction.op == IROp::Load) {
                        if (stacks[instruction.index].empty())
                            throw std::runtime_error("SSA promotion encountered an uninitialized local");
                        aliases[instruction.result] = stacks[instruction.index].back();
                    } else if (instruction.op == IROp::Store) {
                        assign(instruction.index, instruction.arguments[0]);
                    } else {
                        instructions.push_back(std::move(instruction));
                    }
                }
                block.instructions = std::move(instructions);
                if (block.terminator.value != no_id)
                    block.terminator.value = resolve(block.terminator.value);
                for (auto successor : cfg.successors[block.id]) {
                    for (auto &phi : function.blocks[successor].instructions) {
                        if (phi.op != IROp::Phi)
                            break;
                        if (stacks[phi.index].empty())
                            throw std::runtime_error("SSA edge lacks an initialized local");
                        auto edge =
                            std::find(phi.incoming_blocks.begin(), phi.incoming_blocks.end(), block.id);
                        phi.arguments[static_cast<size_t>(edge - phi.incoming_blocks.begin())] =
                            stacks[phi.index].back();
                    }
                }
                visit.leave = true;
                visits.push_back(std::move(visit));
                for (auto child = cfg.children[block.id].rbegin(); child != cfg.children[block.id].rend();
                     ++child)
                    visits.push_back({*child, false, {}});
            }
            // Backedge values may have been named before their dominating subtree
            // was visited; normalize their final aliases once renaming is complete.
            for (auto &block : function.blocks)
                for (auto &instruction : block.instructions)
                    for (auto &argument : instruction.arguments)
                        argument = resolve(argument);
            function.locals = function.parameters;
            function.ssa = true;
        }
    } // namespace
    void promote_locals_to_ssa(IRModule &module) {
        for (auto &function : module.functions)
            promote(function);
    }
} // namespace mplx
