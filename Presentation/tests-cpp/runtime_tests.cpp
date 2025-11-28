#include "../../Infrastructure/mplx-capi/capi.hpp"
#include "frontend.hpp"
#include "integer.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "verifier.hpp"
#include "vm.hpp"
#include <algorithm>
#include <atomic>
#include <clocale>
#include <functional>
#include <iostream>
#include <random>
#include <string>

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }
    mplx::Bytecode compile(const std::string &source, bool optimize = true) {
        auto a = mplx::analyze_source(source, optimize);
        expect(a.ok(), mplx::diagnostics_json(a.diagnostics));
        return std::move(a.bytecode);
    }
    long long run(const std::string &source, bool optimize = true) {
        mplx::VM vm(compile(source, optimize));
        vm.setJitMode(mplx::VM::JitMode::Off);
        return vm.run();
    }
    void invalid(const std::string &source, const std::string &needle) {
        auto a = mplx::analyze_source(source);
        expect(!a.ok(), "source unexpectedly valid");
        expect(mplx::diagnostics_json(a.diagnostics).find(needle) != std::string::npos,
               "missing diagnostic: " + needle);
    }
    void trap(mplx::VM &vm, mplx::RuntimeErrorCode code) {
        try {
            (void)vm.run();
        } catch (const mplx::RuntimeError &error) {
            expect(error.code() == code, std::string("unexpected trap: ") + error.what());
            return;
        }
        expect(false, "expected runtime trap");
    }
    void arithmetic_trap(const std::string &source) {
        for (bool optimize : {false, true}) {
            auto bc = compile(source, optimize);
            for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
                mplx::VM vm(bc);
                vm.setJitMode(mode);
                trap(vm, mplx::RuntimeErrorCode::Arithmetic);
            }
        }
    }
    void locale_independent_syntax() {
        const auto current = std::setlocale(LC_CTYPE, nullptr);
        struct RestoreLocale {
            std::string original;
            ~RestoreLocale() {
                std::setlocale(LC_CTYPE, original.c_str());
            }
        } restore{current ? current : "C"};
        for (const char *locale : {"C", "C.UTF-8", "Russian_Russia.1251", ".1251", ".1252"}) {
            if (!std::setlocale(LC_CTYPE, locale))
                continue;
            const std::string non_ascii = "fn я()->i64{return 1;}fn main()->i64{return я();}";
            expect(!mplx::analyze_source(non_ascii).ok(), "ASCII identifiers depend on host locale");
            const std::string valid = "// комментарий 😀\nfn main()->i64{let value_42=42;return value_42;}";
            expect(run(valid) == 42, "ASCII syntax/UTF-8 comments depend on host locale");
            for (unsigned byte = 128; byte <= 255; ++byte) {
                const auto tokens = mplx::Lexer(std::string(1, static_cast<char>(byte))).Lex();
                expect(tokens.front().kind == mplx::TokenKind::Invalid,
                       "non-ASCII byte classified as an identifier/number under " + std::string(locale));
            }
        }
    }
    void frontend() {
        locale_independent_syntax();
        expect(run("fn main()->i64{return 2+3*4;}") == 14, "precedence");
        expect(run("fn main()->i64{return (2+3)*4;}") == 20, "parentheses");
        expect(run("fn main()->bool{return true && !false || false;}") == 1, "boolean syntax");
        expect(run("fn main()->i64{return -11%3;}") == -2, "signed modulo");
        expect(run("fn main()->bool{return false && (1/0);}") == 0, "AND short circuit");
        expect(run("fn main()->bool{return true || (1/0);}") == 1, "OR short circuit");
        expect(run("// comment\nfn main()->i64{return 4;}") == 4, "comment");
        invalid("fn main()->i64{return @ 1;}", "unexpected token");
        invalid("fn main()->i64{return 999999999999999999999;}", "out of range");
        invalid("fn main()->i64{let x = 1 return x;}", "expected ';'");
        invalid("fn main()->i64{if(1){let }}", "expected identifier");
        invalid("fn main()->i64{ return 1;", "expected '}'");
        invalid("fn", "expected");
        invalid("fn main()->i64{return " + std::string(200, '(') + "1;}", "nesting");
        std::string chain = "fn main()->i64{return 1";
        for (int i = 0; i < 200; ++i)
            chain += "+1";
        invalid(chain + ";}", "nesting");
        std::string nul = "fn main()->i64{return 1;}";
        nul += '\0';
        nul += "garbage";
        invalid(nul, "expected");
        mplx::Parser empty({});
        expect(empty.parse().functions.empty(), "empty token vector");
        auto a = mplx::analyze_source("fn main()->i64{\n return @;\n}");
        expect(!a.ok() && a.diagnostics[0].line == 2 && a.diagnostics[0].col == 9,
               "diagnostic source position");
        expect(mplx::json_quote("\"\\\n") == "\"\\\"\\\\\\u000a\"", "JSON escaping");
    }
    void compiler() {
        invalid("fn main()->i64{return missing;}", "unknown variable");
        invalid("fn f(a:i64)->i64{return a;} fn main()->i64{return f();}", "argument count");
        invalid("fn f()->i64{return 0;} fn f()->i64{return 1;}", "duplicate function");
        invalid("fn main()->i64{let x=1;let x=2;return x;}", "duplicate local");
        invalid("fn f(a:i64,a:i64)->i64{return a;}", "duplicate parameter");
        invalid("fn main()->i64{if(false){let hidden=2;}return hidden;}", "unknown variable");
        invalid("fn main()->i64{if(false){return missing;}return 0;}", "unknown variable");
        invalid("fn main()->text{return 1;}", "unsupported return type");
        expect(run("fn main()->i64{let x=7;if(true){let x=x+1;x=x+1;}return x;}") == 7, "shadowing");
        expect(run("fn main()->i64{let x=0;if(true){x=8;}return x;}") == 8, "outer assignment");
        expect(run("fn main()->i64{let x=1;while(x<4){let y=x;x=y+1;}return x;}") == 4, "loop locals");
        std::string many = "fn main()->i64{";
        for (int i = 0; i < 300; ++i)
            many += "let v" + std::to_string(i) + "=" + std::to_string(i) + ";";
        expect(run(many + "v299=777;return v299+v5;}") == 782, "wide local encoding");
        auto a = mplx::analyze_source("fn main()->i64{return 42;}");
        mplx::Compiler reused;
        for (int i = 0; i < 3; ++i) {
            auto r = reused.compile(a.module);
            expect(r.diags.empty() && r.bc.functions.size() == 1, "compiler reuse");
            mplx::VM vm(std::move(r.bc));
            expect(vm.run() == 42, "reused compiler result");
        }
    }
    struct Assembler {
        mplx::Bytecode bc{{}, {7}, {{"main", 0, 0, 0}}, {}};
        uint32_t emit(mplx::Op op, uint32_t operand = 0) {
            auto pc = static_cast<uint32_t>(bc.code.size());
            bc.code.push_back(op);
            const auto width = mplx::opcode_info(op)->operand_bytes;
            for (uint32_t i = 0; i < width; ++i)
                bc.code.push_back(static_cast<uint8_t>(operand >> (8 * i)));
            return pc;
        }
        void patch(uint32_t pc, uint32_t target) {
            for (int i = 0; i < 4; ++i)
                bc.code[pc + 1 + i] = static_cast<uint8_t>(target >> (8 * i));
        }
    };
    void rejected(const mplx::Bytecode &bc, const std::string &needle) {
        auto v = mplx::verify_bytecode(bc);
        expect(!v.ok(), "malformed bytecode accepted");
        bool found = false;
        for (const auto &e : v.errors)
            found |= e.message.find(needle) != std::string::npos;
        expect(found, "missing verifier diagnostic: " + needle);
        try {
            mplx::VM vm(bc);
            expect(false, "VM accepted malformed bytecode");
        } catch (const std::runtime_error &error) {
            expect(std::string(error.what()).find("invalid bytecode") != std::string::npos,
                   "constructor verification");
        }
    }
    void verifier() {
        rejected({}, "empty");
        auto valid = compile("fn main()->i64{return 7;}");
        expect(mplx::verify_bytecode(valid).ok(), "valid bytecode");
        auto corrupt = valid;
        corrupt.code[0] = 255;
        rejected(corrupt, "unknown opcode");
        corrupt = valid;
        corrupt.code = {mplx::OP_PUSH_CONST, 0};
        rejected(corrupt, "truncated");
        corrupt = valid;
        const auto decoded = mplx::verify_bytecode(valid);
        const auto constant =
            std::find_if(decoded.instructions.begin(), decoded.instructions.end(),
                         [](const auto &instruction) { return instruction.op == mplx::OP_PUSH_CONST; });
        expect(constant != decoded.instructions.end(), "constant mutation fixture has no constant");
        corrupt.code[constant->offset + 1] = 255;
        rejected(corrupt, "constant index");
        corrupt = valid;
        corrupt.functions[0].entry = 1;
        rejected(corrupt, "function entry");
        corrupt = valid;
        corrupt.functions[0].arity = static_cast<uint8_t>(corrupt.functions[0].locals + 1);
        rejected(corrupt, "arity");
        corrupt = valid;
        corrupt.functions.push_back(corrupt.functions[0]);
        rejected(corrupt, "duplicate");
        Assembler a;
        a.emit(mplx::OP_POP);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "underflow");
        a = {};
        a.emit(mplx::OP_LD0);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "local index");
        a = {};
        a.emit(mplx::OP_CALL, 3);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "callee index");
        a = {};
        a.emit(mplx::OP_JMP, 2);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "jump target");
        a = {};
        a.emit(mplx::OP_PUSH_CONST);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "falls through");
        a = {};
        a.emit(mplx::OP_PUSH_CONST);
        a.emit(mplx::OP_PUSH_CONST);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "exactly one");
        a = {};
        a.emit(mplx::OP_PUSH_CONST);
        auto j = a.emit(mplx::OP_JMP_IF_FALSE);
        a.emit(mplx::OP_PUSH_CONST);
        auto merge = a.emit(mplx::OP_PUSH_CONST);
        a.patch(j, merge);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "stack height");
        a = {};
        a.emit(mplx::OP_PUSH_CONST);
        a.emit(mplx::OP_JMP, 0);
        a.emit(mplx::OP_RET);
        a.emit(mplx::OP_HALT);
        rejected(a.bc, "stack height");
        auto multi = compile("fn f()->i64{return 1;}fn main()->i64{return f();}");
        multi.code[0] = mplx::OP_JMP;
        for (int i = 0; i < 4; ++i)
            multi.code[1 + i] = static_cast<uint8_t>(multi.functions[1].entry >> (8 * i));
        rejected(multi, "jump target");
        expect(mplx::dump_cfg_json(valid).find("\\\"") == std::string::npos, "CFG JSON double escaped");
        std::mt19937 rng(0x4d504c58);
        for (int i = 0; i < 5000; ++i) {
            auto mutated = valid;
            mutated.code.resize(1 + rng() % 80);
            for (auto &b : mutated.code)
                b = static_cast<uint8_t>(rng());
            (void)mplx::verify_bytecode(mutated);
        }
        expect(true, "5000 malformed modules verified safely");
    }
    void runtime() {
        expect(run("fn f(n:i64)->i64{if(n<=1){return 1;}return n*f(n-1);}fn main()->i64{return f(10);}") ==
                   3628800,
               "recursion");
        auto bc = compile("fn main()->i64{let i=0;let sum=0;while(i<10000){sum=sum+i;i=i+1;}return sum;}");
        mplx::VM vm(bc);
        vm.setJitMode(mplx::VM::JitMode::Off);
        expect(vm.run() == 49995000, "sum result");
        expect(vm.stats().peak_operands <= 2, "operand stack grows in loop");
        expect(vm.run() == 49995000, "VM reuse");
        auto snapshot = compile("fn main()->i64{return 42;}");
        mplx::VM owned(snapshot);
        snapshot.code.clear();
        snapshot.consts.clear();
        expect(owned.run() == 42 && owned.runByIndex(0) == 42, "immutable VM snapshot");
        auto badentry = compile("fn main(a:i64)->i64{return a;}");
        mplx::VM entry(badentry);
        trap(entry, mplx::RuntimeErrorCode::InvalidEntry);
        auto loop = compile("fn main()->i64{while(true){}return 0;}");
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            mplx::VM v(loop);
            v.setJitMode(mode);
            v.setLimits({101, 10, 100});
            trap(v, mplx::RuntimeErrorCode::InstructionLimit);
            expect(v.stats().instructions == 101, "exact instruction budget");
            trap(v, mplx::RuntimeErrorCode::InstructionLimit);
        }
        mplx::VM recursive(compile("fn f()->i64{return f();}fn main()->i64{return f();}"));
        recursive.setLimits({1000, 8, 100});
        trap(recursive, mplx::RuntimeErrorCode::FrameLimit);
        vm.setLimits({1000000, 10, 2});
        trap(vm, mplx::RuntimeErrorCode::ValueLimit);
        vm.setLimits({1000000, 10, 100});
        std::atomic_bool cancelled{true};
        vm.setCancellationFlag(&cancelled);
        trap(vm, mplx::RuntimeErrorCode::Cancelled);
        cancelled = false;
        expect(vm.run() == 49995000, "reuse after cancellation");
        arithmetic_trap("fn main()->i64{return 1/0;}");
        arithmetic_trap("fn bad()->i64{return 1/0;}fn main()->i64{return bad()*0;}");
        arithmetic_trap("fn main()->i64{let z=0;return 0/z;}");
        arithmetic_trap("fn main()->i64{return 9223372036854775807+1;}");
        arithmetic_trap("fn main()->i64{return (-9223372036854775807-1)/-1;}");
        arithmetic_trap("fn main()->i64{return (-9223372036854775807-1)%-1;}");
        arithmetic_trap("fn main()->i64{return -(-9223372036854775807-1);}");
        arithmetic_trap("fn main()->i64{return 3037000500*3037000500;}");
        arithmetic_trap("fn main()->i64{let m=(-9223372036854775807-1);return m-1;}");
        expect(run("fn main()->i64{let x=9223372036854775807;return x%3;}") == 1, "division boundary");
        expect(mplx::integer::mul(mplx::integer::min, 1) == mplx::integer::min, "MIN multiply");
        expect(mplx::integer::mul(-3037000499LL, -3037000499LL) == 9223372030926249001LL,
               "negative multiply boundary");
    }
    struct Generated {
        std::string text;
        long long value;
    };
    Generated expression(std::mt19937 &rng, int depth) {
        if (depth == 0 || rng() % 4 == 0) {
            auto value = static_cast<long long>(rng() % 21);
            return {std::to_string(value), value};
        }
        auto a = expression(rng, depth - 1), b = expression(rng, depth - 1);
        switch (rng() % 7) {
        case 0:
            return {"(" + a.text + "+" + b.text + ")", a.value + b.value};
        case 1:
            return {"(" + a.text + "-" + b.text + ")", a.value - b.value};
        case 2:
            return {"(" + a.text + "*" + b.text + ")", a.value * b.value};
        case 3:
            return {"i64(" + a.text + "<" + b.text + ")", a.value < b.value};
        case 4:
            return {"i64(" + a.text + "==" + b.text + ")", a.value == b.value};
        case 5:
            return {"i64(" + a.text + "&&" + b.text + ")", a.value != 0 && b.value != 0};
        default:
            return {"i64(" + a.text + "||" + b.text + ")", a.value != 0 || b.value != 0};
        }
    }
    void differential() {
        std::mt19937 rng(0x4d504c58);
        for (int i = 0; i < 1000; ++i) {
            auto generated = expression(rng, 3);
            auto source = "fn main()->i64{return " + generated.text + ";}";
            for (bool opt : {false, true}) {
                auto bc = compile(source, opt);
                mplx::VM vm(bc);
                vm.setJitMode(mplx::VM::JitMode::Off);
                expect(vm.run() == generated.value, "generated reference mismatch: " + source);
#if defined(MPLX_WITH_JIT)
                mplx::VM native(bc);
                native.setJitMode(mplx::VM::JitMode::On);
                expect(native.run() == generated.value, "native mismatch: " + source);
                expect(native.stats().instructions == vm.stats().instructions,
                       "native instruction accounting");
                expect(native.stats().jit_executions == 1, "generated program fell back");
                expect(native.run() == generated.value && native.stats().jit_compilations == 0,
                       "native cache reuse");
#endif
            }
        }
        for (int i = 0; i < 100; ++i) {
            auto n = 1 + rng() % 40, x = rng() % 30;
            auto source = "fn main()->i64{let i=0;let s=" + std::to_string(x) + ";while(i<" +
                          std::to_string(n) + "){if(i%2==0){s=s+i;}else{s=s-i;}i=i+1;}return s;}";
            long long expected = x;
            for (uint32_t j = 0; j < n; ++j)
                expected += j % 2 == 0 ? static_cast<long long>(j) : -static_cast<long long>(j);
            for (bool opt : {false, true}) {
                auto bc = compile(source, opt);
                mplx::VM vm(bc);
                vm.setJitMode(mplx::VM::JitMode::Off);
                expect(vm.run() == expected, "generated loop mismatch");
#if defined(MPLX_WITH_JIT)
                mplx::VM native(bc);
                native.setJitMode(mplx::VM::JitMode::On);
                expect(native.run() == expected && native.stats().jit_executions == 1,
                       "native loop mismatch");
                expect(native.stats().instructions == vm.stats().instructions,
                       "native loop instruction accounting");
#endif
            }
        }
#if defined(MPLX_WITH_JIT)
        mplx::VM fallback(compile("fn f()->i64{return 9;}fn main()->i64{return f();}"));
        fallback.setJitMode(mplx::VM::JitMode::On);
        expect(fallback.run() == 9 && fallback.stats().jit_executions == 1 &&
                   fallback.stats().native_calls == 1,
               "native call telemetry");
        mplx::VM tiered(compile("fn main()->i64{return 11;}"));
        tiered.setHotThreshold(3);
        for (int i = 1; i <= 3; ++i) {
            expect(tiered.run() == 11, "tiered result");
            expect(tiered.stats().jit_executions == static_cast<uint64_t>(i == 3), "hot threshold");
        }
#endif
    }
    void capi() {
        char *json = nullptr;
        char *error = nullptr;
        long long value = 99;
        expect(mplx_check_source("fn main()->i64{return 8;}", &json, &error) == 0 && json && !error,
               "C API check");
        expect(std::string(json) == "{\"diagnostics\":[]}", "C API JSON");
        mplx_free(json);
        expect(mplx_run_from_source("fn main()->i64{return 8;}", "main", &value, &error) == 0 && value == 8 &&
                   !error,
               "C API run");
        expect(mplx_run_from_source("fn main()->i64{return @;}", "main", &value, &error) == 4 && value == 0 &&
                   error,
               "C API rejects syntax");
        mplx_free(error);
        mplx_runtime_options options{sizeof(options), MPLX_ABI_VERSION, 100, 10, 100};
        expect(mplx_run_from_source_ex("fn main()->i64{while(true){}return 0;}", "main", &options, &value,
                                       &error) == 3,
               "C API instruction budget");
        expect(error && std::string(error).find("instruction limit") != std::string::npos, "C API trap text");
        mplx_free(error);
        options.abi_version = 999;
        expect(mplx_run_from_source_ex("", "main", &options, &value, &error) == 1 && value == 0,
               "C API ABI validation");
        mplx_free(error);
        expect(mplx_run_from_source(nullptr, "main", &value, &error) == 1 && value == 0 && !error,
               "C API output initialization");
    }
} // namespace
int main(int argc, char **argv) {
    const std::string suite = argc > 1 ? argv[1] : "";
    try {
        if (suite == "frontend")
            frontend();
        else if (suite == "compiler")
            compiler();
        else if (suite == "verifier")
            verifier();
        else if (suite == "runtime")
            runtime();
        else if (suite == "differential")
            differential();
        else if (suite == "capi")
            capi();
        else
            throw std::runtime_error("unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << suite << ": " << ex.what() << '\n';
        return 1;
    }
}
