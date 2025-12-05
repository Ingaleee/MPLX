#pragma once

#include "ir.hpp"
#include <vector>

namespace mplx {
    struct ControlFlow {
        std::vector<std::vector<uint32_t>> successors, predecessors, children, frontier;
        std::vector<uint32_t> reverse_postorder, immediate_dominator, enter, leave;
        std::vector<bool> reachable;
        bool dominates(uint32_t definition, uint32_t use) const;
    };

    ControlFlow analyze_control_flow(const IRFunction &function);
    void remove_unreachable_blocks(IRFunction &function);
    bool ir_operation_is_total(const IRInstruction &instruction);
    long long evaluate_ir_constant(const IRInstruction &instruction, long long first, long long second = 0);
} // namespace mplx
