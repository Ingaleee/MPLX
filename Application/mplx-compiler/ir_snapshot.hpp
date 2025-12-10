#pragma once

#include "ir.hpp"
#include <span>
#include <string_view>

namespace mplx {
    // Compiler debugging interchange, deliberately distinct from executable MPLXB.
    constexpr uint32_t ir_snapshot_version = 1;
    constexpr size_t ir_snapshot_limit = 16 * 1024 * 1024;
    constexpr size_t ir_content_limit = 256 * 1024 * 1024;
    std::string serialize_ir_snapshot(const IRModule &module, size_t maximum_bytes = ir_snapshot_limit);
    IRModule deserialize_ir_snapshot(std::string_view bytes, bool verify = true);
    std::string ir_fingerprint(const IRModule &module);
    std::string snapshot_hex(std::string_view bytes);
    struct IRChange {
        uint32_t function{0};
        bool signature_changed{false};
        std::vector<uint32_t> blocks;
    };
    std::vector<IRChange> compare_ir(const IRModule &before, const IRModule &after);
    std::string ir_changes_json(const std::vector<IRChange> &changes);
} // namespace mplx
