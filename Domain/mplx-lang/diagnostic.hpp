#pragma once
#include "source.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mplx {
    inline std::string json_quote(const std::string &value) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string out = "\"";
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += static_cast<char>(c);
            } else if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else
                out += static_cast<char>(c);
        }
        return out + '"';
    }
    struct Diagnostic {
        std::string code, message;
        uint32_t line{0}, col{0};
        SourceSpan span;
    };
    inline std::string span_json(SourceSpan span) {
        auto point = [](SourcePosition p) {
            return "{\"offset\":" + std::to_string(p.offset) + ",\"line\":" + std::to_string(p.line) +
                   ",\"column\":" + std::to_string(p.column) + "}";
        };
        return "{\"start\":" + point(span.start) + ",\"end\":" + point(span.end) + "}";
    }
    inline std::string diagnostics_json(const std::vector<Diagnostic> &diagnostics) {
        std::string out = "{\"diagnostics\":[";
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            if (i)
                out += ',';
            const auto &d = diagnostics[i];
            out += "{\"code\":" + json_quote(d.code) + ",\"message\":" + json_quote(d.message) +
                   ",\"line\":" + std::to_string(d.line) + ",\"col\":" + std::to_string(d.col) +
                   ",\"range\":" + span_json(d.span) + '}';
        }
        return out + "]}";
    }
} // namespace mplx
