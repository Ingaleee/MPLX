#include "frontend.hpp"
#include "lexer.hpp"
#include "parser.hpp"

namespace mplx {
    AnalysisResult analyze_source(const std::string &source, bool optimize) {
        AnalysisResult result;
        if (source.size() > 1024 * 1024) {
            result.diagnostics.push_back({"MPLX1000", "source exceeds 1 MiB limit", 0, 0, {}});
            return result;
        }
        try {
            Lexer lexer(source);
            Parser parser(lexer.Lex());
            result.module = parser.parse();
            result.diagnostics = parser.structuredDiagnostics();
            if (!result.module.imports.empty() && result.diagnostics.empty()) {
                const auto &import = result.module.imports.front();
                result.diagnostics.push_back({"MPLX4000",
                                              "imports require a workspace/file compilation context",
                                              import.span.start.line, import.span.start.column, import.span});
            }
            // Resolve recovered syntax as well, so tools retain useful symbol facts.
            auto semantic = analyze_module(result.module);
            result.semantic = std::move(semantic.model);
            result.ir = std::move(semantic.ir);
            if (result.diagnostics.empty())
                result.diagnostics = std::move(semantic.diagnostics);
            if (!result.ok())
                return result;
            if (optimize)
                result.optimization = run_optimization_pipeline(result.ir);
            auto ir_errors = verify_ir(result.ir);
            result.diagnostics.insert(result.diagnostics.end(), ir_errors.begin(), ir_errors.end());
            if (!result.ok())
                return result;
            Compiler compiler(false);
            auto compiled = compiler.compile(result.ir);
            result.bytecode = std::move(compiled.bc);
            for (const auto &text : compiled.diags)
                result.diagnostics.push_back({"MPLX3002", text, 0, 0, {}});
        } catch (const std::exception &error) {
            result.diagnostics.push_back({"MPLX1002", error.what(), 0, 0, {}});
        }
        return result;
    }
    std::string analysis_json(const AnalysisResult &result) {
        auto out = diagnostics_json(result.diagnostics);
        out.pop_back();
        return out + ",\"schema_version\":1,\"semantic\":" + semantic_json(result.semantic) + "}";
    }
} // namespace mplx
