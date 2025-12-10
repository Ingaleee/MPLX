#include "ir_snapshot.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <type_traits>

namespace mplx {
    namespace {
        bool valid_utf8(std::string_view text) {
            for (size_t index = 0; index < text.size();) {
                uint32_t point = static_cast<unsigned char>(text[index++]);
                if (point < 128)
                    continue;
                unsigned count;
                uint32_t minimum;
                if (point >= 0xc2 && point <= 0xdf) {
                    count = 1;
                    minimum = 128;
                    point &= 31;
                } else if (point >= 0xe0 && point <= 0xef) {
                    count = 2;
                    minimum = 2048;
                    point &= 15;
                } else if (point >= 0xf0 && point <= 0xf4) {
                    count = 3;
                    minimum = 65536;
                    point &= 7;
                } else
                    return false;
                if (count > text.size() - index)
                    return false;
                while (count--) {
                    const auto byte = static_cast<unsigned char>(text[index++]);
                    if ((byte & 0xc0) != 0x80)
                        return false;
                    point = (point << 6) | (byte & 63);
                }
                if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
                    return false;
            }
            return true;
        }
        struct Writer {
            std::string bytes;
            size_t maximum{ir_snapshot_limit};
            void number(uint64_t value, size_t width = 4) {
                for (size_t byte = 0; byte < width; ++byte)
                    bytes.push_back(static_cast<char>((value >> (8 * byte)) & 255));
                if (bytes.size() > maximum)
                    throw std::runtime_error("IR encoding exceeds byte budget");
            }
            void text(const std::string &value) {
                if (value.size() > maximum - std::min(bytes.size() + 4, maximum))
                    throw std::runtime_error("IR snapshot string exceeds size limit");
                number(value.size());
                bytes += value;
            }
            void span(SourceSpan value) {
                for (auto point : {value.start, value.end}) {
                    number(point.offset);
                    number(point.line);
                    number(point.column);
                }
            }
            template <class T> void vector(const std::vector<T> &values) {
                number(values.size());
                for (auto value : values)
                    number(static_cast<uint32_t>(value));
            }
            void block(const IRBlock &value) {
                number(value.id);
                number(value.instructions.size());
                for (const auto &instruction : value.instructions) {
                    number(static_cast<uint32_t>(instruction.op));
                    number(instruction.result);
                    number(static_cast<uint32_t>(instruction.type));
                    vector(instruction.arguments);
                    number(instruction.index);
                    number(std::bit_cast<uint64_t>(static_cast<int64_t>(instruction.constant)), 8);
                    text(instruction.operation);
                    span(instruction.span);
                    vector(instruction.incoming_blocks);
                }
                number(static_cast<uint32_t>(value.terminator.kind));
                number(value.terminator.value);
                number(value.terminator.first);
                number(value.terminator.second);
                span(value.terminator.span);
            }
            void signature(const IRFunction &value) {
                text(value.name);
                number(value.symbol);
                number(static_cast<uint32_t>(value.return_type));
                vector(value.parameters);
                vector(value.locals);
                vector(value.values);
                span(value.span);
                number(value.ssa ? 1 : 0);
            }
        };
        struct Reader {
            std::string_view bytes;
            size_t offset{0};
            uint64_t number(size_t width = 4) {
                if (width > bytes.size() - offset)
                    throw std::runtime_error("truncated IR snapshot");
                uint64_t result = 0;
                for (size_t byte = 0; byte < width; ++byte)
                    result |= uint64_t{static_cast<unsigned char>(bytes[offset++])} << (8 * byte);
                return result;
            }
            uint32_t count(size_t minimum_bytes, uint32_t maximum = 1'000'000) {
                auto value = static_cast<uint32_t>(number());
                if (value > maximum || value > (bytes.size() - offset) / minimum_bytes)
                    throw std::runtime_error("IR snapshot count exceeds bounds");
                return value;
            }
            std::string text() {
                auto size = count(1, static_cast<uint32_t>(ir_snapshot_limit));
                std::string result(bytes.substr(offset, size));
                offset += size;
                if (!valid_utf8(result))
                    throw std::runtime_error("invalid UTF-8 in IR snapshot string");
                return result;
            }
            SourceSpan span() {
                SourceSpan result;
                for (auto point : {&result.start, &result.end}) {
                    point->offset = static_cast<uint32_t>(number());
                    point->line = static_cast<uint32_t>(number());
                    point->column = static_cast<uint32_t>(number());
                }
                if (result.start.offset > result.end.offset || !result.start.line || !result.end.line ||
                    !result.start.column || !result.end.column)
                    throw std::runtime_error("invalid IR source range");
                return result;
            }
            template <class T> std::vector<T> vector() {
                std::vector<T> result;
                const auto size = count(4);
                result.reserve(size);
                for (uint32_t index = 0; index < size; ++index) {
                    const auto value = static_cast<uint32_t>(number());
                    if constexpr (std::is_same_v<T, ValueType>) {
                        if (value >= static_cast<uint32_t>(ValueType::Invalid))
                            throw std::runtime_error("invalid IR snapshot type");
                    }
                    result.push_back(static_cast<T>(value));
                }
                return result;
            }
            IRBlock block() {
                IRBlock result;
                result.id = static_cast<uint32_t>(number());
                const auto size = count(60);
                for (uint32_t index = 0; index < size; ++index) {
                    IRInstruction instruction;
                    const auto op = number();
                    if (op > static_cast<uint32_t>(IROp::Copy))
                        throw std::runtime_error("invalid IR snapshot opcode");
                    instruction.op = static_cast<IROp>(op);
                    instruction.result = static_cast<uint32_t>(number());
                    const auto type = number();
                    if (type >= static_cast<uint32_t>(ValueType::Invalid))
                        throw std::runtime_error("invalid IR snapshot type");
                    instruction.type = static_cast<ValueType>(type);
                    instruction.arguments = vector<uint32_t>();
                    instruction.index = static_cast<uint32_t>(number());
                    instruction.constant = std::bit_cast<int64_t>(number(8));
                    instruction.operation = text();
                    instruction.span = span();
                    instruction.incoming_blocks = vector<uint32_t>();
                    result.instructions.push_back(std::move(instruction));
                }
                const auto kind = number();
                if (kind > static_cast<uint32_t>(TerminatorKind::Return))
                    throw std::runtime_error("invalid IR snapshot terminator");
                result.terminator.kind = static_cast<TerminatorKind>(kind);
                result.terminator.value = static_cast<uint32_t>(number());
                result.terminator.first = static_cast<uint32_t>(number());
                result.terminator.second = static_cast<uint32_t>(number());
                result.terminator.span = span();
                return result;
            }
        };
    } // namespace
    std::string serialize_ir_snapshot(const IRModule &module, size_t maximum_bytes) {
        if (maximum_bytes > ir_content_limit)
            throw std::invalid_argument("IR content budget exceeds 256 MiB");
        Writer writer;
        writer.maximum = maximum_bytes;
        writer.bytes = "MPLXIR\r\n";
        writer.number(ir_snapshot_version);
        writer.number(module.functions.size());
        for (const auto &function : module.functions) {
            writer.signature(function);
            writer.number(function.blocks.size());
            for (const auto &block : function.blocks)
                writer.block(block);
        }
        return writer.bytes;
    }
    IRModule deserialize_ir_snapshot(std::string_view bytes, bool verify) {
        if (bytes.size() > ir_snapshot_limit || bytes.size() < 16 || bytes.substr(0, 8) != "MPLXIR\r\n")
            throw std::runtime_error("invalid IR snapshot header or size");
        Reader reader{bytes, 8};
        if (reader.number() != ir_snapshot_version)
            throw std::runtime_error("unsupported IR snapshot version");
        IRModule result;
        const auto count = reader.count(56, 65536);
        for (uint32_t index = 0; index < count; ++index) {
            IRFunction function;
            function.name = reader.text();
            function.symbol = static_cast<uint32_t>(reader.number());
            const auto type = reader.number();
            if (type >= static_cast<uint32_t>(ValueType::Invalid))
                throw std::runtime_error("invalid IR snapshot return type");
            function.return_type = static_cast<ValueType>(type);
            function.parameters = reader.vector<ValueType>();
            function.locals = reader.vector<ValueType>();
            function.values = reader.vector<ValueType>();
            function.span = reader.span();
            const auto ssa = reader.number();
            if (ssa > 1)
                throw std::runtime_error("noncanonical IR snapshot boolean");
            function.ssa = ssa != 0;
            const auto blocks = reader.count(48, 65536);
            for (uint32_t block = 0; block < blocks; ++block)
                function.blocks.push_back(reader.block());
            result.functions.push_back(std::move(function));
        }
        if (reader.offset != bytes.size())
            throw std::runtime_error("trailing IR snapshot bytes");
        if (verify) {
            const auto errors = verify_ir(result);
            if (!errors.empty())
                throw std::runtime_error("IR snapshot verification failed: " + errors.front().message);
        }
        return result;
    }
    std::string snapshot_hex(std::string_view bytes) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (unsigned char byte : bytes) {
            result += digits[byte >> 4];
            result += digits[byte & 15];
        }
        return result;
    }
    std::string ir_fingerprint(const IRModule &module) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char byte : serialize_ir_snapshot(module, ir_content_limit)) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        std::string bytes;
        for (unsigned index = 0; index < 8; ++index)
            bytes += static_cast<char>(hash >> (8 * (7 - index)));
        return "fnv1a64:" + snapshot_hex(bytes);
    }
    std::vector<IRChange> compare_ir(const IRModule &before, const IRModule &after) {
        std::vector<IRChange> result;
        for (uint32_t index = 0; index < std::max(before.functions.size(), after.functions.size()); ++index) {
            IRChange change{index, false, {}};
            if (index >= before.functions.size() || index >= after.functions.size()) {
                change.signature_changed = true;
                const auto &function =
                    index < before.functions.size() ? before.functions[index] : after.functions[index];
                for (const auto &block : function.blocks)
                    change.blocks.push_back(block.id);
            } else {
                const auto &first = before.functions[index];
                const auto &second = after.functions[index];
                Writer left, right;
                left.maximum = right.maximum = ir_content_limit;
                left.signature(first);
                right.signature(second);
                change.signature_changed = left.bytes != right.bytes;
                for (uint32_t block = 0; block < std::max(first.blocks.size(), second.blocks.size());
                     ++block) {
                    left.bytes.clear();
                    right.bytes.clear();
                    if (block < first.blocks.size())
                        left.block(first.blocks[block]);
                    if (block < second.blocks.size())
                        right.block(second.blocks[block]);
                    if (left.bytes != right.bytes)
                        change.blocks.push_back(block);
                }
            }
            if (change.signature_changed || !change.blocks.empty())
                result.push_back(std::move(change));
        }
        return result;
    }
    std::string ir_changes_json(const std::vector<IRChange> &changes) {
        std::string result = "[";
        for (const auto &change : changes) {
            if (result.size() > 1)
                result += ',';
            result += "{\"function\":" + std::to_string(change.function) +
                      ",\"signature_changed\":" + (change.signature_changed ? "true" : "false") +
                      ",\"blocks\":[";
            for (size_t index = 0; index < change.blocks.size(); ++index) {
                if (index)
                    result += ',';
                result += std::to_string(change.blocks[index]);
            }
            result += "]}";
        }
        return result + "]";
    }
} // namespace mplx
