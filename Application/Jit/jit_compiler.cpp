#include "jit_compiler.hpp"
#include "x64_emitter.hpp"
#include "register_allocator.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mplx::jit {
    std::optional<JitCompiled> JitCompiler::compileModule(const VerifiedModule &module,
                                                          NativeOptions options) {
#if !defined(__x86_64__) && !defined(_M_X64)
        (void)module;
        (void)options;
        return std::nullopt;
#else
        const auto &bc = module.bytecode();
        const auto &verified = module.verification();
        if (!verified.ok() || bc.code.size() > 1024 * 1024)
            return std::nullopt;
        for (size_t f = 0; f < bc.functions.size(); ++f)
            if (bc.functions[f].locals + verified.functions[f].max_stack > 4096)
                return std::nullopt;
        constexpr uint32_t exit_label = UINT32_MAX, fuel_trap = UINT32_MAX - 1,
                           overflow_trap = UINT32_MAX - 2, zero_trap = UINT32_MAX - 3,
                           value_trap = UINT32_MAX - 4, i32_trap = UINT32_MAX - 5;
        X64Emitter e;
        const auto machine_ir = lower_machine_ir(module);
        RegisterAllocator registers(e, machine_ir, options.registers);
        uint32_t active_block = no_machine_value;
        // One host frame for the entire execution. MPLX calls jump via explicit heap frames.
        e.emit({0xf3, 0x0f, 0x1e, 0xfa, 0x55, 0x48, 0x89, 0xe5, 0x41, 0x54, 0x41, 0x55});
#if defined(_WIN32)
        e.emit({0x48, 0x83, 0xec, 0x20, 0x49, 0x89, 0xcc}); // shadow space; r12 = rcx
#else
        e.emit({0x49, 0x89, 0xfc}); // r12 = rdi
#endif
        e.context_load_r13(offsetof(ExecutionContext, active_base));
        e.context_load_rax(offsetof(ExecutionContext, entry));
        e.emit({0xff, 0xe0});
        uint32_t function = 0;
        std::vector<uint32_t> order;
        for (uint32_t f = 0; f < bc.functions.size(); ++f)
            order.push_back(f);
        std::sort(order.begin(), order.end(),
                  [&](auto a, auto b) { return bc.functions[a].entry < bc.functions[b].entry; });
        size_t region = 0;
        for (size_t i = 0; i < verified.instructions.size(); ++i) {
            const auto &ins = verified.instructions[i];
            if (ins.op == OP_HALT || verified.stack_heights[i] < 0)
                continue;
            while (region + 1 < order.size() && ins.offset >= bc.functions[order[region + 1]].entry)
                ++region;
            function = order[region];
            auto height = static_cast<uint32_t>(verified.stack_heights[i]);
            const auto &machine = machine_ir.instructions[i];
            if (machine.block != active_block) {
                // Fallthrough flushes precede the target label. Incoming jumps
                // arrive with an already coherent arena and an empty cache.
                registers.flush();
                active_block = machine.block;
            }
            registers.position(ins.offset);
            e.bind(ins.offset);
            e.emit({0xf3, 0x0f, 0x1e, 0xfa});
            e.context_u32(offsetof(ExecutionContext, function), function);
            e.context_u32(offsetof(ExecutionContext, pc), ins.offset);
            e.emit({0x49, 0x83, 0xbc, 0x24});
            e.u32(offsetof(ExecutionContext, remaining));
            e.emit({0x00});
            e.jump(fuel_trap, 0x84);
            e.emit({0x49, 0xff, 0x8c, 0x24});
            e.u32(offsetof(ExecutionContext, remaining));
            if (ins.op == OP_PUSH_CONST || (ins.op >= OP_LD0 && ins.op <= OP_LD3) ||
                ins.op == OP_LOAD_LOCAL || ins.op == OP_LOAD_LOCAL8) {
                e.context_load_rax(offsetof(ExecutionContext, operand_prefix));
                e.emit({0x48, 0x05});
                e.u32(height + 1);
                e.jump(value_trap, 0x82); // unsigned overflow
                e.emit({0x48, 0x89, 0xc1});
                e.context_add_rax(offsetof(ExecutionContext, local_values));
                e.jump(value_trap, 0x82);
                e.context_cmp_rax(offsetof(ExecutionContext, value_limit));
                e.jump(value_trap, 0x87);
                e.emit({0x48, 0x89, 0xc8});
                e.context_cmp_rax(offsetof(ExecutionContext, peak_operands));
                auto skip_peak = 0x40000000u + ins.offset;
                e.jump(skip_peak, 0x86);
                e.context_store_rax(offsetof(ExecutionContext, peak_operands));
                e.bind(skip_peak);
            }
            if (ins.op == OP_PUSH_CONST) {
                e.immediate_rax(static_cast<uint64_t>(bc.consts[ins.operand]));
                registers.store(machine.writes[0]);
                continue;
            }
            if ((ins.op >= OP_LD0 && ins.op <= OP_LD3) || ins.op == OP_LOAD_LOCAL ||
                ins.op == OP_LOAD_LOCAL8) {
                registers.copy(machine.reads[0], machine.writes[0]);
                continue;
            }
            if ((ins.op >= OP_ST0 && ins.op <= OP_ST3) || ins.op == OP_STORE_LOCAL ||
                ins.op == OP_STORE_LOCAL8) {
                registers.copy(machine.reads[0], machine.writes[0]);
                continue;
            }
            if (ins.op == OP_POP)
                continue;
            if (ins.op == OP_CALL) {
                registers.flush();
#if defined(_WIN32)
                e.emit({0x4c, 0x89, 0xe1, 0xba});
                e.u32(ins.operand); // rcx=context; edx=function
                e.emit({0x41, 0xb8});
                e.u32(height); // r8d=caller height
                e.emit({0x4c, 0x8d, 0x0d});
                e.fixup(ins.next); // r9=continuation
#else
                e.emit({0x4c, 0x89, 0xe7, 0xbe});
                e.u32(ins.operand); // rdi=context; esi=function
                e.emit({0xba});
                e.u32(height); // edx=height
                e.emit({0x48, 0x8d, 0x0d});
                e.fixup(ins.next); // rcx=continuation
#endif
                e.immediate_rax(reinterpret_cast<uint64_t>(&native_enter));
                e.emit({0xff, 0xd0, 0x48, 0x85, 0xc0});
                e.jump(exit_label, 0x84);
                e.context_load_r13(offsetof(ExecutionContext, active_base));
                e.emit({0xff, 0xe0});
                continue;
            }
            if (ins.op == OP_RET) {
                registers.load(machine.reads[0]);
                registers.flush();
#if defined(_WIN32)
                e.emit({0x48, 0x89, 0xc2, 0x4c, 0x89, 0xe1}); // rdx=value; rcx=context
#else
                e.emit({0x48, 0x89, 0xc6, 0x4c, 0x89, 0xe7}); // rsi=value; rdi=context
#endif
                e.immediate_rax(reinterpret_cast<uint64_t>(&native_return));
                e.emit({0xff, 0xd0, 0x48, 0x85, 0xc0});
                e.jump(exit_label, 0x84);
                e.context_load_r13(offsetof(ExecutionContext, active_base));
                e.emit({0xff, 0xe0});
                continue;
            }
            if (ins.op == OP_JMP) {
                registers.flush();
                e.jump(ins.operand);
                continue;
            }
            if (ins.op == OP_JMP_IF_FALSE || ins.op == OP_JMP_IF_TRUE) {
                registers.load(machine.reads[0]);
                registers.flush();
                e.emit({0x48, 0x85, 0xc0});
                e.jump(ins.operand, ins.op == OP_JMP_IF_FALSE ? 0x84 : 0x85);
                continue;
            }
            if (ins.op == OP_NEG || ins.op == OP_NOT || ins.op == OP_CHECK_I32) {
                registers.load(machine.reads[0]);
                if (ins.op == OP_NEG) {
                    e.emit({0x48, 0xf7, 0xd8});
                    e.jump(overflow_trap, 0x80);
                } else if (ins.op == OP_NOT)
                    e.emit({0x48, 0x85, 0xc0, 0x0f, 0x94, 0xc0, 0x48, 0x0f, 0xb6, 0xc0});
                else {
                    e.emit({0x48, 0x63, 0xc8, 0x48, 0x39, 0xc8}); // rcx=sign_extend(eax); cmp rax,rcx
                    e.jump(i32_trap, 0x85);
                }
                registers.store(machine.writes[0]);
                continue;
            }
            registers.load(machine.reads[0]);
            registers.load(machine.reads[1], true);
            switch (ins.op) {
            case OP_ADD:
                e.emit({0x48, 0x01, 0xc8});
                e.jump(overflow_trap, 0x80);
                break;
            case OP_SUB:
                e.emit({0x48, 0x29, 0xc8});
                e.jump(overflow_trap, 0x80);
                break;
            case OP_MUL:
                e.emit({0x48, 0x0f, 0xaf, 0xc1});
                e.jump(overflow_trap, 0x80);
                break;
            case OP_DIV:
            case OP_MOD: {
                e.emit({0x48, 0x85, 0xc9});
                e.jump(zero_trap, 0x84);
                e.emit({0x48, 0xba});
                e.u64(0x8000000000000000ULL);
                e.emit({0x48, 0x39, 0xd0});
                auto safe = 0x80000000u + ins.offset;
                e.jump(safe, 0x85);
                e.emit({0x48, 0x83, 0xf9, 0xff});
                e.jump(overflow_trap, 0x84);
                e.bind(safe);
                e.emit({0x48, 0x99, 0x48, 0xf7, 0xf9});
                if (ins.op == OP_MOD)
                    e.emit({0x48, 0x89, 0xd0});
                break;
            }
            case OP_EQ:
            case OP_NE:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE: {
                e.emit({0x48, 0x39, 0xc8});
                const uint8_t cc = ins.op == OP_EQ   ? 0x94
                                   : ins.op == OP_NE ? 0x95
                                   : ins.op == OP_LT ? 0x9c
                                   : ins.op == OP_LE ? 0x9e
                                   : ins.op == OP_GT ? 0x9f
                                                     : 0x9d;
                e.emit({0x0f, cc, 0xc0, 0x48, 0x0f, 0xb6, 0xc0});
                break;
            }
            case OP_AND:
            case OP_OR:
                e.emit({0x48, 0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x48, 0x85, 0xc9, 0x0f, 0x95, 0xc1});
                e.emit({static_cast<uint8_t>(ins.op == OP_AND ? 0x20 : 0x08), 0xc8, 0x48, 0x0f, 0xb6, 0xc0});
                break;
            default:
                return std::nullopt;
            }
            registers.store(machine.writes[0]);
        }
        auto trap = [&](uint32_t label, NativeError error) {
            e.bind(label);
            e.context_u32(offsetof(ExecutionContext, error), static_cast<uint32_t>(error));
            e.jump(exit_label);
        };
        trap(fuel_trap, NativeError::InstructionLimit);
        trap(overflow_trap, NativeError::Overflow);
        trap(zero_trap, NativeError::DivisionByZero);
        trap(value_trap, NativeError::ValueLimit);
        trap(i32_trap, NativeError::I32Range);
        e.bind(exit_label);
        e.context_load_rax(offsetof(ExecutionContext, result));
#if defined(_WIN32)
        e.emit({0x48, 0x83, 0xc4, 0x20});
#endif
        e.emit({0x41, 0x5d, 0x41, 0x5c, 0x5d, 0xc3});
        if (!e.finalize())
            return std::nullopt;
        JitCompiled compiled;
        compiled.memory = plat::ExecutableMemory(plat::alloc_writable(e.bytes.size()));
        if (!compiled.memory.data())
            return std::nullopt;
        std::memcpy(compiled.memory.data(), e.bytes.data(), e.bytes.size());
        if (!compiled.memory.seal())
            return std::nullopt;
        compiled.entry = reinterpret_cast<JitEntryPtr>(compiled.memory.data());
        compiled.size = e.bytes.size();
        compiled.allocation = registers.stats();
        for (const auto &f : bc.functions)
            compiled.functions.push_back(static_cast<uint8_t *>(compiled.memory.data()) +
                                         e.position(f.entry));
        compiled.metadata = std::move(verified.functions);
#if defined(_WIN32)
        char *dump = nullptr;
        size_t size = 0;
        bool enabled = _dupenv_s(&dump, &size, "MPLX_JIT_DUMP") == 0 && dump && std::strcmp(dump, "0") != 0;
        std::free(dump);
#else
        auto *dump = std::getenv("MPLX_JIT_DUMP");
        bool enabled = dump && std::strcmp(dump, "0") != 0;
#endif
        if (enabled)
            std::fprintf(stderr, "[jit] native_module functions=%zu bytes=%zu\n", bc.functions.size(),
                         compiled.size);
        return compiled;
#endif
    }
} // namespace mplx::jit
