#pragma once
#include "ast.hpp"
#include "ir.hpp"
#include <span>

namespace mplx {
    enum class SymbolKind : uint8_t { Function, Parameter, Local };
    struct Symbol {
        std::string definition_path;
        std::string original_name;
        bool imported{false};
        bool exported{false};
        SymbolId id{no_id};
        std::string name;
        SymbolKind kind{SymbolKind::Local};
        ValueType type{ValueType::I64};
        uint32_t scope{0}, function{no_id}, slot{no_id};
        uint32_t visible_from{0};
        SourceSpan declaration;
        SourceSpan extent;
        std::vector<ValueType> parameters;
        std::vector<std::string> parameter_names;
    };
    struct Scope {
        uint32_t id{0}, parent{no_id};
        SourceSpan span;
        std::vector<SymbolId> bindings;
    };
    struct Occurrence {
        SymbolId symbol{no_id};
        SourceSpan span;
        bool declaration{false}, write{false};
    };
    struct SemanticModel {
        struct CallSite {
            SymbolId symbol;
            SourceSpan span;
            std::vector<SourceSpan> arguments;
        };
        std::vector<Symbol> symbols;
        std::vector<Scope> scopes;
        std::vector<Occurrence> occurrences;
        std::vector<CallSite> calls;
    };
    struct SemanticResult {
        IRModule ir;
        SemanticModel model;
        std::vector<Diagnostic> diagnostics;
        bool ok() const {
            return diagnostics.empty();
        }
    };
    struct ImportedFunction {
        std::string name;
        std::string definition_path;
        std::string original_name;
        SourceSpan declaration;
        ValueType type{ValueType::I64};
        std::vector<ValueType> parameters;
        std::vector<std::string> parameter_names;
    };
    SemanticResult analyze_module(const Module &module, std::span<const ImportedFunction> imports = {});
    std::string semantic_json(const SemanticModel &model);
} // namespace mplx
