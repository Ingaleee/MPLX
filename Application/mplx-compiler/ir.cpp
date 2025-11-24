#include "ir.hpp"
#include "ir_analysis.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace mplx {
    namespace {
        const char *op_name(IROp op) {
            switch (op) {
            case IROp::Constant:
                return "constant";
            case IROp::Load:
                return "load";
            case IROp::Store:
                return "store";
            case IROp::Unary:
                return "unary";
            case IROp::Binary:
                return "binary";
            case IROp::Cast:
                return "cast";
            case IROp::Call:
                return "call";
            case IROp::Parameter:
                return "parameter";
            case IROp::Phi:
                return "phi";
            case IROp::Copy:
                return "copy";
            }
            return "invalid";
        }
    } // namespace
    std::vector<Diagnostic> verify_ir(const IRModule &module) {
        std::vector<Diagnostic> errors;
        if (module.functions.empty())
            return {{"MPLX3000", "IR module contains no functions", 0, 0, {}}};
        std::unordered_set<std::string> function_names;
        for (const auto &f : module.functions) {
            auto fail = [&](const std::string &text, SourceSpan span) {
                errors.push_back({"MPLX3001", "IR verification in " + f.name + ": " + text, span.start.line,
                                  span.start.column, span});
            };
            if (f.name.empty() || f.name.find('\0') != std::string::npos ||
                !function_names.insert(f.name).second)
                fail("empty or duplicate function name", f.span);
            if (f.blocks.empty() || f.parameters.size() > 255 || f.locals.size() > 65535 ||
                f.parameters.size() > f.locals.size() || !valid_type(f.return_type)) {
                fail("invalid function metadata", f.span);
                continue;
            }
            if (f.blocks.size() *
                    static_cast<uint64_t>(f.locals.size() + f.values.size() + f.blocks.size() + 1) >
                2'000'000) {
                fail("dataflow analysis exceeds complexity budget", f.span);
                continue;
            }
            for (size_t p = 0; p < f.parameters.size(); ++p)
                if (f.parameters[p] != f.locals[p])
                    fail("parameter/local type mismatch", f.span);
            for (auto type : f.locals)
                if (!valid_type(type))
                    fail("invalid local type", f.span);
            for (auto type : f.values)
                if (!valid_type(type))
                    fail("invalid value type", f.span);
            std::vector<bool> defined(f.values.size(), false);
            struct Use {
                uint32_t value, block, position;
                SourceSpan span;
            };
            std::vector<Use> uses;
            std::vector<uint32_t> definition_block(f.values.size(), no_id),
                definition_position(f.values.size(), no_id);
            std::vector<std::vector<uint32_t>> edges(f.blocks.size()), predecessors(f.blocks.size());
            auto initial_errors = errors.size();
            for (size_t b = 0; b < f.blocks.size(); ++b) {
                const auto &block = f.blocks[b];
                if (block.id != b)
                    fail("noncanonical block ID", f.span);
                uint32_t position = 0;
                bool ordinary_seen = false;
                for (const auto &ins : block.instructions) {
                    if (ins.op == IROp::Phi && ordinary_seen)
                        fail("phi must precede ordinary instructions", ins.span);
                    if (ins.op != IROp::Phi)
                        ordinary_seen = true;
                    if (ins.op != IROp::Phi && !ins.incoming_blocks.empty())
                        fail("non-phi instruction has incoming edges", ins.span);
                    auto bad_args = [&](size_t count) {
                        if (ins.arguments.size() != count) {
                            fail("wrong IR operand count", ins.span);
                            return true;
                        }
                        return false;
                    };
                    bool bad = false;
                    for (auto a : ins.arguments) {
                        if (a >= f.values.size()) {
                            fail("value index out of bounds", ins.span);
                            bad = true;
                        } else if (ins.op != IROp::Phi)
                            uses.push_back({a, static_cast<uint32_t>(b), position, ins.span});
                    }
                    if (ins.op != IROp::Store) {
                        if (ins.result >= f.values.size() || defined[ins.result] ||
                            f.values[ins.result] != ins.type || !valid_type(ins.type)) {
                            fail("invalid or duplicate result/type", ins.span);
                            bad = true;
                        } else {
                            defined[ins.result] = true;
                            definition_block[ins.result] = static_cast<uint32_t>(b);
                            definition_position[ins.result] = position;
                        }
                    } else if (ins.result != no_id) {
                        fail("store cannot define a result", ins.span);
                        bad = true;
                    }
                    ++position;
                    if (bad)
                        continue;
                    switch (ins.op) {
                    default:
                        fail("unknown IR opcode", ins.span);
                        break;
                    case IROp::Constant:
                        if (bad_args(0))
                            break;
                        if ((ins.type == ValueType::Bool && ins.constant != 0 && ins.constant != 1) ||
                            (ins.type == ValueType::I32 &&
                             (ins.constant < INT32_MIN || ins.constant > INT32_MAX)))
                            fail("constant outside type range", ins.span);
                        break;
                    case IROp::Load:
                    case IROp::Store: {
                        if (f.ssa)
                            fail("SSA function contains mutable local operations", ins.span);
                        if (bad_args(ins.op == IROp::Store ? 1 : 0))
                            break;
                        if (ins.index >= f.locals.size() || f.locals[ins.index] != ins.type ||
                            (ins.op == IROp::Store && f.values[ins.arguments[0]] != ins.type))
                            fail("local operand type mismatch", ins.span);
                        break;
                    }
                    case IROp::Parameter:
                        if (bad_args(0))
                            break;
                        if (!f.ssa || b != 0 || ins.index >= f.parameters.size() ||
                            ins.type != f.parameters[ins.index])
                            fail("invalid SSA parameter", ins.span);
                        break;
                    case IROp::Copy:
                        if (!bad_args(1) && f.values[ins.arguments[0]] != ins.type)
                            fail("copy type mismatch", ins.span);
                        break;
                    case IROp::Phi:
                        if (!f.ssa || ins.arguments.empty() ||
                            ins.arguments.size() != ins.incoming_blocks.size())
                            fail("invalid phi shape", ins.span);
                        for (auto argument : ins.arguments)
                            if (f.values[argument] != ins.type)
                                fail("phi input type mismatch", ins.span);
                        break;
                    case IROp::Cast:
                        if (bad_args(1))
                            break;
                        break;
                    case IROp::Unary: {
                        if (bad_args(1))
                            break;
                        auto type = f.values[ins.arguments[0]];
                        if (ins.operation == "!") {
                            if (type != ValueType::Bool || ins.type != ValueType::Bool)
                                fail("not requires bool", ins.span);
                        } else if (ins.operation != "-" || !integer_type(type) || type != ins.type)
                            fail("negation type mismatch", ins.span);
                        break;
                    }
                    case IROp::Binary: {
                        if (bad_args(2))
                            break;
                        auto a = f.values[ins.arguments[0]], c = f.values[ins.arguments[1]];
                        const auto &op = ins.operation;
                        auto equality = op == "==" || op == "!=";
                        auto comparison = equality || op == "<" || op == "<=" || op == ">" || op == ">=";
                        auto arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
                        if (a != c || (!integer_type(a) && !(equality && a == ValueType::Bool)) ||
                            (!comparison && !arithmetic) ||
                            (comparison ? ins.type != ValueType::Bool : ins.type != a))
                            fail("binary operand type mismatch", ins.span);
                        break;
                    }
                    case IROp::Call:
                        if (ins.index >= module.functions.size()) {
                            fail("invalid callee", ins.span);
                            break;
                        }
                        if (ins.arguments.size() != module.functions[ins.index].parameters.size() ||
                            ins.type != module.functions[ins.index].return_type) {
                            fail("call signature mismatch", ins.span);
                            break;
                        }
                        for (size_t p = 0; p < ins.arguments.size(); ++p)
                            if (f.values[ins.arguments[p]] != module.functions[ins.index].parameters[p])
                                fail("call argument type mismatch", ins.span);
                        break;
                    }
                }
                const auto &term = block.terminator;
                if (term.kind == TerminatorKind::Jump && (term.value != no_id || term.second != no_id))
                    fail("jump carries unused value or second target", term.span);
                if (term.kind == TerminatorKind::Return && (term.first != no_id || term.second != no_id))
                    fail("return carries unused control-flow targets", term.span);
                auto target = [&](uint32_t to) {
                    if (to >= f.blocks.size())
                        fail("invalid branch target", term.span);
                    else {
                        if (std::find(edges[b].begin(), edges[b].end(), to) == edges[b].end()) {
                            edges[b].push_back(to);
                            predecessors[to].push_back(static_cast<uint32_t>(b));
                        }
                    }
                };
                if (term.kind == TerminatorKind::Jump)
                    target(term.first);
                else if (term.kind == TerminatorKind::Branch || term.kind == TerminatorKind::Return) {
                    if (term.value >= f.values.size())
                        fail("terminator uses undefined value", term.span);
                    else if (f.values[term.value] !=
                             (term.kind == TerminatorKind::Branch ? ValueType::Bool : f.return_type))
                        fail("terminator type mismatch", term.span);
                    else
                        uses.push_back({term.value, static_cast<uint32_t>(b), position, term.span});
                    if (term.kind == TerminatorKind::Branch) {
                        target(term.first);
                        target(term.second);
                    }
                } else
                    fail("unterminated block", term.span);
            }
            if (errors.size() != initial_errors)
                continue;
            if (f.ssa && !predecessors[0].empty()) {
                fail("SSA entry block must have no predecessors", f.span);
                continue;
            }
            for (const auto &block : f.blocks) {
                for (const auto &instruction : block.instructions) {
                    if (instruction.op != IROp::Phi)
                        continue;
                    auto incoming = instruction.incoming_blocks;
                    auto expected = predecessors[block.id];
                    std::sort(incoming.begin(), incoming.end());
                    std::sort(expected.begin(), expected.end());
                    if (incoming != expected)
                        fail("phi inputs do not match CFG predecessors", instruction.span);
                    for (size_t edge = 0; edge < instruction.incoming_blocks.size(); ++edge) {
                        auto predecessor = instruction.incoming_blocks[edge];
                        if (predecessor >= f.blocks.size()) {
                            fail("invalid phi predecessor", instruction.span);
                            continue;
                        }
                        uses.push_back({instruction.arguments[edge], predecessor,
                                        static_cast<uint32_t>(f.blocks[predecessor].instructions.size()),
                                        instruction.span});
                    }
                }
            }
            if (errors.size() != initial_errors)
                continue;
            ControlFlow cfg;
            try {
                cfg = analyze_control_flow(f);
            } catch (const std::runtime_error &error) {
                fail(error.what(), f.span);
                continue;
            }
            const auto &reachable = cfg.reachable;
            for (const auto &use : uses) {
                auto definition = definition_block[use.value];
                if (definition == no_id ||
                    (definition == use.block && definition_position[use.value] >= use.position) ||
                    (reachable[use.block] &&
                     (!reachable[definition] || !cfg.dominates(definition, use.block))))
                    fail("use before dominating definition", use.span);
            }
            if (f.ssa)
                continue; // SSA contains no mutable local operations.
            // Definite assignment is the intersection of incoming CFG facts.
            using Facts = std::vector<uint8_t>;
            std::vector<Facts> in(f.blocks.size(), Facts(f.locals.size(), 1)), out = in;
            in[0].assign(f.locals.size(), 0);
            std::fill_n(in[0].begin(), f.parameters.size(), uint8_t{1});
            bool changed = true;
            while (changed) {
                changed = false;
                for (size_t b = 0; b < f.blocks.size(); ++b) {
                    if (!reachable[b])
                        continue;
                    Facts next = in[b];
                    if (b != 0) {
                        next.assign(f.locals.size(), 1);
                        for (auto from : predecessors[b])
                            if (reachable[from])
                                for (size_t l = 0; l < next.size(); ++l)
                                    next[l] &= out[from][l];
                    }
                    auto produced = next;
                    for (const auto &ins : f.blocks[b].instructions)
                        if (ins.op == IROp::Store)
                            produced[ins.index] = 1;
                    if (next != in[b] || produced != out[b]) {
                        in[b] = std::move(next);
                        out[b] = std::move(produced);
                        changed = true;
                    }
                }
            }
            for (size_t b = 0; b < f.blocks.size(); ++b)
                if (reachable[b]) {
                    auto facts = in[b];
                    for (const auto &ins : f.blocks[b].instructions) {
                        if (ins.op == IROp::Load && !facts[ins.index])
                            fail("local read before definite assignment", ins.span);
                        if (ins.op == IROp::Store)
                            facts[ins.index] = 1;
                    }
                }
        }
        return errors;
    }
    std::string dump_ir_json(const IRModule &module) {
        std::string out = "{\"schema_version\":1,\"functions\":[";
        for (size_t i = 0; i < module.functions.size(); ++i) {
            if (i)
                out += ',';
            const auto &f = module.functions[i];
            out += "{\"name\":" + json_quote(f.name) + ",\"ssa\":" + (f.ssa ? "true" : "false") +
                   ",\"return_type\":" + json_quote(type_name(f.return_type)) + ",\"locals\":[";
            for (size_t l = 0; l < f.locals.size(); ++l) {
                if (l)
                    out += ',';
                out += json_quote(type_name(f.locals[l]));
            }
            out += "],\"blocks\":[";
            for (size_t b = 0; b < f.blocks.size(); ++b) {
                if (b)
                    out += ',';
                const auto &block = f.blocks[b];
                out += "{\"id\":" + std::to_string(block.id) + ",\"instructions\":[";
                for (size_t n = 0; n < block.instructions.size(); ++n) {
                    if (n)
                        out += ',';
                    const auto &ins = block.instructions[n];
                    out += "{\"op\":" + json_quote(op_name(ins.op)) +
                           ",\"result\":" + std::to_string(ins.result) +
                           ",\"type\":" + json_quote(type_name(ins.type)) +
                           ",\"index\":" + std::to_string(ins.index) +
                           ",\"constant\":" + std::to_string(ins.constant) +
                           ",\"operation\":" + json_quote(ins.operation) +
                           ",\"range\":" + span_json(ins.span) + ",\"arguments\":[";
                    for (size_t a = 0; a < ins.arguments.size(); ++a) {
                        if (a)
                            out += ',';
                        out += std::to_string(ins.arguments[a]);
                    }
                    out += "],\"incoming_blocks\":[";
                    for (size_t edge = 0; edge < ins.incoming_blocks.size(); ++edge) {
                        if (edge)
                            out += ',';
                        out += std::to_string(ins.incoming_blocks[edge]);
                    }
                    out += "]}";
                }
                auto &t = block.terminator;
                auto name = t.kind == TerminatorKind::Jump     ? "jump"
                            : t.kind == TerminatorKind::Branch ? "branch"
                                                               : "return";
                out += "],\"terminator\":{\"kind\":" + json_quote(name) +
                       ",\"value\":" + std::to_string(t.value) + ",\"first\":" + std::to_string(t.first) +
                       ",\"second\":" + std::to_string(t.second) + "}}";
            }
            out += "]}";
        }
        return out + "]}";
    }
} // namespace mplx
