#include "reference.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <vector>

namespace mplx::reference {
    Error::Error(ErrorKind kind, std::string message, std::string function, SourceSpan span)
        : std::runtime_error(std::move(message)), kind_(kind), function_(std::move(function)), span_(span) {
    }
    namespace {
        struct Value {
            long long number{0};
            ValueType type{ValueType::I64};
        };
        enum class WorkKind {
            Expression,
            Unary,
            Binary,
            Argument,
            Call,
            ShortCircuit,
            LogicalResult,
            Statement,
            Declare,
            Assign,
            Discard,
            Return,
            DefaultReturn,
            Branch,
            Loop,
            LoopCondition,
            LeaveScope
        };
        struct Work {
            WorkKind kind;
            const Expr *expression{nullptr};
            const Stmt *statement{nullptr};
            ValueType type{ValueType::Invalid};
        };
        struct Unit {
            Module module;
            std::unordered_map<std::string, const Function *> functions;
        };
        struct Target {
            const Function *function;
            std::string path;
        };
        struct Frame {
            Target target;
            std::vector<std::unordered_map<std::string, Value>> scopes;
            std::vector<Work> work;
            std::vector<Value> values;
        };
        std::string key(const std::string &text) {
            auto path = std::filesystem::path(std::u8string(text.begin(), text.end())).lexically_normal();
            if (path.empty() || path.is_absolute())
                throw std::invalid_argument("invalid reference module path");
            for (const auto &component : path)
                if (component == "..")
                    throw std::invalid_argument("reference module escapes root");
            const auto utf8 = path.generic_u8string();
            std::string result(utf8.begin(), utf8.end());
#if defined(_WIN32)
            for (auto &character : result)
                if (character >= 'A' && character <= 'Z')
                    character += 'a' - 'A';
#endif
            return result;
        }
    } // namespace

    struct Program::Impl {
        std::map<std::string, Unit> units;
        std::string root;
        std::vector<Frame> frames;
        Limits limits;
        uint64_t steps{0};
        size_t live_values{0};
        size_t pending_work{0};
        Value result;

