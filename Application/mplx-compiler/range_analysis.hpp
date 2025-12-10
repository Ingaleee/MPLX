#pragma once

#include "ir.hpp"

namespace mplx {
    struct IntegerRange {
        bool bottom{true};
        long long minimum{0}, maximum{0};
        bool operator==(const IntegerRange &) const = default;
        bool contains(long long value) const;
    };
    struct RangeAnalysis {
        std::vector<IntegerRange> values;
        // Indexed by result value. Only operations with a proved nonempty input
        // range may become total; calls themselves require function summaries.
        std::vector<bool> proven_total;
        uint64_t iterations{0}, widenings{0};
    };
    RangeAnalysis analyze_integer_ranges(const IRFunction &function);
} // namespace mplx
