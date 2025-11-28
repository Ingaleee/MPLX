#include "verifier.hpp"
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <unordered_set>

namespace mplx {
    VerificationResult verify_bytecode(const Bytecode &bc) {
        VerificationResult r;
        auto fail = [&](uint32_t f, uint32_t pc, const std::string &m) { r.errors.push_back({f, pc, m}); };
        constexpr uint32_t module = std::numeric_limits<uint32_t>::max();
        if (bc.code.empty() || bc.code.size() > 16 * 1024 * 1024 || bc.functions.empty() ||
            bc.functions.size() > 65536) {
            fail(module, 0, "empty or oversized bytecode module");
            return r;
        }
        std::vector<int32_t> index(bc.code.size(), -1);
        for (uint32_t pc = 0; pc < bc.code.size();) {
            Instruction ins;
            std::string error;
            if (!decode_instruction(bc, pc, ins, error)) {
                fail(module, pc, error);
                return r;
            }
            index[pc] = static_cast<int32_t>(r.instructions.size());
            r.instructions.push_back(ins);
            if (r.instructions.size() > 2'000'000) {
                fail(module, pc, "decoded instruction count exceeds verification budget");
                return r;
            }
            pc = ins.next;
        }
        if (r.instructions.back().op != OP_HALT) {
            fail(module, 0, "missing terminal HALT sentinel");
            return r;
        }
        bool first_mapping = true;
        uint32_t previous_mapping = 0;
        for (const auto &mapping : bc.source_map) {
            if (mapping.offset >= bc.code.size() || index[mapping.offset] < 0 ||
                (!first_mapping && mapping.offset <= previous_mapping) ||
                mapping.span.start.offset > mapping.span.end.offset) {
                fail(module, mapping.offset, "invalid source mapping");
                return r;
            }
            first_mapping = false;
            previous_mapping = mapping.offset;
        }
        const auto code_end = r.instructions.back().offset;
        std::vector<uint32_t> order;
        std::unordered_set<std::string> names;
        r.functions.resize(bc.functions.size());
        for (uint32_t f = 0; f < bc.functions.size(); ++f) {
            const auto &meta = bc.functions[f];
            if (meta.entry >= code_end || index[meta.entry] < 0)
                fail(f, meta.entry, "invalid function entry");
            if (meta.arity > meta.locals)
                fail(f, meta.entry, "arity exceeds local slots");
            if ((!meta.parameters.empty() && meta.parameters.size() != meta.arity) ||
                !valid_type(meta.return_type))
                fail(f, meta.entry, "invalid export signature");
            for (auto type : meta.parameters)
                if (!valid_type(type))
                    fail(f, meta.entry, "invalid parameter type");
            if (meta.name.empty() || !names.insert(meta.name).second)
                fail(f, meta.entry, "empty or duplicate function name");
            order.push_back(f);
        }
        if (!r.ok())
            return r;
        std::sort(order.begin(), order.end(),
                  [&](auto a, auto b) { return bc.functions[a].entry < bc.functions[b].entry; });
        if (bc.functions[order[0]].entry != 0)
            fail(module, 0, "code before first function");
        for (size_t i = 0; i < order.size(); ++i) {
            auto f = order[i];
            auto end = i + 1 < order.size() ? bc.functions[order[i + 1]].entry : code_end;
            r.functions[f].end = end;
            if (end == bc.functions[f].entry)
                fail(f, end, "overlapping function entries");
        }
        if (!r.ok())
            return r;
        for (auto f : order) {
            const auto &meta = bc.functions[f];
            for (uint32_t pc = meta.entry; pc < r.functions[f].end;) {
                const auto &ins = r.instructions[index[pc]];
                if (ins.op == OP_HALT)
                    fail(f, pc, "HALT inside function");
                if (ins.op == OP_PUSH_CONST && ins.operand >= bc.consts.size())
                    fail(f, pc, "constant index out of bounds");
                if (ins.op == OP_CALL && ins.operand >= bc.functions.size())
                    fail(f, pc, "callee index out of bounds");
                uint32_t local = ins.operand;
                bool has_local = ins.op == OP_LOAD_LOCAL || ins.op == OP_STORE_LOCAL ||
                                 ins.op == OP_LOAD_LOCAL8 || ins.op == OP_STORE_LOCAL8;
                if (ins.op >= OP_LD0 && ins.op <= OP_LD3) {
                    local = ins.op - OP_LD0;
                    has_local = true;
                }
                if (ins.op >= OP_ST0 && ins.op <= OP_ST3) {
                    local = ins.op - OP_ST0;
                    has_local = true;
                }
                if (has_local && local >= meta.locals)
                    fail(f, pc, "local index out of bounds");
                if (ins.op == OP_JMP || ins.op == OP_JMP_IF_FALSE || ins.op == OP_JMP_IF_TRUE) {
                    if (ins.operand < meta.entry || ins.operand >= r.functions[f].end ||
                        index[ins.operand] < 0)
                        fail(f, pc, "jump target is not an instruction in this function");
                }
                pc = ins.next;
            }
        }
        if (!r.ok())
            return r;
        r.stack_heights.assign(r.instructions.size(), -1);
        for (auto f : order) {
            std::deque<uint32_t> work{bc.functions[f].entry};
            r.stack_heights[index[work.front()]] = 0;
            while (!work.empty()) {
                auto pc = work.front();
                work.pop_front();
                const auto &ins = r.instructions[index[pc]];
                auto info = opcode_info(ins.op);
                int64_t h = r.stack_heights[index[pc]];
                auto inputs = ins.op == OP_CALL ? bc.functions[ins.operand].arity : info->inputs;
                if (h < inputs) {
                    fail(f, pc, "operand stack underflow");
                    continue;
                }
                if (ins.op == OP_RET && h != 1) {
                    fail(f, pc, "return requires exactly one operand");
                    continue;
                }
                auto after = h - inputs + info->outputs;
                r.functions[f].max_stack =
                    std::max(r.functions[f].max_stack, static_cast<uint32_t>(std::max(h, after)));
                auto visit = [&](uint32_t dst) {
                    if (dst >= r.functions[f].end) {
                        fail(f, pc, "function falls through without return");
                        return;
                    }
                    auto &seen = r.stack_heights[index[dst]];
                    if (seen < 0) {
                        seen = after;
                        work.push_back(dst);
                    } else if (seen != after)
                        fail(f, dst, "inconsistent operand stack height at control-flow merge");
                };
                if (ins.op == OP_RET)
                    continue;
                if (ins.op == OP_JMP || ins.op == OP_JMP_IF_FALSE || ins.op == OP_JMP_IF_TRUE)
                    visit(ins.operand);
                if (ins.op != OP_JMP)
                    visit(ins.next);
            }
        }
        return r;
    }
    void require_valid_bytecode(const Bytecode &bc) {
        auto r = verify_bytecode(bc);
        if (!r.ok()) {
            const auto &e = r.errors.front();
            throw std::runtime_error("invalid bytecode at pc=" + std::to_string(e.offset) + ": " + e.message);
        }
    }
} // namespace mplx
