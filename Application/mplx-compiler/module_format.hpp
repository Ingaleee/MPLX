#pragma once
#include "verified_module.hpp"
#include <span>
#include <stdexcept>
namespace mplx {
    enum class ModuleError { Header, Version, Features, Size, Checksum, Directory, Metadata, Bytecode };
    class ModuleFormatError : public std::runtime_error {
      public:
        ModuleFormatError(ModuleError code, const std::string &message)
            : std::runtime_error(message), code_(code) {
        }
        ModuleError code() const {
            return code_;
        }

      private:
        ModuleError code_;
    };
    inline constexpr size_t max_module_bytes = 64 * 1024 * 1024;
    uint32_t crc32c(std::span<const uint8_t> bytes);
    std::vector<uint8_t> serialize_module(const VerifiedModule &module);
    std::shared_ptr<const VerifiedModule> deserialize_module(std::span<const uint8_t> bytes);
    std::string module_info_json(const VerifiedModule &module);
} // namespace mplx
