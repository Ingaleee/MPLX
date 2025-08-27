#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mplx {

enum Op : uint8_t {
  OP_PUSH_CONST,
  OP_LOAD_LOCAL,
  OP_STORE_LOCAL,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV,
  OP_NEG,
  OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
  OP_JMP,
  OP_JMP_IF_FALSE,
  OP_CALL,
  OP_RET,
  OP_POP,
  OP_HALT
};

struct FuncMeta { std::string name; uint32_t entry{0}; uint8_t arity{0}; uint16_t locals{0}; };

struct Bytecode {
  std::vector<uint8_t> code;
  std::vector<long long> consts;
  std::vector<FuncMeta> functions;
};

} // namespace mplx