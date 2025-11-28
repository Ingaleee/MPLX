#pragma once
#include "verified_module.hpp"
#include <memory>
#if defined(MPLX_WITH_JIT)
#include "../Jit/jit_compiler.hpp"
#include <mutex>
#endif
namespace mplx {
    class CompiledProgram final {
      public:
        static std::shared_ptr<const CompiledProgram> create(Bytecode bytecode);
        static std::shared_ptr<const CompiledProgram> create(std::shared_ptr<const VerifiedModule> module);
        const VerifiedModule &module() const {
            return *module_;
        }
#if defined(MPLX_WITH_JIT)
        struct NativeLookup {
            std::shared_ptr<const jit::JitCompiled> code;
            bool compiled{false};
        };
        NativeLookup native() const;
#endif
      private:
        explicit CompiledProgram(std::shared_ptr<const VerifiedModule> module);
        const std::shared_ptr<const VerifiedModule> module_;
#if defined(MPLX_WITH_JIT)
        mutable std::once_flag native_once_;
        mutable std::shared_ptr<const jit::JitCompiled> native_;
#endif
    };
} // namespace mplx
