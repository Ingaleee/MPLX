#pragma once
#include "bytecode.hpp"
#include <limits>

namespace mplx {
    struct VerificationError {
        uint32_t function{std::numeric_limits<uint32_t>::max()}, offset{0};
        std::string message;
    };
    struct VerifiedFunction {
        uint32_t end{0}, max_stack{0};
    };
    struct VerificationResult {
        std::vector<VerificationError> errors;
        std::vector<Instruction> instructions;
        std::vector<VerifiedFunction> functions;
        // Indexed by instruction index; -1 means unreachable.
        std::vector<int64_t> stack_heights;
        bool ok() const {
            return errors.empty();
        }
    };
    VerificationResult verify_bytecode(const Bytecode &);
    void require_valid_bytecode(const Bytecode &);
} // namespace mplx
