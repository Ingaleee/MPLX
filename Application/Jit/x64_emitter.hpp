#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <vector>

namespace mplx::jit {
    class X64Emitter {
      public:
        std::vector<uint8_t> bytes;
        void emit(std::initializer_list<uint8_t> value) {
            bytes.insert(bytes.end(), value);
        }
        void u32(uint32_t value) {
            for (int i = 0; i < 4; ++i)
                bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
        void u64(uint64_t value) {
            for (int i = 0; i < 8; ++i)
                bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
        void bind(uint32_t label) {
            labels_[label] = bytes.size();
        }
        size_t position(uint32_t label) const {
            return labels_.at(label);
        }
        void fixup(uint32_t label) {
            fixups_.push_back({bytes.size(), label});
            u32(0);
        }
        void jump(uint32_t label, uint8_t condition = 0) {
            if (condition)
                emit({0x0f, condition});
            else
                emit({0xe9});
            fixup(label);
        }
        bool finalize() {
            for (const auto &f : fixups_) {
                auto target = labels_.find(f.label);
                if (target == labels_.end())
                    return false;
                auto delta = static_cast<int64_t>(target->second) - static_cast<int64_t>(f.position + 4);
                if (delta < INT32_MIN || delta > INT32_MAX)
                    return false;
                auto value = static_cast<uint32_t>(delta);
                for (int i = 0; i < 4; ++i)
                    bytes[f.position + i] = static_cast<uint8_t>(value >> (8 * i));
            }
            return true;
        }
        void load_rax(uint32_t slot) {
            emit({0x49, 0x8b, 0x85});
            u32(slot * 8);
        }
        void load_rcx(uint32_t slot) {
            emit({0x49, 0x8b, 0x8d});
            u32(slot * 8);
        }
        void store_rax(uint32_t slot) {
            emit({0x49, 0x89, 0x85});
            u32(slot * 8);
        }
        // Cache register indices 0..3 select R8..R11. These instructions only
        // move data; spill sequences preserve arithmetic flags and scratch values.
        void scratch_to_cached(uint32_t reg, bool rcx) {
            emit({0x49, 0x89, static_cast<uint8_t>(0xc0 | (rcx ? 8 : 0) | reg)});
        }
        void cached_to_scratch(uint32_t reg, bool rcx) {
            emit({0x49, 0x8b, static_cast<uint8_t>(0xc0 | (rcx ? 8 : 0) | reg)});
        }
        void cached_to_memory(uint32_t reg, uint32_t slot) {
            emit({0x4d, 0x89, static_cast<uint8_t>(0x85 | (reg << 3))});
            u32(slot * 8);
        }
        void immediate_rax(uint64_t value) {
            emit({0x48, 0xb8});
            u64(value);
        }
        void context_load_rax(size_t offset) {
            emit({0x49, 0x8b, 0x84, 0x24});
            u32(static_cast<uint32_t>(offset));
        }
        void context_load_r13(size_t offset) {
            emit({0x4d, 0x8b, 0xac, 0x24});
            u32(static_cast<uint32_t>(offset));
        }
        void context_store_rax(size_t offset) {
            emit({0x49, 0x89, 0x84, 0x24});
            u32(static_cast<uint32_t>(offset));
        }
        void context_u32(size_t offset, uint32_t value) {
            emit({0x41, 0xc7, 0x84, 0x24});
            u32(static_cast<uint32_t>(offset));
            u32(value);
        }
        void context_add_rax(size_t offset) {
            emit({0x49, 0x03, 0x84, 0x24});
            u32(static_cast<uint32_t>(offset));
        }
        void context_cmp_rax(size_t offset) {
            emit({0x49, 0x3b, 0x84, 0x24});
            u32(static_cast<uint32_t>(offset));
        }

      private:
        struct Fixup {
            size_t position;
            uint32_t label;
        };
        std::vector<Fixup> fixups_;
        std::map<uint32_t, size_t> labels_;
    };
} // namespace mplx::jit
