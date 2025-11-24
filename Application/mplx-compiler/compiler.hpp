#pragma once
#include "ast.hpp"
#include "bytecode.hpp"
#include "ir.hpp"

namespace mplx {
    struct CompileResult {
        Bytecode bc;
        std::vector<std::string> diags;
    };
    class Compiler {
      public:
        explicit Compiler(bool optimize = true) : optimize_(optimize) {
        }
        CompileResult compile(const Module &module);
        CompileResult compile(IRModule module);

      private:
        bool optimize_;
    };
} // namespace mplx
