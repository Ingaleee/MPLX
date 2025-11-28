#include "program.hpp"
#include <stdexcept>
namespace mplx {
    CompiledProgram::CompiledProgram(std::shared_ptr<const VerifiedModule> module)
        : module_(std::move(module)) {
        if (!module_)
            throw std::invalid_argument("null verified module");
    }
    std::shared_ptr<const CompiledProgram> CompiledProgram::create(Bytecode bytecode) {
        return create(VerifiedModule::create(std::move(bytecode)));
    }
    std::shared_ptr<const CompiledProgram>
    CompiledProgram::create(std::shared_ptr<const VerifiedModule> module) {
        return std::shared_ptr<const CompiledProgram>(new CompiledProgram(std::move(module)));
    }
#if defined(MPLX_WITH_JIT)
    CompiledProgram::NativeLookup CompiledProgram::native() const {
        NativeLookup result;
        std::call_once(native_once_, [&] {
            jit::JitCompiler compiler;
            auto code = compiler.compileModule(*module_);
            if (code) {
                native_ = std::make_shared<const jit::JitCompiled>(std::move(*code));
                result.compiled = true;
            }
        });
        result.code = native_;
        return result;
    }
#endif
} // namespace mplx