        Impl(std::map<std::string, std::string> sources, std::string entry) : root(key(entry)) {
            if (sources.empty() || sources.size() > 256)
                throw Error(ErrorKind::InvalidSource, "invalid reference source count", "", {});
            size_t total = 0;
            for (const auto &[path, source] : sources) {
                total += source.size();
                if (source.size() > 1024 * 1024 || total > 16 * 1024 * 1024)
                    throw Error(ErrorKind::InvalidSource, "reference source budget exceeded", "", {});
                Lexer lexer(source);
                Parser parser(lexer.Lex());
                auto module = parser.parse();
                if (!parser.structuredDiagnostics().empty()) {
                    const auto &diagnostic = parser.structuredDiagnostics().front();
                    throw Error(ErrorKind::InvalidSource, diagnostic.message, "", diagnostic.span);
                }
                auto [position, inserted] = units.emplace(key(path), Unit{std::move(module), {}});
                if (!inserted)
                    throw Error(ErrorKind::InvalidSource, "duplicate reference source", "", {});
                auto &unit = position->second;
                for (const auto &function : unit.module.functions)
                    if (!unit.functions.emplace(function.name, &function).second)
                        throw Error(ErrorKind::InvalidSource, "duplicate reference function", function.name,
                                    function.span);
            }
            if (!units.contains(root))
                throw Error(ErrorKind::InvalidSource, "missing reference entry file", "", {});
        }
        std::string identity(const Target &target) const {
            return target.path == root ? target.function->name : target.path + "::" + target.function->name;
        }
        [[noreturn]] void fail(ErrorKind kind, const std::string &message, SourceSpan span) const {
            throw Error(kind, message, frames.empty() ? "<entry>" : identity(frames.back().target), span);
        }
        void account(size_t count, SourceSpan span) {
            if (count > limits.values || live_values > limits.values - count)
                fail(ErrorKind::ValueLimit, "reference value limit", span);
            live_values += count;
        }
        void push(Value value, SourceSpan span = {}) {
            account(1, span);
            frames.back().values.push_back(value);
        }
        Value pop() {
            auto &values = frames.back().values;
            if (values.empty())
                fail(ErrorKind::InvalidSource, "reference operand underflow", {});
            auto value = values.back();
            values.pop_back();
            --live_values;
            return value;
        }
        Value convert(Value value, ValueType target, SourceSpan span, bool explicit_cast = false) const {
            if (!valid_type(target))
                fail(ErrorKind::InvalidSource, "unknown reference type", span);
            if (!explicit_cast && (value.type == ValueType::Bool) != (target == ValueType::Bool))
                fail(ErrorKind::InvalidSource, "implicit boolean conversion", span);
            if (target == ValueType::Bool)
                value.number = value.number != 0;
            if (target == ValueType::I32 && (value.number < INT32_MIN || value.number > INT32_MAX))
                fail(ErrorKind::Arithmetic, "i32 range exceeded", span);
            value.type = target;
            return value;
        }
        Value &binding(const std::string &name, SourceSpan span) {
            auto &scopes = frames.back().scopes;
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                auto value = scope->find(name);
                if (value != scope->end())
                    return value->second;
            }
            fail(ErrorKind::InvalidSource, "unknown reference binding: " + name, span);
        }
        bool bound(const std::string &name) const {
            for (const auto &scope : frames.back().scopes)
                if (scope.contains(name))
                    return true;
            return false;
        }
        Target lookup(const std::string &path, const std::string &name, SourceSpan span) const {
            auto unit = units.find(path);
            if (unit == units.end())
                fail(ErrorKind::InvalidSource, "missing reference module: " + path, span);
            auto split = name.find("::");
            if (split == std::string::npos) {
                auto function = unit->second.functions.find(name);
                if (function == unit->second.functions.end())
                    fail(ErrorKind::InvalidSource, "unknown reference function: " + name, span);
                return {function->second, path};
            }
            const auto alias = name.substr(0, split);
            const auto member = name.substr(split + 2);
            for (const auto &import : unit->second.module.imports) {
                if (import.alias != alias)
                    continue;
                const auto parent =
                    std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path();
                const auto imported =
                    (parent / std::filesystem::path(std::u8string(import.path.begin(), import.path.end())))
                        .generic_u8string();
                auto target = lookup(key(std::string(imported.begin(), imported.end())), member, span);
                if (!target.function->exported)
                    fail(ErrorKind::InvalidSource, "private reference import", span);
                return target;
            }
            fail(ErrorKind::InvalidSource, "unknown reference namespace: " + alias, span);
        }
        void schedule(Work item) {
            const auto span = item.expression  ? item.expression->span
                              : item.statement ? item.statement->span
                                               : SourceSpan{};
            if (pending_work >= limits.continuations)
                fail(ErrorKind::ContinuationLimit, "reference continuation limit", span);
            frames.back().work.push_back(item);
            ++pending_work;
        }
        void statements(const std::vector<std::unique_ptr<Stmt>> &body) {
            for (auto statement = body.rbegin(); statement != body.rend(); ++statement)
                schedule({WorkKind::Statement, nullptr, statement->get()});
        }
        void call(Target target, std::vector<Value> arguments, SourceSpan span) {
            if (frames.size() >= limits.frames)
                fail(ErrorKind::FrameLimit, "reference frame limit", span);
            if (arguments.size() != target.function->params.size())
                fail(ErrorKind::InvalidSource, "reference arity mismatch", span);
            Frame frame;
            frame.target = std::move(target);
            frame.scopes.emplace_back();
            for (size_t index = 0; index < arguments.size(); ++index) {
                const auto &parameter = frame.target.function->params[index];
                auto value = convert(arguments[index], parse_type(parameter.typeName), span);
                if (!frame.scopes.back().emplace(parameter.name, value).second)
                    fail(ErrorKind::InvalidSource, "duplicate reference parameter", parameter.name_span);
            }
            account(arguments.size(), span);
            frames.push_back(std::move(frame));
            schedule({WorkKind::DefaultReturn});
            statements(frames.back().target.function->body);
        }
        void finish(Value value, SourceSpan span) {
            value = convert(value, parse_type(frames.back().target.function->returnType), span);
            for (const auto &scope : frames.back().scopes)
                live_values -= scope.size();
            live_values -= frames.back().values.size();
            pending_work -= frames.back().work.size();
            frames.pop_back();
            if (frames.empty())
                result = value;
            else
                push(value, span);
        }
        long long arithmetic(const std::string &operation, long long first, long long second,
                             SourceSpan span) const {
            constexpr auto minimum = std::numeric_limits<long long>::min();
            constexpr auto maximum = std::numeric_limits<long long>::max();
            auto overflow = [&] { fail(ErrorKind::Arithmetic, "integer overflow", span); };
            if (operation == "+") {
                if ((second > 0 && first > maximum - second) || (second < 0 && first < minimum - second))
                    overflow();
                return first + second;
            }
            if (operation == "-") {
                if ((second < 0 && first > maximum + second) || (second > 0 && first < minimum + second))
                    overflow();
                return first - second;
            }
            if (operation == "*") {
                auto magnitude = [](long long value) {
                    return value < 0 ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
                };
                const auto left = magnitude(first), right = magnitude(second);
                const bool negative = (first < 0) != (second < 0);
                const auto bound = negative ? uint64_t{1} << 63 : static_cast<uint64_t>(maximum);
                if (right && left > bound / right)
                    overflow();
                const auto product = left * right;
                if (negative && product == (uint64_t{1} << 63))
                    return minimum;
                return negative ? -static_cast<long long>(product) : static_cast<long long>(product);
            }
            if (operation == "/" || operation == "%") {
                if (!second)
                    fail(ErrorKind::Arithmetic, "division by zero", span);
                if (first == minimum && second == -1)
                    overflow();
                return operation == "/" ? first / second : first % second;
            }
            fail(ErrorKind::InvalidSource, "invalid reference arithmetic", span);
        }
        void expression(const Expr *node) {
            if (const auto *literal = dynamic_cast<const LiteralExpr *>(node)) {
                push({literal->value, literal->is_bool ? ValueType::Bool : ValueType::I64}, node->span);
            } else if (const auto *variable = dynamic_cast<const VarExpr *>(node)) {
                push(binding(variable->name, node->span), node->span);
            } else if (const auto *unary = dynamic_cast<const UnaryExpr *>(node)) {
                schedule({WorkKind::Unary, node});
                schedule({WorkKind::Expression, unary->rhs.get()});
            } else if (const auto *binary = dynamic_cast<const BinaryExpr *>(node)) {
                if (binary->op == "&&" || binary->op == "||") {
                    schedule({WorkKind::ShortCircuit, node});
                    schedule({WorkKind::Expression, binary->lhs.get()});
                } else {
                    schedule({WorkKind::Binary, node});
                    schedule({WorkKind::Expression, binary->rhs.get()});
                    schedule({WorkKind::Expression, binary->lhs.get()});
                }
            } else if (const auto *invocation = dynamic_cast<const CallExpr *>(node)) {
                schedule({WorkKind::Call, node});
                const auto cast_type = parse_type(invocation->callee);
                if (cast_type == ValueType::Invalid) {
                    const auto target =
                        lookup(frames.back().target.path, invocation->callee, invocation->span);
                    if (invocation->args.size() != target.function->params.size())
                        fail(ErrorKind::InvalidSource, "reference call arity", invocation->span);
                    for (size_t index = invocation->args.size(); index > 0; --index) {
                        const auto *argument = invocation->args[index - 1].get();
                        schedule({WorkKind::Argument, argument, nullptr,
                                  parse_type(target.function->params[index - 1].typeName)});
                        schedule({WorkKind::Expression, argument});
                    }
                } else {
                    for (auto argument = invocation->args.rbegin(); argument != invocation->args.rend();
                         ++argument)
                        schedule({WorkKind::Expression, argument->get()});
                }
            } else {
                fail(ErrorKind::InvalidSource, "invalid reference expression",
                     node ? node->span : SourceSpan{});
            }
        }
        void statement(const Stmt *node) {
            if (const auto *declaration = dynamic_cast<const LetStmt *>(node)) {
                schedule({WorkKind::Declare, nullptr, node});
                schedule({WorkKind::Expression, declaration->init.get()});
            } else if (const auto *assignment = dynamic_cast<const AssignStmt *>(node)) {
                schedule({WorkKind::Assign, nullptr, node});
                schedule({WorkKind::Expression, assignment->value.get()});
            } else if (const auto *return_value = dynamic_cast<const ReturnStmt *>(node)) {
                schedule({WorkKind::Return, nullptr, node});
                schedule({WorkKind::Expression, return_value->value.get()});
            } else if (const auto *discard = dynamic_cast<const ExprStmt *>(node)) {
                schedule({WorkKind::Discard});
                schedule({WorkKind::Expression, discard->expr.get()});
            } else if (const auto *branch = dynamic_cast<const IfStmt *>(node)) {
                schedule({WorkKind::Branch, nullptr, node});
                schedule({WorkKind::Expression, branch->cond.get()});
            } else if (dynamic_cast<const WhileStmt *>(node)) {
                schedule({WorkKind::Loop, nullptr, node});
            } else
                fail(ErrorKind::InvalidSource, "invalid reference statement", node->span);
        }
        void execute(Work item) {
            switch (item.kind) {
            case WorkKind::Expression:
                expression(item.expression);
                break;
            case WorkKind::Statement:
                statement(item.statement);
                break;
            case WorkKind::Unary: {
                const auto *unary = static_cast<const UnaryExpr *>(item.expression);
                auto value = pop();
                if (unary->op == "!")
                    value = {value.number == 0, ValueType::Bool};
                else {
                    if (!integer_type(value.type))
                        fail(ErrorKind::InvalidSource, "reference negation type", unary->span);
                    value.number = arithmetic("-", 0, value.number, unary->span);
                    value = convert(value, value.type, unary->span);
                }
                push(value, unary->span);
                break;
            }
            case WorkKind::Binary: {
                const auto *binary = static_cast<const BinaryExpr *>(item.expression);
                auto right = pop(), left = pop();
                const auto &op = binary->op;
                const bool compare =
                    op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
                Value value;
                if (compare) {
                    value.type = ValueType::Bool;
                    if (op == "==")
                        value.number = left.number == right.number;
                    else if (op == "!=")
                        value.number = left.number != right.number;
                    else if (op == "<")
                        value.number = left.number < right.number;
                    else if (op == "<=")
                        value.number = left.number <= right.number;
                    else if (op == ">")
                        value.number = left.number > right.number;
                    else
                        value.number = left.number >= right.number;
                } else {
                    if (!integer_type(left.type) || !integer_type(right.type))
                        fail(ErrorKind::InvalidSource, "reference arithmetic type", binary->span);
                    value.type = left.type == ValueType::I64 || right.type == ValueType::I64 ? ValueType::I64
                                                                                             : ValueType::I32;
                    value.number = arithmetic(op, left.number, right.number, binary->span);
                    value = convert(value, value.type, binary->span);
                }
                push(value, binary->span);
                break;
            }
            case WorkKind::ShortCircuit: {
                const auto *binary = static_cast<const BinaryExpr *>(item.expression);
                const auto left = pop();
                if ((binary->op == "&&" && !left.number) || (binary->op == "||" && left.number)) {
                    push({left.number != 0, ValueType::Bool}, binary->span);
                } else {
                    schedule({WorkKind::LogicalResult, item.expression});
                    schedule({WorkKind::Expression, binary->rhs.get()});
                }
                break;
            }
            case WorkKind::LogicalResult: {
                const auto value = pop();
                push({value.number != 0, ValueType::Bool}, item.expression->span);
                break;
            }
            case WorkKind::Argument: {
                // Convert before evaluating the next argument, preserving trap order.
                const auto value = pop();
                push(convert(value, item.type, item.expression->span), item.expression->span);
                break;
            }
            case WorkKind::Call: {
                const auto *invocation = static_cast<const CallExpr *>(item.expression);
                std::vector<Value> arguments(invocation->args.size());
                for (size_t index = arguments.size(); index > 0; --index)
                    arguments[index - 1] = pop();
                const auto type = parse_type(invocation->callee);
                if (type != ValueType::Invalid) {
                    if (arguments.size() != 1)
                        fail(ErrorKind::InvalidSource, "reference cast arity", invocation->span);
                    push(convert(arguments[0], type, invocation->span, true), invocation->span);
                } else {
                    if (bound(invocation->callee))
                        fail(ErrorKind::InvalidSource, "reference call shadowed by local",
                             invocation->name_span);
                    auto target = lookup(frames.back().target.path, invocation->callee, invocation->span);
                    if (arguments.size() != target.function->params.size())
                        fail(ErrorKind::InvalidSource, "reference call arity", invocation->span);
                    call(std::move(target), std::move(arguments), invocation->span);
                }
                break;
            }
            case WorkKind::Declare: {
                const auto *declaration = static_cast<const LetStmt *>(item.statement);
                auto value = pop();
                if (!declaration->typeName.empty())
                    value = convert(value, parse_type(declaration->typeName), declaration->init->span);
                auto &scope = frames.back().scopes.back();
                if (scope.contains(declaration->name))
                    fail(ErrorKind::InvalidSource, "duplicate reference local", declaration->name_span);
                account(1, declaration->span);
                scope.emplace(declaration->name, value);
                break;
            }
            case WorkKind::Assign: {
                const auto *assignment = static_cast<const AssignStmt *>(item.statement);
                const auto value = pop();
                auto &target = binding(assignment->name, assignment->name_span);
                target = convert(value, target.type, assignment->value->span);
                break;
            }
            case WorkKind::Discard:
                (void)pop();
                break;
            case WorkKind::Return: {
                const auto value = pop();
                finish(value, static_cast<const ReturnStmt *>(item.statement)->value->span);
                break;
            }
            case WorkKind::DefaultReturn: {
                const auto type = parse_type(frames.back().target.function->returnType);
                const auto span = frames.back().target.function->span;
                finish({0, type}, span);
                break;
            }
            case WorkKind::Branch: {
                const auto condition = pop();
                const auto *branch = static_cast<const IfStmt *>(item.statement);
                frames.back().scopes.emplace_back();
                schedule({WorkKind::LeaveScope});
                statements(condition.number ? branch->thenS : branch->elseS);
                break;
            }
            case WorkKind::Loop: {
                const auto *loop = static_cast<const WhileStmt *>(item.statement);
                schedule({WorkKind::LoopCondition, nullptr, item.statement});
                schedule({WorkKind::Expression, loop->cond.get()});
                break;
            }
            case WorkKind::LoopCondition: {
                const auto condition = pop();
                if (!condition.number)
                    break;
                const auto *loop = static_cast<const WhileStmt *>(item.statement);
                schedule({WorkKind::Loop, nullptr, item.statement});
                frames.back().scopes.emplace_back();
                schedule({WorkKind::LeaveScope});
                statements(loop->body);
                break;
            }
            case WorkKind::LeaveScope:
                live_values -= frames.back().scopes.back().size();
                frames.back().scopes.pop_back();
                break;
            }
        }
        long long invoke(const std::string &entry, std::span<const long long> arguments, Limits requested) {
            frames.clear();
            pending_work = live_values = steps = 0;
            limits = requested;
            if (!units.at(root).functions.contains(entry))
                fail(ErrorKind::InvalidEntry, "unknown reference entry: " + entry, {});
            auto target = lookup(root, entry, {});
            if (arguments.size() != target.function->params.size())
                fail(ErrorKind::InvalidEntry, "reference host arity", target.function->span);
            std::vector<Value> values;
            for (size_t index = 0; index < arguments.size(); ++index) {
                const auto type = parse_type(target.function->params[index].typeName);
                if (!valid_type(type) ||
                    (type == ValueType::Bool && arguments[index] != 0 && arguments[index] != 1) ||
                    (type == ValueType::I32 &&
                     (arguments[index] < INT32_MIN || arguments[index] > INT32_MAX)))
                    fail(ErrorKind::InvalidEntry, "reference host argument type", target.function->span);
                values.push_back({arguments[index], type});
            }
            try {
                call(std::move(target), std::move(values), {});
                while (!frames.empty()) {
                    auto &work = frames.back().work;
                    if (work.empty())
                        fail(ErrorKind::InvalidSource, "reference frame has no continuation", {});
                    auto item = work.back();
                    work.pop_back();
                    --pending_work;
                    if (steps >= limits.steps)
                        fail(ErrorKind::StepLimit, "reference step limit",
                             item.expression  ? item.expression->span
                             : item.statement ? item.statement->span
                                              : SourceSpan{});
                    ++steps;
                    execute(item);
                }
                return result.number;
            } catch (...) {
                frames.clear();
                live_values = 0;
                pending_work = 0;
                throw;
            }
        }
    };
    Program::Program(std::string source) : Program({{"main.mplx", std::move(source)}}, "main.mplx") {
    }
    Program::Program(std::map<std::string, std::string> sources, std::string entry_file)
        : impl_(std::make_unique<Impl>(std::move(sources), std::move(entry_file))) {
    }
    Program::~Program() = default;
    long long Program::invoke(const std::string &entry, std::span<const long long> arguments, Limits limits) {
        return impl_->invoke(entry, arguments, limits);
    }
} // namespace mplx::reference
