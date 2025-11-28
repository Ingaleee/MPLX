#pragma once
#include "source.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mplx {
    enum Op : uint8_t {
        OP_PUSH_CONST,
        OP_LD0,
        OP_LD1,
        OP_LD2,
        OP_LD3,
        OP_ST0,
        OP_ST1,
        OP_ST2,
        OP_ST3,
        OP_LOAD_LOCAL8,
        OP_STORE_LOCAL8,
        OP_LOAD_LOCAL,
        OP_STORE_LOCAL,
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_MOD,
        OP_NEG,
        OP_EQ,
        OP_NE,
        OP_LT,
        OP_LE,
        OP_GT,
        OP_GE,
        OP_JMP,
        OP_JMP_IF_FALSE,
        OP_JMP_IF_TRUE,
        OP_AND,
        OP_OR,
        OP_NOT,
        OP_CALL,
        OP_RET,
        OP_POP,
        OP_HALT,
        OP_CHECK_I32,
        OP_COUNT
    };
    // Modules contain no VM-owned pointers or mutable execution counters.
    struct FuncMeta {
        std::string name;
        uint32_t entry{0};
        uint8_t arity{0};
        uint16_t locals{0};
        std::vector<ValueType> parameters{};
        ValueType return_type{ValueType::I64};
    };
    struct Bytecode {
        std::vector<uint8_t> code;
        std::vector<long long> consts;
        std::vector<FuncMeta> functions;
        struct SourceMapping {
            uint32_t offset;
            SourceSpan span;
        };
        std::vector<SourceMapping> source_map;
    };
    struct Instruction {
        uint32_t offset{0}, next{0}, operand{0};
        Op op{OP_HALT};
    };
    struct OpcodeInfo {
        const char *name;
        uint8_t operand_bytes, inputs, outputs;
    };
    const OpcodeInfo *opcode_info(uint8_t op);
    bool decode_instruction(const Bytecode &, uint32_t, Instruction &, std::string &);
    std::string dump_bytecode_json(const Bytecode &);
    std::string dump_cfg_json(const Bytecode &);
} // namespace mplx
