#include "frontend.hpp"
#include "reference.hpp"
#include "vm.hpp"
#include <cstdlib>
#include <string>
struct Outcome {
    long long value;
    int error;
    std::string function{};
    mplx::SourceSpan span{};
    bool operator==(const Outcome &) const = default;
};
static Outcome execute(mplx::Bytecode bc, mplx::VM::JitMode mode) {
    mplx::VM vm(std::move(bc));
    vm.setJitMode(mode);
    vm.setLimits({10000, 32, 2048});
    try {
        return {vm.run(), -1};
    } catch (const mplx::RuntimeError &error) {
        if (error.code() == mplx::RuntimeErrorCode::Arithmetic)
            return {0, static_cast<int>(error.code()), error.function(), error.span()};
        return {0, static_cast<int>(error.code())};
    }
}
static Outcome execute_source(const std::string &source) {
    mplx::reference::Program program(source);
    try {
        return {program.invoke("main", {}, {100000, 32, 2048}), -1};
    } catch (const mplx::reference::Error &error) {
        using namespace mplx;
        switch (error.kind()) {
        case reference::ErrorKind::Arithmetic:
            return {0, static_cast<int>(RuntimeErrorCode::Arithmetic), error.function(), error.span()};
        case reference::ErrorKind::InvalidEntry:
            return {0, static_cast<int>(RuntimeErrorCode::InvalidEntry)};
        case reference::ErrorKind::StepLimit:
            return {0, static_cast<int>(RuntimeErrorCode::InstructionLimit)};
        case reference::ErrorKind::FrameLimit:
            return {0, static_cast<int>(RuntimeErrorCode::FrameLimit)};
        case reference::ErrorKind::ValueLimit:
        case reference::ErrorKind::ContinuationLimit:
            return {0, static_cast<int>(RuntimeErrorCode::ValueLimit)};
        default:
            std::abort();
        }
    }
}
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 8192)
        return 0;
    std::string source(reinterpret_cast<const char *>(data), size);
    auto optimized = mplx::analyze_source(source, true);
    auto unoptimized = mplx::analyze_source(source, false);
    if (optimized.ok() != unoptimized.ok())
        std::abort();
    if (!optimized.ok())
        return 0;
    // Instruction limits differ with optimization; compare values/arithmetic traps
    // when both finish. Each execution is still bounded.
    auto ref = execute(unoptimized.bytecode, mplx::VM::JitMode::Off);
    auto opt = execute(optimized.bytecode, mplx::VM::JitMode::Off);
    auto ast = execute_source(source);
    auto resource_limit = [](int error) {
        return error == static_cast<int>(mplx::RuntimeErrorCode::InstructionLimit) ||
               error == static_cast<int>(mplx::RuntimeErrorCode::FrameLimit) ||
               error == static_cast<int>(mplx::RuntimeErrorCode::ValueLimit);
    };
    if (!resource_limit(ref.error) && !resource_limit(opt.error) && !(ref == opt))
        std::abort();
    if (!resource_limit(ast.error) && !resource_limit(opt.error) && !(ast == opt))
        std::abort();
#if defined(MPLX_WITH_JIT)
    auto native = execute(optimized.bytecode, mplx::VM::JitMode::On);
    if (!(native == opt))
        std::abort();
#endif
    return 0;
}
