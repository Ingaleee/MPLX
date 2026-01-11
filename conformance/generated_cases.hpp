// Generated from semantics.json by scripts/conformance.mjs. Do not edit.
#pragma once

#include <string>
#include <vector>

namespace mplx::conformance {
    constexpr unsigned semantics_version = 1;
    struct Case {
        std::string id, rule, source, entry;
        std::vector<long long> arguments;
        std::string kind;
        long long value;
        std::string function, diagnostic;
        unsigned fault_start, fault_end;
    };
    inline const std::vector<Case> cases{
        {
            "literal-width",
            "T01",
            "fn main() -> i64 {\n"
            "    return 2147483648;\n"
            "}\n",
            "main",
            {},
            "value",
            2147483648LL,
            "",
            "",
            0,
            0,
        },
        {
            "inferred-let",
            "T01",
            "fn main() -> i64 {\n"
            "    let x = 2147483648;\n"
            "    return x;\n"
            "}\n",
            "main",
            {},
            "value",
            2147483648LL,
            "",
            "",
            0,
            0,
        },
        {
            "default-parameter",
            "T01",
            "fn identity(x) -> i64 {\n"
            "    return x;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return identity(2147483648);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            79,
            89,
        },
        {
            "default-return",
            "T01",
            "fn narrow() {\n"
            "    return 2147483649;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return narrow();\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "narrow",
            "",
            25,
            35,
        },
        {
            "mixed-promotion",
            "T02",
            "fn main() -> i64 {\n"
            "    let x: i32 = 2147483647;\n"
            "    return x + 1;\n"
            "}\n",
            "main",
            {},
            "value",
            2147483648LL,
            "",
            "",
            0,
            0,
        },
        {
            "i32-add-overflow",
            "T02",
            "fn main() -> i64 {\n"
            "    let x: i32 = 2147483647;\n"
            "    return x + i32(1);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            59,
            69,
        },
        {
            "i64-add-overflow",
            "T02",
            "fn main() -> i64 {\n"
            "    return 9223372036854775807 + 1;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            53,
        },
        {
            "i64-sub-overflow",
            "T02",
            "fn main() -> i64 {\n"
            "    let minimum = -9223372036854775807 - 1;\n"
            "    return minimum - 1;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            74,
            85,
        },
        {
            "i64-mul-overflow",
            "T02",
            "fn main() -> i64 {\n"
            "    return 3037000500 * 3037000500;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            53,
        },
        {
            "i64-neg-overflow",
            "T02",
            "fn main() -> i64 {\n"
            "    let minimum = -9223372036854775807 - 1;\n"
            "    return -minimum;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            74,
            82,
        },
        {
            "division-sign",
            "T03",
            "fn main() -> i64 {\n"
            "    return -17 / 5;\n"
            "}\n",
            "main",
            {},
            "value",
            -3LL,
            "",
            "",
            0,
            0,
        },
        {
            "remainder-sign",
            "T03",
            "fn main() -> i64 {\n"
            "    return -17 % 5;\n"
            "}\n",
            "main",
            {},
            "value",
            -2LL,
            "",
            "",
            0,
            0,
        },
        {
            "division-zero",
            "T03",
            "fn main() -> i64 {\n"
            "    return 1 / 0;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            35,
        },
        {
            "remainder-zero",
            "T03",
            "fn main() -> i64 {\n"
            "    return 1 % 0;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            35,
        },
        {
            "i64-min-div",
            "T04",
            "fn main() -> i64 {\n"
            "    let minimum = -9223372036854775807 - 1;\n"
            "    return minimum / -1;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            74,
            86,
        },
        {
            "i64-min-mod",
            "T04",
            "fn main() -> i64 {\n"
            "    let minimum = -9223372036854775807 - 1;\n"
            "    return minimum % -1;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            74,
            86,
        },
        {
            "i32-min-div",
            "T04",
            "fn main() -> i64 {\n"
            "    let minimum: i32 = -2147483648;\n"
            "    return minimum / i32(-1);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            66,
            83,
        },
        {
            "i32-min-mod",
            "T04",
            "fn main() -> i64 {\n"
            "    let minimum: i32 = -2147483648;\n"
            "    return minimum % i32(-1);\n"
            "}\n",
            "main",
            {},
            "value",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "narrow-assignment",
            "T05",
            "fn main() -> i64 {\n"
            "    let x: i32 = 0;\n"
            "    x = 2147483648;\n"
            "    return x;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            47,
            57,
        },
        {
            "narrow-return",
            "T05",
            "fn main() -> i32 {\n"
            "    return 2147483648;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            40,
        },
        {
            "explicit-narrow",
            "T05",
            "fn main() -> i64 {\n"
            "    return i32(2147483648);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            30,
            45,
        },
        {
            "bool-casts",
            "T06",
            "fn main() -> i64 {\n"
            "    return i64(bool(-42)) + i64(false);\n"
            "}\n",
            "main",
            {},
            "value",
            1LL,
            "",
            "",
            0,
            0,
        },
        {
            "bool-integer-distinct",
            "T06",
            "fn main() -> bool {\n"
            "    return true == 1;\n"
            "}\n",
            "main",
            {},
            "diagnostic",
            0LL,
            "",
            "MPLX2014",
            0,
            0,
        },
        {
            "bool-arithmetic",
            "T06",
            "fn main() -> i64 {\n"
            "    return true + false;\n"
            "}\n",
            "main",
            {},
            "diagnostic",
            0LL,
            "",
            "MPLX2014",
            0,
            0,
        },
        {
            "left-operand-first",
            "E01",
            "fn main() -> i64 {\n"
            "    return (1 / 0) + (9223372036854775807 + 1);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            31,
            36,
        },
        {
            "convert-argument-first",
            "E01",
            "fn choose(a: i32, b: i64) -> i64 {\n"
            "    return b;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return choose(2147483648, 1 / 0);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            88,
            98,
        },
        {
            "evaluate-argument-first",
            "E01",
            "fn choose(a: i64, b: i32) -> i64 {\n"
            "    return a;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return choose(1 / 0, 2147483648);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            88,
            93,
        },
        {
            "short-circuit-and",
            "E02",
            "fn main() -> bool {\n"
            "    return false && bool(1 / 0);\n"
            "}\n",
            "main",
            {},
            "value",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "short-circuit-or",
            "E02",
            "fn main() -> bool {\n"
            "    return true || bool(1 / 0);\n"
            "}\n",
            "main",
            {},
            "value",
            1LL,
            "",
            "",
            0,
            0,
        },
        {
            "numeric-truth",
            "E02",
            "fn main() -> i64 {\n"
            "    if (-7) {\n"
            "        return 42;\n"
            "    }\n"
            "    return 0;\n"
            "}\n",
            "main",
            {},
            "value",
            42LL,
            "",
            "",
            0,
            0,
        },
        {
            "unused-trap",
            "E03",
            "fn main() -> i64 {\n"
            "    1 / 0;\n"
            "    return 42;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            23,
            28,
        },
        {
            "unused-callee-trap",
            "E03",
            "fn fail() -> i64 {\n"
            "    return 9 / 0;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    fail();\n"
            "    return 42;\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "fail",
            "",
            30,
            35,
        },
        {
            "unused-total-call",
            "E03",
            "fn identity(x: i64) -> i64 {\n"
            "    return x;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    identity(7);\n"
            "    return 42;\n"
            "}\n",
            "main",
            {},
            "value",
            42LL,
            "",
            "",
            0,
            0,
        },
        {
            "parenthesized-fault-range",
            "E04",
            "fn main() -> i64 {\n"
            "    return (((3 / 0)));\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "main",
            "",
            33,
            38,
        },
        {
            "callee-fault-range",
            "E04",
            "fn fail(x: i64) -> i64 {\n"
            "    return 8 / x;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return fail(0);\n"
            "}\n",
            "main",
            {},
            "arithmetic",
            0LL,
            "fail",
            "",
            36,
            41,
        },
        {
            "initializer-shadowing",
            "S01",
            "fn main() -> i64 {\n"
            "    let x = 20;\n"
            "    if (true) {\n"
            "        let x = x + 22;\n"
            "        return x;\n"
            "    }\n"
            "    return x;\n"
            "}\n",
            "main",
            {},
            "value",
            42LL,
            "",
            "",
            0,
            0,
        },
        {
            "loop-body-scope",
            "S01",
            "fn main() -> i64 {\n"
            "    let x = 10;\n"
            "    let i = 0;\n"
            "    while (i < 4) {\n"
            "        let x = x + 1;\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return x;\n"
            "}\n",
            "main",
            {},
            "value",
            10LL,
            "",
            "",
            0,
            0,
        },
        {
            "duplicate-binding",
            "S02",
            "fn main() -> i64 {\n"
            "    let x = 1;\n"
            "    let x = 2;\n"
            "    return x;\n"
            "}\n",
            "main",
            {},
            "diagnostic",
            0LL,
            "",
            "MPLX2005",
            0,
            0,
        },
        {
            "hidden-function",
            "S02",
            "fn identity() -> i64 {\n"
            "    return 42;\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    let identity = 1;\n"
            "    return identity();\n"
            "}\n",
            "main",
            {},
            "diagnostic",
            0LL,
            "",
            "MPLX2017",
            0,
            0,
        },
        {
            "unreachable-error",
            "S03",
            "fn main() -> i64 {\n"
            "    if (false) {\n"
            "        return missing;\n"
            "    }\n"
            "    return 42;\n"
            "}\n",
            "main",
            {},
            "diagnostic",
            0LL,
            "",
            "MPLX2006",
            0,
            0,
        },
        {
            "implicit-return",
            "F01",
            "fn main() -> i64 {\n"
            "    let x = 42;\n"
            "}\n",
            "main",
            {},
            "value",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "implicit-bool-return",
            "F01",
            "fn main() -> bool {\n"
            "    let x = true;\n"
            "}\n",
            "main",
            {},
            "value",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "forward-call",
            "F01",
            "fn main() -> i64 {\n"
            "    return later();\n"
            "}\n"
            "fn later() -> i64 {\n"
            "    return 42;\n"
            "}\n",
            "main",
            {},
            "value",
            42LL,
            "",
            "",
            0,
            0,
        },
        {
            "recursive-call",
            "F01",
            "fn triangular(n: i64) -> i64 {\n"
            "    if (n == 0) {\n"
            "        return 0;\n"
            "    }\n"
            "    return n + triangular(n - 1);\n"
            "}\n"
            "fn main() -> i64 {\n"
            "    return triangular(10);\n"
            "}\n",
            "main",
            {},
            "value",
            55LL,
            "",
            "",
            0,
            0,
        },
        {
            "host-i64",
            "F02",
            "fn main(value: i64) -> i64 {\n"
            "    return value;\n"
            "}\n",
            "main",
            {(-9223372036854775807LL - 1)},
            "value",
            (-9223372036854775807LL - 1),
            "",
            "",
            0,
            0,
        },
        {
            "host-i32-reject",
            "F02",
            "fn main(value: i32) -> i32 {\n"
            "    return value;\n"
            "}\n",
            "main",
            {2147483648LL},
            "invalid-entry",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "host-bool-reject",
            "F02",
            "fn main(value: bool) -> bool {\n"
            "    return value;\n"
            "}\n",
            "main",
            {2LL},
            "invalid-entry",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "host-arity-reject",
            "F02",
            "fn main(value: i64) -> i64 {\n"
            "    return value;\n"
            "}\n",
            "main",
            {},
            "invalid-entry",
            0LL,
            "",
            "",
            0,
            0,
        },
        {
            "host-bool",
            "F02",
            "fn main(value: bool) -> bool {\n"
            "    return value;\n"
            "}\n",
            "main",
            {1LL},
            "value",
            1LL,
            "",
            "",
            0,
            0,
        },
        {
            "precedence",
            "P01",
            "fn main() -> i64 {\n"
            "    return 2 + 3 * 4 - 8 / 2;\n"
            "}\n",
            "main",
            {},
            "value",
            10LL,
            "",
            "",
            0,
            0,
        },
        {
            "left-associativity",
            "P01",
            "fn main() -> i64 {\n"
            "    return 20 - 5 - 3;\n"
            "}\n",
            "main",
            {},
            "value",
            12LL,
            "",
            "",
            0,
            0,
        },
        {
            "logical-precedence",
            "P01",
            "fn main() -> bool {\n"
            "    return true || false && false;\n"
            "}\n",
            "main",
            {},
            "value",
            1LL,
            "",
            "",
            0,
            0,
        },
    };
} // namespace mplx::conformance
