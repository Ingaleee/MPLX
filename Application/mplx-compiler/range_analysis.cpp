#include "range_analysis.hpp"
#include "integer.hpp"
#include "ir_analysis.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mplx {
    namespace {
        IntegerRange type_range(ValueType type) {
            return type == ValueType::I32    ? IntegerRange{false, INT32_MIN, INT32_MAX}
                   : type == ValueType::Bool ? IntegerRange{false, 0, 1}
                                             : IntegerRange{false, INT64_MIN, INT64_MAX};
        }
        IntegerRange join_range(IntegerRange first, IntegerRange second) {
            if (first.bottom)
                return second;
            if (second.bottom)
                return first;
            return {false, std::min(first.minimum, second.minimum), std::max(first.maximum, second.maximum)};
        }
        struct Evaluation {
            IntegerRange range;
            bool total{false};
        };
        Evaluation evaluate(const IRInstruction &instruction, const std::vector<IntegerRange> &values) {
            const auto bounds = type_range(instruction.type);
            if (instruction.op == IROp::Constant)
                return {{false, instruction.constant, instruction.constant}, true};
            if (instruction.op == IROp::Parameter || instruction.op == IROp::Load)
                return {bounds, true};
            if (instruction.op == IROp::Call)
                return {bounds, false};
            if (instruction.op == IROp::Phi) {
                IntegerRange range;
                for (auto argument : instruction.arguments)
                    range = join_range(range, values.at(argument));
                return {range, !range.bottom};
            }
            if (instruction.op == IROp::Store)
                return {bounds, false};
            for (auto argument : instruction.arguments)
                if (values.at(argument).bottom)
                    return {};
            if (instruction.arguments.empty())
                return {bounds, false};
            const auto first = values[instruction.arguments[0]];
            if (instruction.op == IROp::Copy)
                return {first, true};
            if (instruction.op == IROp::Cast) {
                if (instruction.type == ValueType::Bool) {
                    if (first.minimum == 0 && first.maximum == 0)
                        return {{false, 0, 0}, true};
                    return {first.contains(0) ? bounds : IntegerRange{false, 1, 1}, true};
                }
                if (first.minimum >= bounds.minimum && first.maximum <= bounds.maximum)
                    return {first, true};
                return {bounds, false};
            }
            if (instruction.op == IROp::Unary && instruction.operation == "!")
                return {{false, first.maximum == 0 ? 1 : 0, first.minimum == 1 ? 0 : 1}, true};
            if (instruction.op == IROp::Binary && ir_operation_is_total(instruction))
                return {bounds, true};
            try {
                IntegerRange computed;
                auto include = [&](long long value) {
                    if (value < bounds.minimum || value > bounds.maximum)
                        throw std::runtime_error("range may exceed result type");
                    computed = join_range(computed, {false, value, value});
                };
                if (instruction.op == IROp::Unary && instruction.operation == "-") {
                    include(integer::neg(first.minimum));
                    include(integer::neg(first.maximum));
                    return {computed, true};
                }
                if (instruction.op != IROp::Binary || instruction.arguments.size() != 2)
                    return {bounds, false};
                const auto second = values[instruction.arguments[1]];
                const auto &operation = instruction.operation;
                if (operation == "%") {
                    if (second.contains(0) || (first.contains(INT64_MIN) && second.contains(-1)))
                        return {bounds, false};
                    // The remainder magnitude cannot exceed that of the dividend;
                    // source operands already have their declared result width.
                    return {bounds, true};
                }
                if (operation == "/" && second.contains(0))
                    return {bounds, false};
                for (auto left : {first.minimum, first.maximum})
                    for (auto right : {second.minimum, second.maximum}) {
                        if (operation == "+")
                            include(integer::add(left, right));
                        else if (operation == "-")
                            include(integer::sub(left, right));
                        else if (operation == "*")
                            include(integer::mul(left, right));
                        else if (operation == "/")
                            include(integer::div(left, right));
                        else
                            return {bounds, false};
                    }
                return {computed, true};
            } catch (const std::runtime_error &) {
                return {bounds, false};
            }
        }
    } // namespace
    bool IntegerRange::contains(long long value) const {
        return !bottom && minimum <= value && value <= maximum;
    }
    RangeAnalysis analyze_integer_ranges(const IRFunction &function) {
        RangeAnalysis result;
        result.values.resize(function.values.size());
        result.proven_total.resize(function.values.size(), false);
        std::vector<uint32_t> updates(function.values.size(), 0);
        uint64_t work = 0;
        bool changed = true;
        while (changed) {
            changed = false;
            ++result.iterations;
            for (const auto &block : function.blocks)
                for (const auto &instruction : block.instructions) {
                    if (++work > 8'000'000)
                        throw std::runtime_error("integer range analysis exceeds work budget");
                    if (instruction.result == no_id)
                        continue;
                    const auto value = instruction.result;
                    const auto candidate = evaluate(instruction, result.values).range;
                    auto next = join_range(result.values.at(value), candidate);
                    if (next != result.values[value]) {
                        // Ascending phi ranges in cycles cannot grow one integer
                        // per iteration indefinitely. Widen to the declared type.
                        if (++updates[value] > 3) {
                            next = type_range(instruction.type);
                            ++result.widenings;
                        }
                        result.values[value] = next;
                        changed = true;
                    }
                }
        }
        for (const auto &block : function.blocks)
            for (const auto &instruction : block.instructions)
                if (instruction.result != no_id)
                    result.proven_total[instruction.result] = evaluate(instruction, result.values).total;
        return result;
    }
} // namespace mplx
