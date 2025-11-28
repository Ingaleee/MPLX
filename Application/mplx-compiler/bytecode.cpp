#include "bytecode.hpp"
#include "diagnostic.hpp"
#include "verifier.hpp"
#include <map>
#include <set>

namespace mplx {
    const OpcodeInfo *opcode_info(uint8_t op) {
        static constexpr OpcodeInfo infos[] = {
            {"PUSH_CONST", 4, 0, 1},  {"LD0", 0, 0, 1},          {"LD1", 0, 0, 1},
            {"LD2", 0, 0, 1},         {"LD3", 0, 0, 1},          {"ST0", 0, 1, 1},
            {"ST1", 0, 1, 1},         {"ST2", 0, 1, 1},          {"ST3", 0, 1, 1},
            {"LOAD_LOCAL8", 1, 0, 1}, {"STORE_LOCAL8", 1, 1, 1}, {"LOAD_LOCAL", 4, 0, 1},
            {"STORE_LOCAL", 4, 1, 1}, {"ADD", 0, 2, 1},          {"SUB", 0, 2, 1},
            {"MUL", 0, 2, 1},         {"DIV", 0, 2, 1},          {"MOD", 0, 2, 1},
            {"NEG", 0, 1, 1},         {"EQ", 0, 2, 1},           {"NE", 0, 2, 1},
            {"LT", 0, 2, 1},          {"LE", 0, 2, 1},           {"GT", 0, 2, 1},
            {"GE", 0, 2, 1},          {"JMP", 4, 0, 0},          {"JMP_IF_FALSE", 4, 1, 0},
            {"JMP_IF_TRUE", 4, 1, 0}, {"AND", 0, 2, 1},          {"OR", 0, 2, 1},
            {"NOT", 0, 1, 1},         {"CALL", 4, 0, 1},         {"RET", 0, 1, 0},
            {"POP", 0, 1, 0},         {"HALT", 0, 0, 0},         {"CHECK_I32", 0, 1, 1}};
        static_assert(sizeof(infos) / sizeof(infos[0]) == OP_COUNT);
        return op < OP_COUNT ? &infos[op] : nullptr;
    }
    bool decode_instruction(const Bytecode &bc, uint32_t offset, Instruction &ins, std::string &error) {
        if (offset >= bc.code.size()) {
            error = "instruction out of bounds";
            return false;
        }
        auto info = opcode_info(bc.code[offset]);
        if (!info) {
            error = "unknown opcode";
            return false;
        }
        if (bc.code.size() - offset - 1 < info->operand_bytes) {
            error = "truncated operand";
            return false;
        }
        ins = {offset, offset + 1u + info->operand_bytes, 0, static_cast<Op>(bc.code[offset])};
        for (uint32_t i = 0; i < info->operand_bytes; ++i)
            ins.operand |= static_cast<uint32_t>(bc.code[offset + 1 + i]) << (8 * i);
        return true;
    }
    std::string dump_bytecode_json(const Bytecode &bc) {
        std::string out = "{\"functions\":[";
        for (size_t i = 0; i < bc.functions.size(); ++i) {
            if (i)
                out += ',';
            const auto &f = bc.functions[i];
            out += "{\"name\":" + json_quote(f.name) + ",\"entry\":" + std::to_string(f.entry) +
                   ",\"arity\":" + std::to_string(f.arity) + ",\"locals\":" + std::to_string(f.locals) + '}';
        }
        out += "],\"consts\":[";
        for (size_t i = 0; i < bc.consts.size(); ++i) {
            if (i)
                out += ',';
            out += std::to_string(bc.consts[i]);
        }
        out += "],\"code\":[";
        for (size_t i = 0; i < bc.code.size(); ++i) {
            if (i)
                out += ',';
            out += std::to_string(bc.code[i]);
        }
        return out + "]}";
    }
    std::string dump_cfg_json(const Bytecode &bc) {
        const auto v = verify_bytecode(bc);
        if (!v.ok()) {
            require_valid_bytecode(bc);
        }
        struct Block {
            uint32_t start, end, function;
        };
        std::vector<Block> blocks;
        std::map<uint32_t, size_t> ids;
        for (uint32_t f = 0; f < bc.functions.size(); ++f) {
            auto begin = bc.functions[f].entry, end = v.functions[f].end;
            std::set<uint32_t> leaders{begin};
            for (const auto &ins : v.instructions) {
                if (ins.offset < begin || ins.offset >= end)
                    continue;
                if (ins.op == OP_JMP || ins.op == OP_JMP_IF_FALSE || ins.op == OP_JMP_IF_TRUE) {
                    leaders.insert(ins.operand);
                    if (ins.next < end)
                        leaders.insert(ins.next);
                } else if (ins.op == OP_RET && ins.next < end)
                    leaders.insert(ins.next);
            }
            std::vector<uint32_t> starts(leaders.begin(), leaders.end());
            for (size_t i = 0; i < starts.size(); ++i) {
                ids[starts[i]] = blocks.size();
                blocks.push_back({starts[i], i + 1 < starts.size() ? starts[i + 1] : end, f});
            }
        }
        std::string out = "{\"blocks\":[";
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (i)
                out += ',';
            const auto &b = blocks[i];
            out += "{\"id\":" + std::to_string(i) + ",\"function\":" + std::to_string(b.function) +
                   ",\"start\":" + std::to_string(b.start) + ",\"end\":" + std::to_string(b.end) + '}';
        }
        out += "],\"edges\":[";
        bool first = true;
        auto edge = [&](size_t from, uint32_t target) {
            if (!first)
                out += ',';
            first = false;
            out += "{\"from\":" + std::to_string(from) + ",\"to\":" + std::to_string(ids.at(target)) + '}';
        };
        for (size_t i = 0; i < blocks.size(); ++i) {
            Instruction last;
            std::string error;
            for (auto p = blocks[i].start; p < blocks[i].end; p = last.next)
                decode_instruction(bc, p, last, error);
            if (last.op == OP_JMP || last.op == OP_JMP_IF_FALSE || last.op == OP_JMP_IF_TRUE)
                edge(i, last.operand);
            if (last.op != OP_JMP && last.op != OP_RET && last.next < v.functions[blocks[i].function].end)
                edge(i, last.next);
        }
        return out + "]}";
    }
} // namespace mplx
