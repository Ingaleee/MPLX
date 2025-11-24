#include "semantic.hpp"
#include <algorithm>
#include <limits>
#include <unordered_map>

namespace mplx {
    namespace {
        class Analyzer {
          public:
            SemanticResult result;
            SemanticResult analyze(const Module &module, std::span<const ImportedFunction> imports) {
                SourceSpan range;
                if (!module.functions.empty())
                    range = join(module.functions.front().span, module.functions.back().span);
                result.model.scopes.push_back({0, no_id, range, {}});
                scopes_.push_back({});
                scope_ids_.push_back(0);
                if (module.functions.empty())
                    error("MPLX2000", "module contains no functions", range);
                for (uint32_t i = 0; i < module.functions.size(); ++i) {
                    const auto &f = module.functions[i];
                    auto type = parse_type(f.returnType);
                    if (type == ValueType::Invalid)
                        error("MPLX2002", "unsupported return type: " + f.returnType, f.name_span);
                    if (parse_type(f.name) != ValueType::Invalid)
                        error("MPLX2003", "function name is reserved for a type conversion: " + f.name,
                              f.name_span);
                    if (f.params.size() > 255)
                        error("MPLX2004", "too many parameters: " + f.name, f.name_span);
                    Symbol symbol;
                    symbol.name = f.name;
                    symbol.original_name = f.name;
                    symbol.exported = f.exported;
                    symbol.kind = SymbolKind::Function;
                    symbol.type = type;
                    symbol.function = i;
                    symbol.declaration = f.name_span;
                    symbol.extent = f.span;
                    for (const auto &p : f.params) {
                        auto parameter = parse_type(p.typeName);
                        if (parameter == ValueType::Invalid)
                            error("MPLX2002", "unsupported parameter type: " + p.typeName, p.name_span);
                        symbol.parameters.push_back(parameter);
                        symbol.parameter_names.push_back(p.name);
                    }
                    auto id = declare(std::move(symbol), "duplicate function");
                    functions_.push_back(id);
                }
                for (size_t i = 0; i < imports.size(); ++i) {
                    const auto &import = imports[i];
                    Symbol symbol;
                    symbol.name = import.name;
                    symbol.original_name = import.original_name;
                    symbol.definition_path = import.definition_path;
                    symbol.declaration = import.declaration;
                    symbol.kind = SymbolKind::Function;
                    symbol.type = import.type;
                    symbol.parameters = import.parameters;
                    symbol.parameter_names = import.parameter_names;
                    symbol.function = static_cast<uint32_t>(module.functions.size() + i);
                    symbol.imported = true;
                    symbol.exported = true;
                    declare(std::move(symbol), "duplicate imported function");
                    // Imported declarations belong to another document, not this document's tokens.
                    result.model.occurrences.pop_back();
                }
                for (uint32_t i = 0; i < module.functions.size(); ++i)
                    function(module.functions[i], i);
                for (const auto &import : imports) {
                    IRFunction prototype;
                    prototype.name = import.name;
                    prototype.return_type = import.type;
                    prototype.parameters = prototype.locals = import.parameters;
                    prototype.values = {import.type};
                    IRBlock body;
                    body.instructions.push_back({IROp::Constant, 0, import.type, {}, 0, 0, "", {}});
                    body.terminator = {TerminatorKind::Return, 0, no_id, no_id, {}};
                    prototype.blocks.push_back(std::move(body));
                    result.ir.functions.push_back(std::move(prototype));
                }
                if (result.ok()) {
                    auto diagnostics = verify_ir(result.ir);
                    result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(),
                                              diagnostics.end());
                }
                return std::move(result);
            }

