#pragma once
#include "verifier.hpp"
#include <memory>
namespace mplx {
    // Construction is the only mutating boundary. Instances share verified facts.
    class VerifiedModule final {
      public:
        static std::shared_ptr<const VerifiedModule> create(Bytecode bytecode);
        const Bytecode &bytecode() const {
            return bytecode_;
        }
        const VerificationResult &verification() const {
            return verification_;
        }

      private:
        explicit VerifiedModule(Bytecode bytecode);
        const Bytecode bytecode_;
        const VerificationResult verification_;
    };
} // namespace mplx
