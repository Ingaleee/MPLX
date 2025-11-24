#pragma once
#include "compiler.hpp"
#include "ir_passes.hpp"
#include "semantic.hpp"

namespace mplx {
    struct AnalysisResult {
        Module module;
        IRModule ir;
        SemanticModel semantic;
        Bytecode bytecode;
        std::vector<Diagnostic> diagnostics;
        OptimizationReport optimization;
        bool ok() const {
            return diagnostics.empty();
        }
    };
    AnalysisResult analyze_source(const std::string &source, bool optimize = true);
    std::string analysis_json(const AnalysisResult &result);
} // namespace mplx
