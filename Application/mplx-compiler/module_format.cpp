#include "module_format.hpp"
#include "diagnostic.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
namespace mplx {
    namespace {
        constexpr size_t header_size = 64, section_count = 6, directory_stride = 32, data_offset = 256;
        constexpr std::array<uint8_t, 8> magic = {'M', 'P', 'L', 'X', 'B', 'C', 13, 10};
        constexpr auto crc_table = [] {
            std::array<uint32_t, 256> table{};
            for (uint32_t n = 0; n < 256; ++n) {
                auto c = n;
                for (int b = 0; b < 8; ++b)
                    c = (c >> 1) ^ ((c & 1) ? 0x82f63b78u : 0);
                table[n] = c;
            }
            return table;
        }();
        [[noreturn]] void reject(ModuleError code, const char *message) {
            throw ModuleFormatError(code, message);
        }
        uint64_t read(std::span<const uint8_t> bytes, size_t offset, size_t width) {
            if (offset > bytes.size() || width > bytes.size() - offset)
                reject(ModuleError::Size, "truncated module field");
            uint64_t result = 0;
            for (size_t b = 0; b < width; ++b)
                result |= uint64_t(bytes[offset + b]) << (8 * b);
            return result;
        }
        void put(std::vector<uint8_t> &bytes, size_t offset, uint64_t value, size_t width) {
            for (size_t b = 0; b < width; ++b)
                bytes[offset + b] = static_cast<uint8_t>(value >> (8 * b));
        }
        void append(std::vector<uint8_t> &bytes, uint64_t value, size_t width) {
            const auto offset = bytes.size();
            bytes.resize(offset + width);
            put(bytes, offset, value, width);
        }
        bool utf8_name(std::span<const uint8_t> bytes) {
            if (bytes.empty() || bytes.size() > 1024)
                return false;
            for (size_t i = 0; i < bytes.size();) {
                uint32_t c = bytes[i++];
                if (c == 0)
                    return false;
                if (c < 128)
                    continue;
                unsigned count = 0;
                uint32_t minimum = 0;
                if (c >= 0xc2 && c <= 0xdf) {
                    count = 1;
                    c &= 31;
                    minimum = 128;
                } else if (c >= 0xe0 && c <= 0xef) {
                    count = 2;
                    c &= 15;
                    minimum = 2048;
                } else if (c >= 0xf0 && c <= 0xf4) {
                    count = 3;
                    c &= 7;
                    minimum = 65536;
                } else
                    return false;
                if (count > bytes.size() - i)
                    return false;
                while (count--) {
                    auto next = bytes[i++];
                    if ((next & 0xc0) != 0x80)
                        return false;
                    c = (c << 6) | (next & 63);
                }
                if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
                    return false;
            }
            return true;
        }
        ValueType decode_type(uint64_t value) {
            switch (value) {
            case 0:
                return ValueType::I32;
            case 1:
                return ValueType::I64;
            case 2:
                return ValueType::Bool;
            default:
                reject(ModuleError::Metadata, "unknown export value type");
            }
        }
        uint8_t encode_type(ValueType value) {
            switch (value) {
            case ValueType::I32:
                return 0;
            case ValueType::I64:
                return 1;
            case ValueType::Bool:
                return 2;
            default:
                reject(ModuleError::Metadata, "invalid export value type");
            }
        }
        constexpr std::array<uint32_t, 6> strides = {1, 24, 8, 1, 28, 1};
        constexpr std::array<uint64_t, 6> limits = {8 * 1024 * 1024,  65536,      1000000, 16 * 1024 * 1024,
                                                    16 * 1024 * 1024, 65536 * 255};
        struct Section {
            size_t offset, size, count;
        };
        std::array<Section, 6> directory(std::span<const uint8_t> bytes) {
            std::array<Section, 6> sections{};
            size_t cursor = data_offset;
            for (size_t i = 0; i < section_count; ++i) {
                const auto at = header_size + i * directory_stride;
                auto kind = read(bytes, at, 4), stride = read(bytes, at + 4, 4),
                     offset = read(bytes, at + 8, 8), size = read(bytes, at + 16, 8),
                     count = read(bytes, at + 24, 8);
                if (kind != i + 1 || stride != strides[i] || count > limits[i] || count * stride != size ||
                    offset != cursor || size > bytes.size() - cursor)
                    reject(ModuleError::Directory, "noncanonical or out-of-bounds module section");
                sections[i] = {cursor, static_cast<size_t>(size), static_cast<size_t>(count)};
                cursor += static_cast<size_t>(size);
            }
            if (cursor != bytes.size() || !sections[1].count || !sections[3].count)
                reject(ModuleError::Directory, "missing sections or trailing bytes");
            return sections;
        }
    } // namespace
    uint32_t crc32c(std::span<const uint8_t> bytes) {
        uint32_t value = UINT32_MAX;
        for (auto byte : bytes)
            value = crc_table[(value ^ byte) & 255] ^ (value >> 8);
        return ~value;
    }
    std::vector<uint8_t> serialize_module(const VerifiedModule &module) {
        const auto &bc = module.bytecode();
        size_t name_bytes = 0, type_bytes = 0;
        for (const auto &f : bc.functions) {
            if (f.name.size() > 1024 || f.name.size() > limits[0] - name_bytes)
                reject(ModuleError::Size, "export name pool exceeds format limits");
            name_bytes += f.name.size();
            type_bytes += f.arity;
        }
        const std::array<size_t, 6> counts = {name_bytes,     bc.functions.size(),  bc.consts.size(),
                                              bc.code.size(), bc.source_map.size(), type_bytes};
        size_t encoded_size = data_offset;
        for (size_t i = 0; i < counts.size(); ++i) {
            if (counts[i] > limits[i] || counts[i] > (max_module_bytes - encoded_size) / strides[i])
                reject(ModuleError::Size, "module exceeds format limits");
            encoded_size += counts[i] * strides[i];
        }
        std::array<std::vector<uint8_t>, 6> sections;
        for (size_t i = 0; i < sections.size(); ++i)
            sections[i].reserve(counts[i] * strides[i]);
        uint32_t names = 0, types = 0;
        for (const auto &f : bc.functions) {
            const auto name = std::span(reinterpret_cast<const uint8_t *>(f.name.data()), f.name.size());
            if (!utf8_name(name))
                reject(ModuleError::Metadata, "export name is empty, invalid UTF-8 or exceeds 1024 bytes");
            append(sections[1], names, 4);
            append(sections[1], f.name.size(), 4);
            append(sections[1], f.entry, 4);
            append(sections[1], types, 4);
            append(sections[1], f.locals, 2);
            append(sections[1], f.arity, 1);
            append(sections[1], encode_type(f.return_type), 1);
            append(sections[1], 0, 4);
            sections[0].insert(sections[0].end(), name.begin(), name.end());
            names += static_cast<uint32_t>(name.size());
            for (auto type : f.parameters)
                sections[5].push_back(encode_type(type));
            types += f.arity;
        }
        for (auto value : bc.consts)
            append(sections[2], std::bit_cast<uint64_t>(static_cast<int64_t>(value)), 8);
        sections[3] = bc.code;
        for (const auto &mapping : bc.source_map) {
            append(sections[4], mapping.offset, 4);
            for (auto p : {mapping.span.start, mapping.span.end}) {
                append(sections[4], p.offset, 4);
                append(sections[4], p.line, 4);
                append(sections[4], p.column, 4);
            }
        }
        size_t total = data_offset;
        for (size_t i = 0; i < section_count; ++i) {
            if (sections[i].size() / strides[i] > limits[i] || sections[i].size() > max_module_bytes - total)
                reject(ModuleError::Size, "module exceeds format limits");
            total += sections[i].size();
        }
        std::vector<uint8_t> result(data_offset, 0);
        result.reserve(total);
        std::copy(magic.begin(), magic.end(), result.begin());
        put(result, 8, 1, 2);
        put(result, 10, 0, 2);
        put(result, 12, header_size, 4);
        put(result, 16, 1, 8);
        put(result, 24, total, 8);
        put(result, 32, section_count, 4);
        put(result, 36, directory_stride, 4);
        put(result, 40, header_size, 8);
        for (size_t i = 0; i < section_count; ++i) {
            const auto at = header_size + i * directory_stride;
            put(result, at, i + 1, 4);
            put(result, at + 4, strides[i], 4);
            put(result, at + 8, result.size(), 8);
            put(result, at + 16, sections[i].size(), 8);
            put(result, at + 24, sections[i].size() / strides[i], 8);
            result.insert(result.end(), sections[i].begin(), sections[i].end());
        }
        put(result, 48, crc32c(std::span(result).subspan(header_size)), 4);
        put(result, 52, crc32c(std::span(result).first(header_size)), 4);
        return result;
    }
    std::shared_ptr<const VerifiedModule> deserialize_module(std::span<const uint8_t> bytes) {
        if (bytes.size() < data_offset || bytes.size() > max_module_bytes)
            reject(ModuleError::Size, "module size outside supported range");
        if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
            reject(ModuleError::Header, "invalid module magic");
        if (read(bytes, 8, 2) != 1 || read(bytes, 10, 2) != 0)
            reject(ModuleError::Version, "unsupported module format version");
        if (read(bytes, 12, 4) != header_size || read(bytes, 32, 4) != section_count ||
            read(bytes, 36, 4) != directory_stride || read(bytes, 40, 8) != header_size ||
            read(bytes, 56, 8) != 0)
            reject(ModuleError::Header, "invalid module header layout or reserved field");
        if (read(bytes, 16, 8) != 1)
            reject(ModuleError::Features, "unsupported module feature flags");
        if (read(bytes, 24, 8) != bytes.size())
            reject(ModuleError::Size, "module length mismatch");
        std::array<uint8_t, header_size> header{};
        std::copy_n(bytes.begin(), header_size, header.begin());
        std::fill_n(header.begin() + 52, 4, uint8_t{0});
        if (crc32c(header) != read(bytes, 52, 4) || crc32c(bytes.subspan(header_size)) != read(bytes, 48, 4))
            reject(ModuleError::Checksum, "module checksum mismatch");
        const auto sections = directory(bytes);
        Bytecode bc;
        // All counts/strides/ranges have been checked before payload allocations.
        bc.functions.reserve(sections[1].count);
        bc.consts.reserve(sections[2].count);
        bc.source_map.reserve(sections[4].count);
        size_t name_cursor = 0, type_cursor = 0;
        for (size_t i = 0; i < sections[1].count; ++i) {
            auto at = sections[1].offset + i * 24;
            auto name_offset = read(bytes, at, 4), name_size = read(bytes, at + 4, 4),
                 arity = read(bytes, at + 18, 1), type_offset = read(bytes, at + 12, 4);
            if (name_offset != name_cursor || name_size > sections[0].size - name_cursor ||
                type_offset != type_cursor || arity > sections[5].size - type_cursor ||
                read(bytes, at + 20, 4) != 0)
                reject(ModuleError::Metadata, "invalid export name/signature range");
            auto name = bytes.subspan(sections[0].offset + name_cursor, static_cast<size_t>(name_size));
            if (!utf8_name(name))
                reject(ModuleError::Metadata, "invalid export UTF-8 name");
            FuncMeta f;
            f.name.assign(reinterpret_cast<const char *>(name.data()), name.size());
            f.entry = static_cast<uint32_t>(read(bytes, at + 8, 4));
            f.locals = static_cast<uint16_t>(read(bytes, at + 16, 2));
            f.arity = static_cast<uint8_t>(arity);
            f.return_type = decode_type(read(bytes, at + 19, 1));
            f.parameters.reserve(f.arity);
            for (size_t p = 0; p < arity; ++p)
                f.parameters.push_back(decode_type(bytes[sections[5].offset + type_cursor + p]));
            name_cursor += name.size();
            type_cursor += f.arity;
            bc.functions.push_back(std::move(f));
        }
        if (name_cursor != sections[0].size || type_cursor != sections[5].size)
            reject(ModuleError::Metadata, "unreferenced name/signature data");
        for (size_t i = 0; i < sections[2].count; ++i)
            bc.consts.push_back(std::bit_cast<int64_t>(read(bytes, sections[2].offset + i * 8, 8)));
        bc.code.assign(bytes.begin() + sections[3].offset,
                       bytes.begin() + sections[3].offset + sections[3].size);
        for (size_t i = 0; i < sections[4].count; ++i) {
            const auto at = sections[4].offset + i * 28;
            auto position = [&](size_t p) {
                return SourcePosition{static_cast<uint32_t>(read(bytes, p, 4)),
                                      static_cast<uint32_t>(read(bytes, p + 4, 4)),
                                      static_cast<uint32_t>(read(bytes, p + 8, 4))};
            };
            bc.source_map.push_back(
                {static_cast<uint32_t>(read(bytes, at, 4)), {position(at + 4), position(at + 16)}});
        }
        try {
            return VerifiedModule::create(std::move(bc));
        } catch (const std::runtime_error &e) {
            throw ModuleFormatError(ModuleError::Bytecode, e.what());
        }
    }
    std::string module_info_json(const VerifiedModule &module) {
        const auto &bc = module.bytecode();
        std::string result =
            "{\"format_version\":\"1.0\",\"code_bytes\":" + std::to_string(bc.code.size()) +
            ",\"constants\":" + std::to_string(bc.consts.size()) +
            ",\"instructions\":" + std::to_string(module.verification().instructions.size()) +
            ",\"source_mappings\":" + std::to_string(bc.source_map.size()) + ",\"exports\":[";
        bool first = true;
        for (const auto &f : bc.functions) {
            if (!first)
                result += ',';
            first = false;
            result += "{\"name\":" + json_quote(f.name) +
                      ",\"return_type\":" + json_quote(type_name(f.return_type)) + ",\"parameters\":[";
            for (size_t p = 0; p < f.parameters.size(); ++p) {
                if (p)
                    result += ',';
                result += json_quote(type_name(f.parameters[p]));
            }
            result +=
                "],\"entry\":" + std::to_string(f.entry) + ",\"locals\":" + std::to_string(f.locals) + "}";
        }
        return result + "]}";
    }
} // namespace mplx