          private:
            std::vector<std::unordered_map<std::string, SymbolId>> scopes_;
            std::vector<uint32_t> scope_ids_, functions_;
            uint32_t function_{0}, block_{0};
            IRFunction &fn() {
                return result.ir.functions[function_];
            }
            IRBlock &block() {
                return fn().blocks[block_];
            }
            void error(const char *code, const std::string &text, SourceSpan span) {
                result.diagnostics.push_back({code, text, span.start.line, span.start.column, span});
            }
            SymbolId declare(Symbol symbol, const char *duplicate) {
                const auto scope = scope_ids_.back();
                if (scopes_.back().contains(symbol.name))
                    error("MPLX2005", std::string(duplicate) + ": " + symbol.name, symbol.declaration);
                symbol.id = static_cast<SymbolId>(result.model.symbols.size());
                symbol.scope = scope;
                auto id = symbol.id;
                scopes_.back()[symbol.name] = id;
                result.model.scopes[scope].bindings.push_back(id);
                result.model.occurrences.push_back({id, symbol.declaration, true, false});
                result.model.symbols.push_back(std::move(symbol));
                return id;
            }
            SymbolId resolve(const std::string &name, SourceSpan span, bool write = false) {
                for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s) {
                    auto it = s->find(name);
                    if (it != s->end()) {
                        result.model.occurrences.push_back({it->second, span, false, write});
                        return it->second;
                    }
                }
                error("MPLX2006", "unknown variable: " + name, span);
                return no_id;
            }
            uint32_t addBlock() {
                auto id = static_cast<uint32_t>(fn().blocks.size());
                fn().blocks.push_back({id, {}, {}});
                return id;
            }
            void ensureOpen() {
                if (block().terminator.kind != TerminatorKind::Unset)
                    block_ = addBlock();
            }
            uint32_t local(ValueType type, SourceSpan span) {
                if (fn().locals.size() >= 65535) {
                    error("MPLX2007", "too many local variables", span);
                    return 0;
                }
                auto slot = static_cast<uint32_t>(fn().locals.size());
                fn().locals.push_back(type);
                return slot;
            }
            uint32_t instruction(IRInstruction value) {
                if (value.op != IROp::Store) {
                    value.result = static_cast<uint32_t>(fn().values.size());
                    fn().values.push_back(value.type);
                }
                auto id = value.result;
                block().instructions.push_back(std::move(value));
                return id;
            }
            uint32_t constant(long long value, ValueType type, SourceSpan span) {
                return instruction({IROp::Constant, no_id, type, {}, 0, value, "", span});
            }
            uint32_t load(uint32_t slot, SourceSpan span) {
                return instruction({IROp::Load, no_id, fn().locals[slot], {}, slot, 0, "", span});
            }
            void store(uint32_t slot, uint32_t value, SourceSpan span) {
                instruction({IROp::Store, no_id, fn().locals[slot], {value}, slot, 0, "", span});
            }
            uint32_t convert(uint32_t value, ValueType type, SourceSpan span, bool explicit_cast = false) {
                auto from = fn().values[value];
                if (from == type)
                    return value;
                if ((!integer_type(from) || !integer_type(type)) && !explicit_cast) {
                    error("MPLX2010",
                          std::string("cannot convert ") + type_name(from) + " to " + type_name(type), span);
                    return constant(0, type == ValueType::Invalid ? ValueType::I64 : type, span);
                }
                return instruction({IROp::Cast, no_id, type, {value}, 0, 0, "", span});
            }
            uint32_t condition(uint32_t value, SourceSpan span) {
                return convert(value, ValueType::Bool, span, true);
            }
            uint32_t expression(const Expr *e) {
                if (!e) {
                    error("MPLX2011", "missing expression", {});
                    return constant(0, ValueType::I64, {});
                }
                if (auto v = dynamic_cast<const LiteralExpr *>(e))
                    return constant(v->value, v->is_bool ? ValueType::Bool : ValueType::I64, e->span);
                if (auto v = dynamic_cast<const VarExpr *>(e)) {
                    auto id = resolve(v->name, e->span);
                    if (id == no_id)
                        return constant(0, ValueType::I64, e->span);
                    const auto &symbol = result.model.symbols[id];
                    if (symbol.kind == SymbolKind::Function) {
                        error("MPLX2012", "function requires a call: " + v->name, e->span);
                        return constant(0, ValueType::I64, e->span);
                    }
                    return load(symbol.slot, e->span);
                }
                if (auto unary = dynamic_cast<const UnaryExpr *>(e)) {
                    auto value = expression(unary->rhs.get());
                    auto type = fn().values[value];
                    if (unary->op == "!") {
                        value = condition(value, unary->rhs->span);
                        type = ValueType::Bool;
                    } else if (!integer_type(type)) {
                        error("MPLX2013", "unary '-' requires an integer", unary->operator_span);
                        return constant(0, ValueType::I64, e->span);
                    }
                    return instruction({IROp::Unary, no_id, type, {value}, 0, 0, unary->op, e->span});
                }
                if (auto binary = dynamic_cast<const BinaryExpr *>(e)) {
                    const auto &op = binary->op;
                    auto lhs = expression(binary->lhs.get());
                    if (op == "&&" || op == "||") {
                        lhs = condition(lhs, binary->lhs->span);
                        auto shortcut = addBlock(), rhs_block = addBlock(), done = addBlock();
                        auto slot = local(ValueType::Bool, e->span);
                        block().terminator = {TerminatorKind::Branch, lhs, op == "&&" ? rhs_block : shortcut,
                                              op == "&&" ? shortcut : rhs_block, binary->operator_span};
                        block_ = shortcut;
                        auto value = constant(op == "||" ? 1 : 0, ValueType::Bool, e->span);
                        store(slot, value, e->span);
                        block().terminator = {TerminatorKind::Jump, no_id, done, no_id, e->span};
                        block_ = rhs_block;
                        auto rhs = condition(expression(binary->rhs.get()), binary->rhs->span);
                        store(slot, rhs, e->span);
                        block().terminator = {TerminatorKind::Jump, no_id, done, no_id, e->span};
                        block_ = done;
                        return load(slot, e->span);
                    }
                    auto rhs = expression(binary->rhs.get());
                    auto left = fn().values[lhs], right = fn().values[rhs];
                    const bool equality = op == "==" || op == "!=";
                    const bool comparison = equality || op == "<" || op == "<=" || op == ">" || op == ">=";
                    if (equality && left == ValueType::Bool && right == ValueType::Bool)
                        return instruction(
                            {IROp::Binary, no_id, ValueType::Bool, {lhs, rhs}, 0, 0, op, e->span});
                    if (!integer_type(left) || !integer_type(right)) {
                        error("MPLX2014", "operator '" + op + "' requires compatible integer operands",
                              binary->operator_span);
                        return constant(0, comparison ? ValueType::Bool : ValueType::I64, e->span);
                    }
                    auto type =
                        left == ValueType::I64 || right == ValueType::I64 ? ValueType::I64 : ValueType::I32;
                    lhs = convert(lhs, type, binary->lhs->span);
                    rhs = convert(rhs, type, binary->rhs->span);
                    return instruction({IROp::Binary,
                                        no_id,
                                        comparison ? ValueType::Bool : type,
                                        {lhs, rhs},
                                        0,
                                        0,
                                        op,
                                        e->span});
                }
                if (auto call = dynamic_cast<const CallExpr *>(e)) {
                    auto cast = parse_type(call->callee);
                    if (cast != ValueType::Invalid) {
                        if (call->args.size() != 1) {
                            error("MPLX2015", "type conversion requires one argument", call->name_span);
                            return constant(0, cast, e->span);
                        }
                        return convert(expression(call->args[0].get()), cast, e->span, true);
                    }
                    SymbolId id = no_id;
                    for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s) {
                        auto it = s->find(call->callee);
                        if (it != s->end()) {
                            id = it->second;
                            break;
                        }
                    }
                    if (id == no_id) {
                        error("MPLX2016", "unknown function: " + call->callee, call->name_span);
                        for (const auto &a : call->args)
                            (void)expression(a.get());
                        return constant(0, ValueType::I64, e->span);
                    }
                    result.model.occurrences.push_back({id, call->name_span, false, false});
                    SemanticModel::CallSite call_site{id, e->span, {}};
                    for (const auto &argument : call->args)
                        call_site.arguments.push_back(argument->span);
                    result.model.calls.push_back(std::move(call_site));
                    auto symbol = result.model.symbols[id];
                    if (symbol.kind != SymbolKind::Function) {
                        error("MPLX2017", "symbol is not callable: " + call->callee, call->name_span);
                        return constant(0, ValueType::I64, e->span);
                    }
                    if (call->args.size() != symbol.parameters.size())
                        error("MPLX2018",
                              "wrong argument count for " + call->callee + ": expected " +
                                  std::to_string(symbol.parameters.size()),
                              call->name_span);
                    std::vector<uint32_t> args;
                    for (size_t i = 0; i < call->args.size(); ++i) {
                        auto value = expression(call->args[i].get());
                        if (i < symbol.parameters.size())
                            value = convert(value, symbol.parameters[i], call->args[i]->span);
                        args.push_back(value);
                    }
                    return instruction(
                        {IROp::Call, no_id, symbol.type, std::move(args), symbol.function, 0, "", e->span});
                }
                error("MPLX2011", "unknown expression", e->span);
                return constant(0, ValueType::I64, e->span);
            }
            void body(const std::vector<std::unique_ptr<Stmt>> &statements, SourceSpan span,
                      bool nested = true) {
                if (nested) {
                    auto id = static_cast<uint32_t>(result.model.scopes.size());
                    result.model.scopes.push_back({id, scope_ids_.back(), span, {}});
                    scope_ids_.push_back(id);
                    scopes_.push_back({});
                }
                for (const auto &stmt : statements) {
                    ensureOpen();
                    statement(stmt.get());
                }
                if (nested) {
                    scope_ids_.pop_back();
                    scopes_.pop_back();
                }
            }
            void statement(const Stmt *s) {
                if (!s)
                    return;
                if (auto let = dynamic_cast<const LetStmt *>(s)) {
                    auto value = expression(let->init.get());
                    auto type = fn().values[value];
                    if (!let->typeName.empty()) {
                        type = parse_type(let->typeName);
                        if (type == ValueType::Invalid) {
                            error("MPLX2002", "unsupported local type: " + let->typeName, let->name_span);
                            type = ValueType::I64;
                        }
                        value = convert(value, type, let->init->span);
                    }
                    auto slot = local(type, let->name_span);
                    Symbol symbol;
                    symbol.name = let->name;
                    symbol.type = type;
                    symbol.kind = SymbolKind::Local;
                    symbol.function = function_;
                    symbol.slot = slot;
                    symbol.declaration = let->name_span;
                    symbol.visible_from = s->span.end.offset;
                    declare(std::move(symbol), "duplicate local");
                    store(slot, value, s->span);
                    return;
                }
                if (auto assign = dynamic_cast<const AssignStmt *>(s)) {
                    auto id = resolve(assign->name, assign->name_span, true);
                    auto value = expression(assign->value.get());
                    if (id != no_id) {
                        auto symbol = result.model.symbols[id];
                        if (symbol.kind == SymbolKind::Function)
                            error("MPLX2019", "cannot assign to function", assign->name_span);
                        else {
                            value = convert(value, symbol.type, assign->value->span);
                            store(symbol.slot, value, s->span);
                        }
                    }
                    return;
                }
                if (auto ret = dynamic_cast<const ReturnStmt *>(s)) {
                    auto value = convert(expression(ret->value.get()), fn().return_type, ret->value->span);
                    block().terminator = {TerminatorKind::Return, value, no_id, no_id, s->span};
                    return;
                }
                if (auto expr = dynamic_cast<const ExprStmt *>(s)) {
                    (void)expression(expr->expr.get());
                    return;
                }
                if (auto cond = dynamic_cast<const IfStmt *>(s)) {
                    auto value = condition(expression(cond->cond.get()), cond->cond->span);
                    auto yes = addBlock(), no = addBlock(), done = addBlock();
                    block().terminator = {TerminatorKind::Branch, value, yes, no, cond->cond->span};
                    block_ = yes;
                    body(cond->thenS, cond->then_span);
                    if (block().terminator.kind == TerminatorKind::Unset)
                        block().terminator = {TerminatorKind::Jump, no_id, done, no_id, s->span};
                    block_ = no;
                    body(cond->elseS, cond->else_span);
                    if (block().terminator.kind == TerminatorKind::Unset)
                        block().terminator = {TerminatorKind::Jump, no_id, done, no_id, s->span};
                    block_ = done;
                    return;
                }
                if (auto loop = dynamic_cast<const WhileStmt *>(s)) {
                    auto test = addBlock(), work = addBlock(), done = addBlock();
                    block().terminator = {TerminatorKind::Jump, no_id, test, no_id, s->span};
                    block_ = test;
                    auto value = condition(expression(loop->cond.get()), loop->cond->span);
                    block().terminator = {TerminatorKind::Branch, value, work, done, loop->cond->span};
                    block_ = work;
                    body(loop->body, loop->body_span);
                    if (block().terminator.kind == TerminatorKind::Unset)
                        block().terminator = {TerminatorKind::Jump, no_id, test, no_id, s->span};
                    block_ = done;
                    return;
                }
                error("MPLX2020", "unknown statement", s->span);
            }
            void function(const Function &f, uint32_t index) {
                function_ = index;
                IRFunction lowered;
                lowered.name = f.name;
                lowered.symbol = functions_[index];
                lowered.span = f.span;
                lowered.return_type = result.model.symbols[functions_[index]].type;
                if (lowered.return_type == ValueType::Invalid)
                    lowered.return_type = ValueType::I64;
                lowered.parameters = result.model.symbols[functions_[index]].parameters;
                result.ir.functions.push_back(std::move(lowered));
                block_ = addBlock();
                auto scope = static_cast<uint32_t>(result.model.scopes.size());
                result.model.scopes.push_back({scope, 0, f.body_span, {}});
                scopes_.push_back({});
                scope_ids_.push_back(scope);
                for (const auto &p : f.params) {
                    auto type = parse_type(p.typeName);
                    if (type == ValueType::Invalid)
                        type = ValueType::I64;
                    auto slot = local(type, p.name_span);
                    Symbol symbol;
                    symbol.name = p.name;
                    symbol.kind = SymbolKind::Parameter;
                    symbol.type = type;
                    symbol.function = index;
                    symbol.slot = slot;
                    symbol.declaration = p.name_span;
                    declare(std::move(symbol), "duplicate parameter");
                }
                body(f.body, f.body_span, false);
                if (block().terminator.kind == TerminatorKind::Unset) {
                    auto value = constant(0, fn().return_type, f.span);
                    block().terminator = {TerminatorKind::Return, value, no_id, no_id, f.span};
                }
                // Complete unreachable blocks too; semantic diagnostics are never optimized away.
                for (auto &b : fn().blocks)
                    if (b.terminator.kind == TerminatorKind::Unset) {
                        auto value = static_cast<uint32_t>(fn().values.size());
                        fn().values.push_back(fn().return_type);
                        b.instructions.push_back(
                            {IROp::Constant, value, fn().return_type, {}, 0, 0, "", f.span});
                        b.terminator = {TerminatorKind::Return, value, no_id, no_id, f.span};
                    }
                scopes_.pop_back();
                scope_ids_.pop_back();
            }
        };
    } // namespace
    SemanticResult analyze_module(const Module &module, std::span<const ImportedFunction> imports) {
        return Analyzer().analyze(module, imports);
    }
    std::string semantic_json(const SemanticModel &model) {
        std::string out = "{\"symbols\":[";
        for (size_t i = 0; i < model.symbols.size(); ++i) {
            if (i)
                out += ',';
            const auto &s = model.symbols[i];
            const char *kind = s.kind == SymbolKind::Function    ? "function"
                               : s.kind == SymbolKind::Parameter ? "parameter"
                                                                 : "local";
            out += "{\"id\":" + std::to_string(s.id) + ",\"name\":" + json_quote(s.name) +
                   ",\"kind\":" + json_quote(kind) + ",\"type\":" + json_quote(type_name(s.type)) +
                   ",\"scope\":" + std::to_string(s.scope) +
                   ",\"visible_from\":" + std::to_string(s.visible_from) +
                   ",\"declaration\":" + span_json(s.declaration) +
                   ",\"range\":" + span_json(s.kind == SymbolKind::Function ? s.extent : s.declaration) +
                   ",\"parameters\":[";
            for (size_t p = 0; p < s.parameters.size(); ++p) {
                if (p)
                    out += ',';
                out += "{\"name\":" + json_quote(s.parameter_names[p]) +
                       ",\"type\":" + json_quote(type_name(s.parameters[p])) + "}";
            }
            out += "],\"definition_path\":" + json_quote(s.definition_path) +
                   ",\"original_name\":" + json_quote(s.original_name) +
                   ",\"imported\":" + (s.imported ? "true" : "false") +
                   ",\"exported\":" + (s.exported ? "true" : "false") + "}";
        }
        out += "],\"scopes\":[";
        for (size_t i = 0; i < model.scopes.size(); ++i) {
            if (i)
                out += ',';
            const auto &s = model.scopes[i];
            out += "{\"id\":" + std::to_string(s.id) + ",\"parent\":" + std::to_string(s.parent) +
                   ",\"range\":" + span_json(s.span) + ",\"bindings\":[";
            for (size_t b = 0; b < s.bindings.size(); ++b) {
                if (b)
                    out += ',';
                out += std::to_string(s.bindings[b]);
            }
            out += "]}";
        }
        out += "],\"occurrences\":[";
        for (size_t i = 0; i < model.occurrences.size(); ++i) {
            if (i)
                out += ',';
            const auto &o = model.occurrences[i];
            out += "{\"symbol\":" + std::to_string(o.symbol) + ",\"range\":" + span_json(o.span) +
                   ",\"declaration\":" + (o.declaration ? "true" : "false") +
                   ",\"write\":" + (o.write ? "true" : "false") + "}";
        }
        out += "],\"calls\":[";
        for (size_t i = 0; i < model.calls.size(); ++i) {
            if (i)
                out += ',';
            const auto &call = model.calls[i];
            out += "{\"symbol\":" + std::to_string(call.symbol) + ",\"range\":" + span_json(call.span) +
                   ",\"arguments\":[";
            for (size_t a = 0; a < call.arguments.size(); ++a) {
                if (a)
                    out += ',';
                out += span_json(call.arguments[a]);
            }
            out += "]}";
        }
        return out + "]}";
    }
} // namespace mplx
