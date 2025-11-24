#pragma once
#include "diagnostic.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mplx {
    using SymbolId = uint32_t;
    constexpr uint32_t no_id = std::numeric_limits<uint32_t>::max();
    enum class IROp : uint8_t { Constant, Load, Store, Unary, Binary, Cast, Call, Parameter, Phi, Copy };
    struct IRInstruction {
        IROp op{IROp::Constant};
        uint32_t result{no_id};
        ValueType type{ValueType::I64};
        std::vector<uint32_t> arguments;
        uint32_t index{0};
        long long constant{0};
        std::string operation;
        SourceSpan span;
        std::vector<uint32_t> incoming_blocks{};
    };
    enum class TerminatorKind : uint8_t { Unset, Jump, Branch, Return };
    struct IRTerminator {
        TerminatorKind kind{TerminatorKind::Unset};
        uint32_t value{no_id}, first{no_id}, second{no_id};
        SourceSpan span;
    };
    struct IRBlock {
        uint32_t id{0};
        std::vector<IRInstruction> instructions;
        IRTerminator terminator;
    };
    struct IRFunction {
        std::string name;
        SymbolId symbol{no_id};
        ValueType return_type{ValueType::I64};
        std::vector<ValueType> parameters, locals, values;
        std::vector<IRBlock> blocks;
        SourceSpan span;
        bool ssa{false};
    };
    struct IRModule {
        std::vector<IRFunction> functions;
    };
    std::vector<Diagnostic> verify_ir(const IRModule &module);
    void optimize_ir(IRModule &module);
    std::string dump_ir_json(const IRModule &module);
} // namespace mplx
