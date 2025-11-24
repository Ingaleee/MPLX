#include "compiler.hpp"
#include "semantic.hpp"
#include "verifier.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace mplx {
    namespace {
        class Lowerer {
          public:
            Bytecode bc;
            Bytecode lower(const IRModule &module) {
                for (const auto &f : module.functions)
                    function(f);
                emit(OP_HALT, {}, 0);
                return std::move(bc);
            }

          private:
            struct Patch {
                uint32_t operand, target;
            };
            void u32(uint32_t value) {
                for (int i = 0; i < 4; ++i)
                    bc.code.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
            void emit(Op op, SourceSpan span, uint32_t operand = 0) {
                bc.source_map.push_back({static_cast<uint32_t>(bc.code.size()), span});
                bc.code.push_back(op);
                const auto width = opcode_info(op)->operand_bytes;
                if (width == 4)
                    u32(operand);
                else if (width == 1)
                    bc.code.push_back(static_cast<uint8_t>(operand));
            }
            void local(uint32_t slot, bool store, SourceSpan span) {
                if (slot <= 3)
                    emit(static_cast<Op>((store ? OP_ST0 : OP_LD0) + slot), span);
                else if (slot <= 255)
                    emit(store ? OP_STORE_LOCAL8 : OP_LOAD_LOCAL8, span, slot);
                else
                    emit(store ? OP_STORE_LOCAL : OP_LOAD_LOCAL, span, slot);
            }
            std::vector<uint32_t> allocateTemps(const IRFunction &f, uint32_t &next) {
                using Live = std::set<uint32_t>;
                const auto count = f.blocks.size();
                std::vector<Live> uses(count), defs(count), in(count), out(count);
                std::vector<std::vector<uint32_t>> successors(count);
                for (const auto &b : f.blocks) {
                    for (const auto &op : b.instructions) {
                        if (op.op != IROp::Phi)
                            for (auto value : op.arguments)
                                if (!defs[b.id].contains(value))
                                    uses[b.id].insert(value);
                        if (op.result != no_id)
                            defs[b.id].insert(op.result);
                    }
                    const auto &t = b.terminator;
                    if ((t.kind == TerminatorKind::Branch || t.kind == TerminatorKind::Return) &&
                        !defs[b.id].contains(t.value))
                        uses[b.id].insert(t.value);
                    if (t.kind == TerminatorKind::Jump || t.kind == TerminatorKind::Branch)
                        successors[b.id].push_back(t.first);
                    if (t.kind == TerminatorKind::Branch)
                        successors[b.id].push_back(t.second);
                }
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (size_t index = count; index > 0; --index) {
                        auto b = index - 1;
                        Live output;
                        for (auto to : successors[b]) {
                            output.insert(in[to].begin(), in[to].end());
                            for (const auto &phi : f.blocks[to].instructions) {
                                if (phi.op != IROp::Phi)
                                    break;
                                const auto edge =
                                    std::find(phi.incoming_blocks.begin(), phi.incoming_blocks.end(), b);
                                if (edge != phi.incoming_blocks.end())
                                    output.insert(phi.arguments[static_cast<size_t>(
                                        edge - phi.incoming_blocks.begin())]);
                            }
                        }
                        Live input = uses[b];
                        for (auto value : output)
                            if (!defs[b].contains(value))
                                input.insert(value);
                        if (input != in[b] || output != out[b]) {
                            in[b] = std::move(input);
                            out[b] = std::move(output);
                            changed = true;
                        }
                    }
                }
                std::vector<Live> conflicts(f.values.size());
                std::vector<bool> active(f.values.size(), false);
                for (const auto &b : f.blocks) {
                    auto live = out[b.id];
                    if (b.terminator.kind == TerminatorKind::Branch ||
                        b.terminator.kind == TerminatorKind::Return)
                        live.insert(b.terminator.value);
                    for (auto it = b.instructions.rbegin(); it != b.instructions.rend(); ++it) {
                        if (it->result != no_id) {
                            auto result = it->result;
                            active[result] = true;
                            for (auto other : live)
                                if (other != result) {
                                    conflicts[result].insert(other);
                                    conflicts[other].insert(result);
                                }
                            live.erase(result);
                        }
                        if (it->op != IROp::Phi)
                            live.insert(it->arguments.begin(), it->arguments.end());
                    }
                }
                std::vector<uint32_t> order, colors(f.values.size(), no_id), slots(f.values.size(), no_id);
                for (uint32_t value = 0; value < f.values.size(); ++value)
                    if (active[value])
                        order.push_back(value);
                std::sort(order.begin(), order.end(), [&](auto a, auto b) {
                    return conflicts[a].size() != conflicts[b].size()
                               ? conflicts[a].size() > conflicts[b].size()
                               : a < b;
                });
                uint32_t count_colors = 0;
                for (auto value : order) {
                    Live unavailable;
                    for (auto other : conflicts[value])
                        if (colors[other] != no_id)
                            unavailable.insert(colors[other]);
                    uint32_t color = 0;
                    while (unavailable.contains(color))
                        ++color;
                    colors[value] = color;
                    count_colors = std::max(count_colors, color + 1);
                    slots[value] = next + color;
                }
                // Check the physical allocation against the liveness interference graph.
                for (auto value : order)
                    for (auto other : conflicts[value])
                        if (colors[value] == colors[other])
                            throw std::runtime_error("invalid temporary-slot interference allocation");
                next += count_colors;
                return slots;
            }
            void function(const IRFunction &f) {
                FuncMeta meta{f.name, static_cast<uint32_t>(bc.code.size()),
                              static_cast<uint8_t>(f.parameters.size()), 0};
                meta.parameters = f.parameters;
                meta.return_type = f.return_type;
                uint32_t next = static_cast<uint32_t>(f.locals.size());
                auto slots = allocateTemps(f, next);
                if (next > 65535)
                    throw std::runtime_error("IR lowering exceeds 65535 local/temporary slots");
                meta.locals = static_cast<uint16_t>(next);
                struct EdgeCopy {
                    uint32_t source, destination;
                    SourceSpan span;
                };
                std::map<std::pair<uint32_t, uint32_t>, std::vector<EdgeCopy>> copies;
                for (const auto &block : f.blocks) {
                    for (const auto &phi : block.instructions) {
                        if (phi.op != IROp::Phi)
                            break;
                        for (size_t edge = 0; edge < phi.arguments.size(); ++edge) {
                            const auto source = slots.at(phi.arguments[edge]);
                            const auto destination = slots.at(phi.result);
                            if (source != destination)
                                copies[{phi.incoming_blocks[edge], block.id}].push_back(
                                    {source, destination, phi.span});
                        }
                    }
                }
                std::vector<uint32_t> blocks(f.blocks.size() + copies.size());
                std::map<std::pair<uint32_t, uint32_t>, uint32_t> edge_blocks;
                uint32_t edge_id = static_cast<uint32_t>(f.blocks.size());
                for (const auto &[edge, moves] : copies) {
                    (void)moves;
                    edge_blocks.emplace(edge, edge_id++);
                }
                std::vector<Patch> patches;
                auto jump = [&](Op op, uint32_t block, SourceSpan span) {
                    emit(op, span, 0);
                    patches.push_back({static_cast<uint32_t>(bc.code.size() - 4), block});
                };
                auto load = [&](uint32_t value, SourceSpan span) { local(slots.at(value), false, span); };
                for (const auto &b : f.blocks) {
                    blocks[b.id] = static_cast<uint32_t>(bc.code.size());
                    for (const auto &ins : b.instructions) {
                        if (ins.op == IROp::Phi)
                            continue;
                        for (auto a : ins.arguments)
                            load(a, ins.span);
                        switch (ins.op) {
                        case IROp::Constant:
                            bc.consts.push_back(ins.constant);
                            emit(OP_PUSH_CONST, ins.span, static_cast<uint32_t>(bc.consts.size() - 1));
                            break;
                        case IROp::Load:
                        case IROp::Parameter:
                            local(ins.index, false, ins.span);
                            break;
                        case IROp::Copy:
                            break; // the argument load already produces the copied value
                        case IROp::Phi:
                            throw std::logic_error("phi reached ordinary instruction lowering");
                        case IROp::Store:
                            local(ins.index, true, ins.span);
                            break;
                        case IROp::Cast:
                            if (ins.type == ValueType::Bool) {
                                emit(OP_NOT, ins.span);
                                emit(OP_NOT, ins.span);
                            }
                            if (ins.type == ValueType::I32)
                                emit(OP_CHECK_I32, ins.span);
                            break;
                        case IROp::Unary:
                            emit(ins.operation == "!" ? OP_NOT : OP_NEG, ins.span);
                            break;
                        case IROp::Call:
                            emit(OP_CALL, ins.span, ins.index);
                            break;
                        case IROp::Binary: {
                            const auto &op = ins.operation;
                            auto code = op == "+"    ? OP_ADD
                                        : op == "-"  ? OP_SUB
                                        : op == "*"  ? OP_MUL
                                        : op == "/"  ? OP_DIV
                                        : op == "%"  ? OP_MOD
                                        : op == "==" ? OP_EQ
                                        : op == "!=" ? OP_NE
                                        : op == "<"  ? OP_LT
                                        : op == "<=" ? OP_LE
                                        : op == ">"  ? OP_GT
                                                     : OP_GE;
                            emit(code, ins.span);
                            break;
                        }
                        }
                        if ((ins.op == IROp::Unary || ins.op == IROp::Binary) && ins.type == ValueType::I32)
                            emit(OP_CHECK_I32, ins.span);
                        if (ins.result != no_id)
                            local(slots[ins.result], true, ins.span);
                        emit(OP_POP, ins.span);
                    }
                    const auto &term = b.terminator;
                    auto target = [&](uint32_t successor) {
                        auto edge = edge_blocks.find({b.id, successor});
                        return edge == edge_blocks.end() ? successor : edge->second;
                    };
                    if (term.kind == TerminatorKind::Jump)
                        jump(OP_JMP, target(term.first), term.span);
                    else if (term.kind == TerminatorKind::Branch) {
                        load(term.value, term.span);
                        jump(OP_JMP_IF_FALSE, target(term.second), term.span);
                        jump(OP_JMP, target(term.first), term.span);
                    } else {
                        load(term.value, term.span);
                        emit(OP_RET, term.span);
                    }
                }
                for (const auto &[edge, moves] : copies) {
                    blocks[edge_blocks.at(edge)] = static_cast<uint32_t>(bc.code.size());
                    // Parallel edge copies stage all inputs before writing any
                    // destination. Cycles and aliased slots therefore stay correct.
                    for (const auto &move : moves)
                        local(move.source, false, move.span);
                    for (auto move = moves.rbegin(); move != moves.rend(); ++move) {
                        local(move->destination, true, move->span);
                        emit(OP_POP, move->span);
                    }
                    jump(OP_JMP, edge.second, f.span);
                }
                for (const auto &p : patches)
                    for (int i = 0; i < 4; ++i)
                        bc.code[p.operand + i] = static_cast<uint8_t>(blocks[p.target] >> (8 * i));
                bc.functions.push_back(std::move(meta));
            }
        };
    } // namespace
    CompileResult Compiler::compile(const Module &module) {
        auto semantic = analyze_module(module);
        if (!semantic.ok()) {
            CompileResult result;
            for (const auto &d : semantic.diagnostics)
                result.diags.push_back(d.message);
            return result;
        }
        return compile(std::move(semantic.ir));
    }
    CompileResult Compiler::compile(IRModule module) {
        CompileResult result;
        auto errors = verify_ir(module);
        if (optimize_ && errors.empty()) {
            optimize_ir(module);
            errors = verify_ir(module);
        }
        if (!errors.empty()) {
            for (const auto &d : errors)
                result.diags.push_back(d.message);
            return result;
        }
        result.bc = Lowerer().lower(module);
        auto verification = verify_bytecode(result.bc);
        for (const auto &error : verification.errors)
            result.diags.push_back("bytecode verification: " + error.message);
        return result;
    }
} // namespace mplx
