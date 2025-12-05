#include "frontend.hpp"
#include "ir_analysis.hpp"
#include "ir_passes.hpp"
#include "ssa.hpp"
#include "vm.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>

namespace {
    size_t checks = 0;
    void expect(bool condition, const std::string &message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }
    mplx::AnalysisResult raw(const std::string &source) {
        auto result = mplx::analyze_source(source, false);
        expect(result.ok(), mplx::diagnostics_json(result.diagnostics));
        return result;
    }
    void compare(const std::string &source, long long expected) {
        auto reference = raw(source);
        auto optimized = mplx::analyze_source(source, true);
        expect(optimized.ok(), mplx::diagnostics_json(optimized.diagnostics));
        expect(optimized.optimization.passes.size() == 7, "missing verified optimization passes");
        expect(mplx::verify_ir(optimized.ir).empty(), "optimized SSA verification");
        for (const auto *bytecode : {&reference.bytecode, &optimized.bytecode}) {
            mplx::VM vm(*bytecode);
            vm.setJitMode(mplx::VM::JitMode::Off);
            expect(vm.run() == expected, "SSA reference value mismatch");
#if defined(MPLX_WITH_JIT)
            mplx::VM native(*bytecode);
            native.setJitMode(mplx::VM::JitMode::On);
            expect(native.run() == expected, "SSA native value mismatch");
            expect(native.stats().jit_executions == 1, "SSA fixture fell back from native code");
            expect(native.stats().instructions == vm.stats().instructions, "SSA backend fuel mismatch");
#endif
        }
    }
    mplx::IRModule critical_edge() {
        using namespace mplx;
        IRFunction function;
        function.name = "choose";
        function.parameters = function.locals = {ValueType::Bool};
        function.return_type = ValueType::I64;
        function.values = {ValueType::Bool, ValueType::I64, ValueType::I64, ValueType::I64};
        function.ssa = true;
        IRBlock entry;
        entry.instructions = {{IROp::Parameter, 0, ValueType::Bool, {}, 0, 0, "", {}},
                              {IROp::Constant, 1, ValueType::I64, {}, 0, 11, "", {}}};
        entry.terminator = {TerminatorKind::Branch, 0, 1, 2, {}};
        IRBlock left;
        left.id = 1;
        left.instructions = {{IROp::Constant, 2, ValueType::I64, {}, 0, 22, "", {}}};
        left.terminator = {TerminatorKind::Jump, no_id, 2, no_id, {}};
        IRBlock merge;
        merge.id = 2;
        merge.instructions = {{IROp::Phi, 3, ValueType::I64, {1, 2}, 0, 0, "", {}, {0, 1}}};
        merge.terminator = {TerminatorKind::Return, 3, no_id, no_id, {}};
        function.blocks = {entry, left, merge};
        return {{function}};
    }
    void verifier_and_edges() {
        auto module = critical_edge();
        expect(mplx::verify_ir(module).empty(), "critical-edge SSA fixture");
        auto bytecode = mplx::Compiler(false).compile(module);
        expect(bytecode.diags.empty(), "critical-edge lowering");
        for (long long argument : {0, 1}) {
            std::array<long long, 1> arguments{argument};
            for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
                mplx::VM vm(bytecode.bc);
                vm.setJitMode(mode);
                expect(vm.run("choose", arguments) == (argument ? 22 : 11),
                       "critical edge selects wrong copy");
            }
        }
        auto corrupt = module;
        corrupt.functions[0].blocks[2].instructions[0].incoming_blocks = {0, 0};
        expect(!mplx::verify_ir(corrupt).empty(), "duplicate phi predecessor accepted");
        corrupt = module;
        corrupt.functions[0].blocks[2].instructions[0].arguments[0] = 2;
        expect(!mplx::verify_ir(corrupt).empty(), "phi edge accepted a nondominating definition");
        corrupt = module;
        corrupt.functions[0].blocks[2].instructions[0].arguments[0] = 0;
        expect(!mplx::verify_ir(corrupt).empty(), "phi type mismatch accepted");
        corrupt = module;
        corrupt.functions[0].blocks[2].instructions[0].incoming_blocks.pop_back();
        expect(!mplx::verify_ir(corrupt).empty(), "incomplete phi metadata accepted");
        corrupt = module;
        corrupt.functions[0].blocks[2].instructions[0].arguments[0] = mplx::no_id;
        expect(!mplx::verify_ir(corrupt).empty(), "invalid phi value accepted");
        corrupt = module;
        corrupt.functions[0].blocks[0].terminator.first = 0;
        expect(!mplx::verify_ir(corrupt).empty(), "SSA entry backedge accepted without invocation input");
        auto cfg = mplx::analyze_control_flow(module.functions[0]);
        expect(cfg.dominates(0, 2) && !cfg.dominates(1, 2), "CFG dominance query");
        expect(cfg.frontier[1] == std::vector<uint32_t>{2}, "dominance frontier");
    }
    void passes() {
        auto module = raw("fn main()->i64{let x=1;return x;}").ir;
        const auto before = mplx::dump_ir_json(module);
        mplx::IRPassManager manager;
        manager.add("corrupt-return",
                    [](auto &candidate) { candidate.functions[0].blocks[0].terminator.value = mplx::no_id; });
        try {
            manager.run(module);
            expect(false, "invalid pass result published");
        } catch (const std::runtime_error &error) {
            expect(std::string(error.what()).find("corrupt-return") != std::string::npos,
                   "missing pass identity");
        }
        expect(mplx::dump_ir_json(module) == before, "failed pass mutated published IR");
        auto constant = mplx::analyze_source("fn main()->i64{let x=7;if(x==7){x=42;}else{x=99;}return x;}");
        expect(constant.ok(), "constant branch optimization");
        bool branch = false;
        for (const auto &block : constant.ir.functions[0].blocks) {
            branch |= block.terminator.kind == mplx::TerminatorKind::Branch;
            for (const auto &instruction : block.instructions)
                expect(instruction.op != mplx::IROp::Load && instruction.op != mplx::IROp::Store,
                       "mutable operation survived SSA promotion");
        }
        expect(!branch, "SCCP did not remove constant branch");
        compare("fn main()->i64{let a=1;let b=2;let i=0;while(i<5){let t=a;a=b;b=t;i=i+1;}return a*10+b;}",
                21);
        compare("fn main()->i64{let a=1;let b=2;let c=3;let i=0;while(i<7){let t=a;a=b;b=c;c=t;i=i+1;}return "
                "a*100+b*10+c;}",
                231);
        compare("fn main()->i64{let x=0;let i=0;while(i<6){let "
                "j=0;while(j<4){if((i+j)%2==0){x=x+1;}else{x=x+2;}j=j+1;}i=i+1;}return x;}",
                36);
        compare("fn main()->i64{let x=1;let i=0;while(i<3){let x=x+10;if(x>10){let x=99;}i=i+1;}return x;}",
                1);
    }
    void generated() {
        std::mt19937 random(0x53534137);
        for (int sample = 0; sample < 400; ++sample) {
            auto count = random() % 19;
            long long first = random() % 20, second = random() % 20, total = 0;
            const auto start_first = first, start_second = second;
            for (unsigned iteration = 0; iteration < count; ++iteration) {
                std::swap(first, second);
                first += iteration % 3;
                total += first - second;
            }
            const auto source = "fn main()->i64{let a=" + std::to_string(start_first) +
                                ";let b=" + std::to_string(start_second) + ";let s=0;let i=0;while(i<" +
                                std::to_string(count) + "){let t=a;a=b;b=t;a=a+i%3;s=s+a-b;i=i+1;}return s;}";
            compare(source, total);
        }
    }
} // namespace

int main() {
    try {
        verifier_and_edges();
        passes();
        generated();
        std::cout << "SSA: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
