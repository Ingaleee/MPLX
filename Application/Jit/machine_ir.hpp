#pragma once
#include "../mplx-compiler/verified_module.hpp"
#include <string>

namespace mplx::jit {
    inline constexpr uint32_t no_machine_value = UINT32_MAX;
    struct MachineValue {
        uint32_t slot{0}, block{0}, definition{no_machine_value};
        std::vector<uint32_t> uses;
    };
    struct MachineInstruction {
        uint32_t pc{0}, function{0}, block{no_machine_value};
        std::vector<uint32_t> reads, writes;
    };
    struct MachineBlock {
        uint32_t function{0}, first{0}, last{0};
        std::vector<uint32_t> successors;
    };
    // Block-local SSA for physical frame slots. Values crossing an edge live in
    // the frame arena; calls and returns explicitly invalidate volatile registers.
    struct MachineIR {
        std::vector<MachineInstruction> instructions;
        std::vector<MachineBlock> blocks;
        std::vector<MachineValue> values;
    };
    MachineIR lower_machine_ir(const VerifiedModule &module);
    std::string dump_machine_ir_json(const MachineIR &module);
    struct AllocationStats {
        uint64_t memory_reads{0}, memory_writes{0}, register_reads{0}, register_writes{0}, spills{0},
            coalesced_copies{0};
    };
    struct NativeOptions {
        // Zero selects the arena-only baseline. R8-R11 are volatile on both ABIs.
        uint32_t registers{4};
    };
} // namespace mplx::jit
