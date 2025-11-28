#include "vm.hpp"
#include "integer.hpp"
#include <algorithm>
#include <iostream>
#include <utility>

namespace mplx {
    RuntimeError::RuntimeError(RuntimeErrorCode code, std::string message, std::string function, uint32_t pc,
                               SourceSpan span)
        : std::runtime_error(message + " [function=" + function + ", pc=" + std::to_string(pc) +
                             (span.end.offset > span.start.offset
                                  ? ", line=" + std::to_string(span.start.line) +
                                        ", col=" + std::to_string(span.start.column)
                                  : "") +
                             "]"),
          code_(code), function_(std::move(function)), pc_(pc), span_(span) {
    }
    VM::VM(Bytecode bc) : VM(CompiledProgram::create(std::move(bc))) {
    }
    VM::VM(std::shared_ptr<const CompiledProgram> program)
        : program_(std::move(program)), bc_(program_ ? program_->module().bytecode()
                                                     : throw std::invalid_argument("null compiled program")),
          verified_(program_->module().verification()) {
#if defined(MPLX_WITH_JIT)
        hot_counts_.resize(bc_.functions.size());
#endif
    }
    void VM::reset() {
        operands_.clear();
        locals_.clear();
        frames_.clear();
        ip_ = current_pc_ = 0;
    }
    [[noreturn]] void VM::fail(RuntimeErrorCode code, const std::string &message) const {
        auto name = frames_.empty() ? std::string("<entry>") : bc_.functions[frames_.back().function].name;
        throw RuntimeError(code, message, name, current_pc_, sourceSpan(current_pc_));
    }
    SourceSpan VM::sourceSpan(uint32_t pc) const {
        auto it = std::lower_bound(bc_.source_map.begin(), bc_.source_map.end(), pc,
                                   [](const auto &a, uint32_t p) { return a.offset < p; });
        return it != bc_.source_map.end() && it->offset == pc ? it->span : SourceSpan{};
    }
    void VM::push(long long value) {
        if (operands_.size() + locals_.size() >= limits_.values)
            fail(RuntimeErrorCode::ValueLimit, "value limit exceeded");
        operands_.push_back(value);
        stats_.peak_operands = std::max<uint64_t>(stats_.peak_operands, operands_.size());
    }
    long long VM::pop() {
        if (frames_.empty() || operands_.size() <= frames_.back().operand_base)
            fail(RuntimeErrorCode::InvalidState, "operand stack underflow");
        auto value = operands_.back();
        operands_.pop_back();
        return value;
    }
    void VM::enter(uint32_t f, uint32_t return_ip) {
        const auto &meta = bc_.functions[f];
        if (frames_.size() >= limits_.frames)
            fail(RuntimeErrorCode::FrameLimit, "frame limit exceeded");
        if (operands_.size() < meta.arity)
            fail(RuntimeErrorCode::InvalidState, "not enough call arguments");
        auto base = operands_.size() - meta.arity, local_base = locals_.size();
        if (base + local_base + meta.locals > limits_.values)
            fail(RuntimeErrorCode::ValueLimit, "value limit exceeded");
        locals_.resize(local_base + meta.locals, 0);
        std::copy(operands_.begin() + base, operands_.end(), locals_.begin() + local_base);
        operands_.resize(base);
        frames_.push_back({return_ip, f, base, local_base});
        stats_.peak_locals = std::max<uint64_t>(stats_.peak_locals, locals_.size());
        stats_.peak_frames = std::max<uint64_t>(stats_.peak_frames, frames_.size());
        ip_ = meta.entry;
    }
    long long VM::run(const std::string &entry, std::span<const long long> arguments) {
        for (uint32_t f = 0; f < bc_.functions.size(); ++f)
            if (bc_.functions[f].name == entry)
                return execute(f, arguments);
        reset();
        stats_ = {};
        fail(RuntimeErrorCode::InvalidEntry, "entry function not found: " + entry);
    }
    long long VM::runByIndex(uint32_t function, std::span<const long long> arguments) {
        return execute(function, arguments);
    }
    long long VM::execute(uint32_t function, std::span<const long long> arguments) {
        reset();
        stats_ = {};
        if (function >= bc_.functions.size())
            fail(RuntimeErrorCode::InvalidEntry, "function index out of bounds");
        const auto &signature = bc_.functions[function];
        auto entry_error = [&](const std::string &message) {
            throw RuntimeError(RuntimeErrorCode::InvalidEntry, message, signature.name, signature.entry,
                               sourceSpan(signature.entry));
        };
        if (arguments.size() != signature.arity)
            entry_error("entry argument count mismatch");
        auto valid_value = [](long long value, ValueType type) {
            return type == ValueType::I64 ||
                   (type == ValueType::I32 && value >= INT32_MIN && value <= INT32_MAX) ||
                   (type == ValueType::Bool && (value == 0 || value == 1));
        };
        for (size_t p = 0; p < arguments.size(); ++p)
            if (!valid_value(arguments[p], signature.parameters[p]))
                entry_error("entry argument " + std::to_string(p) + " violates " +
                            std::string(type_name(signature.parameters[p])) + " type");
        try {
            for (auto argument : arguments)
                push(argument);
            auto finish = [&](long long value) {
                if (!valid_value(value, signature.return_type))
                    throw RuntimeError(RuntimeErrorCode::InvalidState, "entry return violates export type",
                                       signature.name, current_pc_, sourceSpan(current_pc_));
                reset();
                return value;
            };
#if defined(MPLX_WITH_JIT)
            if (jit_mode_ != JitMode::Off && !trace_enabled_ && !cancellation_) {
                if (hot_counts_[function] < UINT32_MAX)
                    ++hot_counts_[function];
                if (jit_mode_ == JitMode::On || hot_counts_[function] >= hot_threshold_) {
                    auto native = program_->native();
                    stats_.jit_compilations = native.compiled ? 1 : 0;
                    if (native.code) {
                        ++stats_.jit_executions;
                        auto result = jit::execute_native(bc_, *native.code, function, limits_.instructions,
                                                          limits_.frames, limits_.values, arguments);
                        stats_.instructions = result.instructions;
                        stats_.peak_operands = result.peak_operands;
                        stats_.peak_locals = result.peak_locals;
                        stats_.peak_frames = result.peak_frames;
                        stats_.native_calls = result.native_calls;
                        stats_.jit_executions = result.peak_frames ? 1 : 0;
                        if (result.error != jit::NativeError::None) {
                            RuntimeErrorCode code = RuntimeErrorCode::InvalidState;
                            std::string message = "native runtime transition failure";
                            switch (result.error) {
                            case jit::NativeError::InstructionLimit:
                                code = RuntimeErrorCode::InstructionLimit;
                                message = "instruction limit exceeded";
                                break;
                            case jit::NativeError::FrameLimit:
                                code = RuntimeErrorCode::FrameLimit;
                                message = "frame limit exceeded";
                                break;
                            case jit::NativeError::ValueLimit:
                                code = RuntimeErrorCode::ValueLimit;
                                message = "value limit exceeded";
                                break;
                            case jit::NativeError::Overflow:
                                code = RuntimeErrorCode::Arithmetic;
                                message = "integer overflow";
                                break;
                            case jit::NativeError::DivisionByZero:
                                code = RuntimeErrorCode::Arithmetic;
                                message = "division by zero";
                                break;
                            case jit::NativeError::I32Range:
                                code = RuntimeErrorCode::Arithmetic;
                                message = "i32 range exceeded";
                                break;
                            default:
                                break;
                            }
                            throw RuntimeError(code, message,
                                               result.peak_frames ? bc_.functions[result.function].name
                                                                  : "<entry>",
                                               result.pc, sourceSpan(result.pc));
                        }
                        current_pc_ = result.pc;
                        return finish(result.value);
                    }
                }
                ++stats_.jit_fallbacks;
            } else if (jit_mode_ != JitMode::Off)
                ++stats_.jit_fallbacks;
#endif
            enter(function, 0);
            auto value = interpret();
            return finish(value);
        } catch (...) {
            reset();
            throw;
        }
    }
    long long VM::interpret() {
        for (;;) {
            current_pc_ = ip_;
            if (cancellation_ && cancellation_->load(std::memory_order_relaxed))
                fail(RuntimeErrorCode::Cancelled, "execution cancelled");
            if (stats_.instructions >= limits_.instructions)
                fail(RuntimeErrorCode::InstructionLimit, "instruction limit exceeded");
            Instruction ins;
            std::string error;
            if (!decode_instruction(bc_, ip_, ins, error))
                fail(RuntimeErrorCode::InvalidState, error);
            ip_ = ins.next;
            if (trace_enabled_ && (!trace_limit_ || stats_.instructions < trace_limit_))
                std::cerr << "pc=" << current_pc_ << " op=" << opcode_info(ins.op)->name
                          << " sp=" << operands_.size() << '\n';
            ++stats_.instructions;
            auto local_index = [&]() -> size_t {
                uint32_t index = ins.operand;
                if (ins.op >= OP_LD0 && ins.op <= OP_LD3)
                    index = ins.op - OP_LD0;
                if (ins.op >= OP_ST0 && ins.op <= OP_ST3)
                    index = ins.op - OP_ST0;
                return frames_.back().local_base + index;
            };
            try {
                switch (ins.op) {
                case OP_PUSH_CONST:
                    push(bc_.consts[ins.operand]);
                    break;
                case OP_LD0:
                case OP_LD1:
                case OP_LD2:
                case OP_LD3:
                case OP_LOAD_LOCAL8:
                case OP_LOAD_LOCAL:
                    push(locals_[local_index()]);
                    break;
                case OP_ST0:
                case OP_ST1:
                case OP_ST2:
                case OP_ST3:
                case OP_STORE_LOCAL8:
                case OP_STORE_LOCAL: {
                    auto value = pop();
                    locals_[local_index()] = value;
                    push(value);
                    break;
                }
                case OP_ADD: {
                    auto b = pop(), a = pop();
                    push(integer::add(a, b));
                    break;
                }
                case OP_SUB: {
                    auto b = pop(), a = pop();
                    push(integer::sub(a, b));
                    break;
                }
                case OP_MUL: {
                    auto b = pop(), a = pop();
                    push(integer::mul(a, b));
                    break;
                }
                case OP_DIV: {
                    auto b = pop(), a = pop();
                    push(integer::div(a, b));
                    break;
                }
                case OP_MOD: {
                    auto b = pop(), a = pop();
                    push(integer::mod(a, b));
                    break;
                }
                case OP_NEG:
                    push(integer::neg(pop()));
                    break;
                case OP_CHECK_I32: {
                    auto value = pop();
                    if (value < INT32_MIN || value > INT32_MAX)
                        throw std::runtime_error("i32 range exceeded");
                    push(value);
                    break;
                }
                case OP_EQ: {
                    auto b = pop(), a = pop();
                    push(a == b);
                    break;
                }
                case OP_NE: {
                    auto b = pop(), a = pop();
                    push(a != b);
                    break;
                }
                case OP_LT: {
                    auto b = pop(), a = pop();
                    push(a < b);
                    break;
                }
                case OP_LE: {
                    auto b = pop(), a = pop();
                    push(a <= b);
                    break;
                }
                case OP_GT: {
                    auto b = pop(), a = pop();
                    push(a > b);
                    break;
                }
                case OP_GE: {
                    auto b = pop(), a = pop();
                    push(a >= b);
                    break;
                }
                case OP_AND: {
                    auto b = pop(), a = pop();
                    push(a != 0 && b != 0);
                    break;
                }
                case OP_OR: {
                    auto b = pop(), a = pop();
                    push(a != 0 || b != 0);
                    break;
                }
                case OP_NOT:
                    push(pop() == 0);
                    break;
                case OP_JMP:
                    ip_ = ins.operand;
                    break;
                case OP_JMP_IF_FALSE:
                    if (pop() == 0)
                        ip_ = ins.operand;
                    break;
                case OP_JMP_IF_TRUE:
                    if (pop() != 0)
                        ip_ = ins.operand;
                    break;
                case OP_CALL:
                    enter(ins.operand, ip_);
                    break;
                case OP_RET: {
                    auto value = pop();
                    auto frame = frames_.back();
                    frames_.pop_back();
                    operands_.resize(frame.operand_base);
                    locals_.resize(frame.local_base);
                    if (frames_.empty())
                        return value;
                    ip_ = frame.return_ip;
                    push(value);
                    break;
                }
                case OP_POP:
                    (void)pop();
                    break;
                default:
                    fail(RuntimeErrorCode::InvalidState, "unexpected terminal opcode");
                }
            } catch (const RuntimeError &) {
                throw;
            } catch (const std::runtime_error &ex) {
                fail(RuntimeErrorCode::Arithmetic, ex.what());
            }
        }
    }
} // namespace mplx
