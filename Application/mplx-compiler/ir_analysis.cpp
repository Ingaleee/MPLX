#include "ir_analysis.hpp"
#include "integer.hpp"
#include <algorithm>
#include <stdexcept>

namespace mplx {
    bool ControlFlow::dominates(uint32_t definition, uint32_t use) const {
        return definition < reachable.size() && use < reachable.size() && reachable[definition] &&
               reachable[use] && enter[definition] <= enter[use] && leave[use] <= leave[definition];
    }

    ControlFlow analyze_control_flow(const IRFunction &function) {
        const auto count = function.blocks.size();
        if (!count || count > 65536)
            throw std::runtime_error("invalid CFG size");
        ControlFlow result;
        result.successors.resize(count);
        result.predecessors.resize(count);
        result.children.resize(count);
        result.frontier.resize(count);
        result.reachable.resize(count, false);
        result.immediate_dominator.resize(count, no_id);
        result.enter.resize(count, no_id);
        result.leave.resize(count, no_id);
        for (const auto &block : function.blocks) {
            if (block.id >= count)
                throw std::runtime_error("invalid CFG block identity");
            const auto &terminator = block.terminator;
            auto edge = [&](uint32_t target) {
                if (target >= count)
                    throw std::runtime_error("invalid CFG target");
                auto &edges = result.successors[block.id];
                if (std::find(edges.begin(), edges.end(), target) == edges.end()) {
                    edges.push_back(target);
                    result.predecessors[target].push_back(block.id);
                }
            };
            if (terminator.kind == TerminatorKind::Jump || terminator.kind == TerminatorKind::Branch)
                edge(terminator.first);
            if (terminator.kind == TerminatorKind::Branch)
                edge(terminator.second);
        }
        struct Visit {
            uint32_t block;
            size_t next;
        };
        std::vector<Visit> stack{{0, 0}};
        result.reachable[0] = true;
        std::vector<uint32_t> postorder;
        while (!stack.empty()) {
            auto &visit = stack.back();
            const auto &edges = result.successors[visit.block];
            if (visit.next < edges.size()) {
                const auto target = edges[visit.next++];
                if (!result.reachable[target]) {
                    result.reachable[target] = true;
                    stack.push_back({target, 0});
                }
            } else {
                postorder.push_back(visit.block);
                stack.pop_back();
            }
        }
        result.reverse_postorder.assign(postorder.rbegin(), postorder.rend());
        std::vector<uint32_t> position(count, no_id);
        for (uint32_t index = 0; index < result.reverse_postorder.size(); ++index)
            position[result.reverse_postorder[index]] = index;
        result.immediate_dominator[0] = 0;
        uint64_t work = 0;
        auto spend = [&] {
            if (++work > 8'000'000)
                throw std::runtime_error("CFG analysis exceeds work budget");
        };
        auto intersect = [&](uint32_t first, uint32_t second) {
            while (first != second) {
                spend();
                while (position[first] > position[second]) {
                    spend();
                    first = result.immediate_dominator[first];
                }
                while (position[second] > position[first]) {
                    spend();
                    second = result.immediate_dominator[second];
                }
            }
            return first;
        };
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto block : result.reverse_postorder) {
                if (!block)
                    continue;
                spend();
                uint32_t dominator = no_id;
                for (auto predecessor : result.predecessors[block])
                    if (result.immediate_dominator[predecessor] != no_id)
                        dominator = dominator == no_id ? predecessor : intersect(dominator, predecessor);
                if (dominator != result.immediate_dominator[block]) {
                    result.immediate_dominator[block] = dominator;
                    changed = true;
                }
            }
        }
        for (auto block : result.reverse_postorder) {
            if (block)
                result.children[result.immediate_dominator[block]].push_back(block);
            if (result.predecessors[block].size() < 2)
                continue;
            for (auto predecessor : result.predecessors[block]) {
                if (!result.reachable[predecessor])
                    continue;
                auto runner = predecessor;
                while (runner != result.immediate_dominator[block]) {
                    spend();
                    result.frontier[runner].push_back(block);
                    runner = result.immediate_dominator[runner];
                }
            }
        }
        for (auto &frontier : result.frontier) {
            std::sort(frontier.begin(), frontier.end());
            frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());
        }
        uint32_t clock = 0;
        stack = {{0, 0}};
        result.enter[0] = clock++;
        while (!stack.empty()) {
            auto &visit = stack.back();
            if (visit.next < result.children[visit.block].size()) {
                auto child = result.children[visit.block][visit.next++];
                result.enter[child] = clock++;
                stack.push_back({child, 0});
            } else {
                result.leave[visit.block] = clock++;
                stack.pop_back();
            }
        }
        return result;
    }

    void remove_unreachable_blocks(IRFunction &function) {
        const auto cfg = analyze_control_flow(function);
        std::vector<uint32_t> remap(function.blocks.size(), no_id);
        std::vector<uint32_t> original;
        std::vector<IRBlock> blocks;
        for (const auto &block : function.blocks)
            if (cfg.reachable[block.id]) {
                remap[block.id] = static_cast<uint32_t>(blocks.size());
                original.push_back(block.id);
                blocks.push_back(block);
                blocks.back().id = remap[block.id];
            }
        for (auto &block : blocks) {
            const auto old = original[block.id];
            for (auto &instruction : block.instructions) {
                if (instruction.op != IROp::Phi)
                    continue;
                std::vector<uint32_t> arguments, incoming;
                for (size_t edge = 0; edge < instruction.incoming_blocks.size(); ++edge) {
                    const auto from = instruction.incoming_blocks[edge];
                    if (cfg.reachable[from] &&
                        std::find(cfg.successors[from].begin(), cfg.successors[from].end(), old) !=
                            cfg.successors[from].end()) {
                        arguments.push_back(instruction.arguments[edge]);
                        incoming.push_back(remap[from]);
                    }
                }
                instruction.arguments = std::move(arguments);
                instruction.incoming_blocks = std::move(incoming);
            }
            auto &terminator = block.terminator;
            if (terminator.kind == TerminatorKind::Jump || terminator.kind == TerminatorKind::Branch)
                terminator.first = remap[terminator.first];
            if (terminator.kind == TerminatorKind::Branch)
                terminator.second = remap[terminator.second];
        }
        function.blocks = std::move(blocks);
    }

    bool ir_operation_is_total(const IRInstruction &instruction) {
        if (instruction.op == IROp::Constant || instruction.op == IROp::Parameter ||
            instruction.op == IROp::Phi || instruction.op == IROp::Copy || instruction.op == IROp::Load)
            return true;
        if (instruction.op == IROp::Cast)
            return instruction.type != ValueType::I32;
        if (instruction.op == IROp::Unary)
            return instruction.operation == "!";
        if (instruction.op == IROp::Binary) {
            const auto &op = instruction.operation;
            return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
        }
        return false;
    }

    long long evaluate_ir_constant(const IRInstruction &instruction, long long first, long long second) {
        long long value = first;
        if (instruction.op == IROp::Unary)
            value = instruction.operation == "!" ? first == 0 : integer::neg(first);
        if (instruction.op == IROp::Binary) {
            const auto &op = instruction.operation;
            if (op == "+")
                value = integer::add(first, second);
            else if (op == "-")
                value = integer::sub(first, second);
            else if (op == "*")
                value = integer::mul(first, second);
            else if (op == "/")
                value = integer::div(first, second);
            else if (op == "%")
                value = integer::mod(first, second);
            else if (op == "==")
                value = first == second;
            else if (op == "!=")
                value = first != second;
            else if (op == "<")
                value = first < second;
            else if (op == "<=")
                value = first <= second;
            else if (op == ">")
                value = first > second;
            else if (op == ">=")
                value = first >= second;
            else
                throw std::runtime_error("invalid constant operation");
        }
        if (instruction.type == ValueType::I32 && (value < INT32_MIN || value > INT32_MAX))
            throw std::runtime_error("i32 range exceeded");
        return instruction.type == ValueType::Bool ? value != 0 : value;
    }
} // namespace mplx
