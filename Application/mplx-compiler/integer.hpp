#pragma once
#include <limits>
#include <stdexcept>

namespace mplx::integer {
    using Value = long long;
    static_assert(sizeof(Value) == 8);
    constexpr Value min = std::numeric_limits<Value>::min(), max = std::numeric_limits<Value>::max();
    inline Value add(Value a, Value b) {
        if ((b > 0 && a > max - b) || (b < 0 && a < min - b))
            throw std::runtime_error("integer overflow");
        return a + b;
    }
    inline Value sub(Value a, Value b) {
        if ((b > 0 && a < min + b) || (b < 0 && a > max + b))
            throw std::runtime_error("integer overflow");
        return a - b;
    }
    inline Value mul(Value a, Value b) {
        if (a > 0 ? (b > 0 ? a > max / b : b < min / a)
                  : (a < 0 && (b > 0 ? a < min / b : b < 0 && a < max / b)))
            throw std::runtime_error("integer overflow");
        return a * b;
    }
    inline Value neg(Value a) {
        if (a == min)
            throw std::runtime_error("integer overflow");
        return -a;
    }
    inline void check_div(Value a, Value b) {
        if (b == 0)
            throw std::runtime_error("division by zero");
        if (a == min && b == -1)
            throw std::runtime_error("integer overflow");
    }
    inline Value div(Value a, Value b) {
        check_div(a, b);
        return a / b;
    }
    inline Value mod(Value a, Value b) {
        check_div(a, b);
        return a % b;
    }
} // namespace mplx::integer
