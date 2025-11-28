#include "verified_module.hpp"
#include <stdexcept>
namespace mplx {
    namespace {
        Bytecode canonicalize(Bytecode bytecode) {
            // Hand-built legacy modules have an untyped i64 calling convention.
            for (auto &function : bytecode.functions)
                if (function.parameters.empty() && function.arity)
                    function.parameters.assign(function.arity, ValueType::I64);
            return bytecode;
        }
    } // namespace
    VerifiedModule::VerifiedModule(Bytecode bytecode)
        : bytecode_(canonicalize(std::move(bytecode))), verification_(verify_bytecode(bytecode_)) {
        if (!verification_.ok()) {
            const auto &error = verification_.errors.front();
            throw std::runtime_error("invalid bytecode at pc=" + std::to_string(error.offset) + ": " +
                                     error.message);
        }
    }
    std::shared_ptr<const VerifiedModule> VerifiedModule::create(Bytecode bytecode) {
        return std::shared_ptr<const VerifiedModule>(new VerifiedModule(std::move(bytecode)));
    }
} // namespace mplx
