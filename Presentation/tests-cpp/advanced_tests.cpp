#include "frontend.hpp"
#include "vm.hpp"
#include <algorithm>
#include <iostream>
#include <random>

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &text) {
        ++checks;
        if (!condition)
            throw std::runtime_error(text);
    }
    mplx::AnalysisResult analyze(const std::string &source, bool optimize = true) {
        auto a = mplx::analyze_source(source, optimize);
        expect(a.ok(), mplx::diagnostics_json(a.diagnostics));
        return a;
    }
    void typed_ir() {
        auto a = analyze(
            "fn add(a:i32,b:i32)->i32{return a+b;}fn main()->i64{let x:i32=20;return add(x,22);}", false);
        expect(mplx::verify_ir(a.ir).empty(), "valid typed IR");
        expect(a.ir.functions[0].return_type == mplx::ValueType::I32, "return type");
        expect(a.ir.functions[0].parameters == std::vector{mplx::ValueType::I32, mplx::ValueType::I32},
               "parameter types");
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            mplx::VM vm(a.bytecode);
            vm.setJitMode(mode);
            expect(vm.run() == 42, "typed execution");
        }
        for (const auto &source : {"fn main()->i64{return true+1;}", "fn main()->bool{return 1;}",
                                   "fn f(v:bool)->i64{return 1;}fn main()->i64{return f(1);}",
                                   "fn main()->i64{let v:bool=true;v=4;return 1;}"}) {
            auto invalid = mplx::analyze_source(source);
            expect(!invalid.ok(), "type error accepted");
            expect(invalid.diagnostics[0].span.start.offset > 0, "missing semantic source span");
        }
        expect(analyze("fn main()->i64{return i64(true)+1;}").ok(), "explicit bool conversion");
        for (bool optimize : {false, true})
            for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
                auto range = analyze("fn main()->i32{\n let x:i32=2147483647;\n return x+1;\n}", optimize);
                mplx::VM vm(range.bytecode);
                vm.setJitMode(mode);
                try {
                    (void)vm.run();
                    expect(false, "i32 overflow did not trap");
                } catch (const mplx::RuntimeError &e) {
                    expect(e.code() == mplx::RuntimeErrorCode::Arithmetic &&
                               std::string(e.what()).find("i32 range") != std::string::npos,
                           "wrong i32 trap");
                    expect(e.span().start.line == 3, "runtime source map");
                }
            }
        auto malformed = a.ir;
        malformed.functions[0].blocks[0].terminator.value = 999999;
        expect(!mplx::verify_ir(malformed).empty(), "undefined IR return accepted");
        malformed = a.ir;
        malformed.functions[0].blocks[0].instructions[0].type = mplx::ValueType::Bool;
        expect(!mplx::verify_ir(malformed).empty(), "IR type mismatch accepted");
        malformed = a.ir;
        malformed.functions[0].return_type = static_cast<mplx::ValueType>(255);
        expect(!mplx::verify_ir(malformed).empty(), "unknown IR type accepted");
        malformed = a.ir;
        malformed.functions[0].blocks[0].instructions[0].op = static_cast<mplx::IROp>(255);
        expect(!mplx::verify_ir(malformed).empty(), "unknown IR opcode accepted");
        auto bad_map = a.bytecode;
        auto mapping = std::find_if(bad_map.source_map.begin(), bad_map.source_map.end(), [&](const auto &m) {
            return mplx::opcode_info(bad_map.code[m.offset])->operand_bytes > 0;
        });
        expect(mapping != bad_map.source_map.end(), "missing instruction operand in mapping fixture");
        ++mapping->offset;
        expect(!mplx::verify_bytecode(bad_map).ok(), "source mapping inside operand accepted");
        bad_map = a.bytecode;
        bad_map.source_map[1].offset = bad_map.source_map[0].offset;
        expect(!mplx::verify_bytecode(bad_map).ok(), "duplicate source mapping accepted");
        auto branch = analyze("fn main()->i64{let x=1;if(x==1){x=2;}return x;}", false);
        malformed = branch.ir;
        malformed.functions[0].blocks[0].terminator.first = 99999;
        expect(!mplx::verify_ir(malformed).empty(), "invalid IR branch accepted");
        // A virtual register defined only on one predecessor cannot feed the merge.
        auto dom = analyze("fn main()->i64{if(true){return 2;}else{return 3;}}", false);
        malformed = dom.ir;
        auto &f = malformed.functions[0];
        const auto value = f.blocks[1].instructions[0].result;
        f.blocks[0].terminator.value = value;
        expect(!mplx::verify_ir(malformed).empty(), "nondominating value accepted");
        // Locals without an assignment are invalid even if their type matches.
        auto unassigned = analyze("fn main()->i64{let x=1;return x;}", false).ir;
        for (auto &b : unassigned.functions[0].blocks)
            std::erase_if(b.instructions, [](const auto &op) { return op.op == mplx::IROp::Store; });
        expect(!mplx::verify_ir(unassigned).empty(), "uninitialized IR local accepted");
        auto symbols = analyze("fn main()->i64{let x=1;if(true){let x=x+1;x=x+2;}return x;}", false);
        std::vector<uint32_t> xs;
        for (const auto &s : symbols.semantic.symbols)
            if (s.name == "x")
                xs.push_back(s.id);
        expect(xs.size() == 2 && xs[0] != xs[1], "shadowed bindings have no distinct identity");
        size_t outer = 0, inner = 0;
        for (const auto &o : symbols.semantic.occurrences) {
            if (o.symbol == xs[0])
                ++outer;
            if (o.symbol == xs[1])
                ++inner;
        }
        expect(outer == 3 && inner == 3, "compiler occurrence binding");
        auto folding = analyze("fn main()->i64{return (2+3)*(4+5);}", true);
        auto raw = analyze("fn main()->i64{return (2+3)*(4+5);}", false);
        expect(folding.bytecode.code.size() < raw.bytecode.code.size(), "IR constant folding and DCE");
    }
    struct Outcome {
        long long value{0};
        int error{-1};
        uint32_t pc{0};
        std::string function;
    };
    Outcome execute(mplx::VM &vm) {
        try {
            return {vm.run(), -1, 0, ""};
        } catch (const mplx::RuntimeError &error) {
            return {0, static_cast<int>(error.code()), error.pc(), error.function()};
        }
    }
    void compare(const std::string &source, mplx::ExecutionLimits limits = {1000000, 1024, 1000000}) {
        auto a = analyze(source);
        mplx::VM interpreted(a.bytecode), native(a.bytecode);
        interpreted.setJitMode(mplx::VM::JitMode::Off);
        native.setJitMode(mplx::VM::JitMode::On);
        interpreted.setLimits(limits);
        native.setLimits(limits);
        auto x = execute(interpreted), y = execute(native);
        expect(x.value == y.value && x.error == y.error, "native outcome differs");
#if defined(MPLX_WITH_JIT) && (defined(_M_X64) || defined(__x86_64__))
        expect(native.stats().jit_fallbacks == 0, "call module fell back");
        expect(x.pc == y.pc && x.function == y.function,
               "native trap location differs: interp=" + x.function + ":" + std::to_string(x.pc) +
                   " native=" + y.function + ":" + std::to_string(y.pc));
        expect(native.stats().instructions == interpreted.stats().instructions,
               "native call instruction accounting");
        expect(native.stats().peak_operands == interpreted.stats().peak_operands, "native operand peak");
        expect(native.stats().peak_locals == interpreted.stats().peak_locals, "native local peak");
        expect(native.stats().peak_frames == interpreted.stats().peak_frames, "native frame peak");
#endif
    }
    void calls() {
        compare("fn f(n:i64)->i64{if(n<=1){return 1;}return n*f(n-1);}fn main()->i64{return f(10);}");
        compare(
            "fn even(n:i64)->bool{if(n==0){return true;}return odd(n-1);}fn odd(n:i64)->bool{if(n==0){return "
            "false;}return even(n-1);}fn main()->bool{return even(99);}");
        compare("fn f(n:i64)->i64{if(n<=1){return n;}return f(n-1)+f(n-2);}fn main()->i64{return f(12);}");
        compare("fn fail(n:i64)->i64{return 10/n;}fn main()->i64{return 5+fail(0);}");
        compare("fn f(n:i32)->i32{return n+1;}fn main()->i64{return f(2147483647);}");
        const std::string recursive = "fn f(n:i64)->i64{return f(n+1);}fn main()->i64{return f(0);}";
        compare(recursive, {1000000, 40, 1000000});
        compare(recursive, {157, 1000, 1000000});
        compare(recursive, {1000000, 1000, 150});
        compare("fn main()->i64{let x=0;while(x<100){x=x+1;}return x;}", {1000000, 100, 2});
        // 6000 language frames use one host machine frame.
        compare("fn f(n:i64)->i64{if(n==0){return 1;}return f(n-1);}fn main()->i64{return f(6000);}",
                {1000000, 7000, 1000000});
        std::mt19937 rng(0x4d504c58);
        for (int i = 0; i < 100; ++i) {
            auto n = 1 + rng() % 30, a = rng() % 15, b = rng() % 12;
            auto source = "fn step(a:i64,b:i64)->i64{return a*3+b;}fn main()->i64{let i=0;let s=0;while(i<" +
                          std::to_string(n) + "){s=s+step(" + std::to_string(a) + "," + std::to_string(b) +
                          ");i=i+1;}return s;}";
            compare(source);
        }
    }
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return 2;
        if (std::string(argv[1]) == "typed-ir")
            typed_ir();
        else if (std::string(argv[1]) == "native-calls")
            calls();
        else
            return 2;
        std::cout << argv[1] << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
