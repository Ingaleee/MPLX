#pragma once
#include <cstdint>
#include <string>

namespace mplx {
    struct SourcePosition {
        uint32_t offset{0}, line{1}, column{1};
        bool operator==(const SourcePosition &) const = default;
    };
    struct SourceSpan {
        SourcePosition start, end;
        bool operator==(const SourceSpan &) const = default;
        bool contains(uint32_t offset) const {
            return start.offset <= offset && offset < end.offset;
        }
    };
    inline SourceSpan join(SourceSpan a, SourceSpan b) {
        return {a.start, b.end};
    }
    enum class ValueType : uint8_t { I32, I64, Bool, Invalid };
    inline const char *type_name(ValueType type) {
        switch (type) {
        case ValueType::I32:
            return "i32";
        case ValueType::I64:
            return "i64";
        case ValueType::Bool:
            return "bool";
        default:
            return "<invalid>";
        }
    }
    inline ValueType parse_type(const std::string &text) {
        return text == "i32"    ? ValueType::I32
               : text == "i64"  ? ValueType::I64
               : text == "bool" ? ValueType::Bool
                                : ValueType::Invalid;
    }
    inline bool integer_type(ValueType type) {
        return type == ValueType::I32 || type == ValueType::I64;
    }
    inline bool valid_type(ValueType type) {
        return integer_type(type) || type == ValueType::Bool;
    }
} // namespace mplx
