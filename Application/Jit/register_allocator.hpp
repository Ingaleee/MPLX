#pragma once
#include "machine_ir.hpp"
#include "x64_emitter.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mplx::jit {
    class RegisterAllocator {
      public:
        RegisterAllocator(X64Emitter &emitter, const MachineIR &module, uint32_t registers)
            : emitter_(emitter), module_(module), count_(registers) {
            if (count_ > cache_.size())
                throw std::invalid_argument("native register count exceeds four");
        }
        void position(uint32_t pc) {
            pc_ = pc;
        }
        void flush() {
            for (uint32_t reg = 0; reg < count_; ++reg) {
                spill(reg, false);
                cache_[reg] = {};
            }
        }
        void load(uint32_t value, bool rcx = false) {
            const auto found = find(value);
            if (found != no_machine_value) {
                emitter_.cached_to_scratch(found, rcx);
                ++stats_.register_reads;
                return;
            }
            const auto slot = module_.values[value].slot;
            if (rcx)
                emitter_.load_rcx(slot);
            else
                emitter_.load_rax(slot);
            ++stats_.memory_reads;
            if (count_) {
                const auto reg = allocate();
                emitter_.scratch_to_cached(reg, rcx);
                cache_[reg] = {{value, false}};
                ++stats_.register_writes;
            }
        }
        void store(uint32_t value) {
            const auto slot = module_.values[value].slot;
            if (!count_) {
                emitter_.store_rax(slot);
                ++stats_.memory_writes;
                return;
            }
            forget_slot(slot);
            const auto reg = allocate();
            emitter_.scratch_to_cached(reg, false);
            cache_[reg] = {{value, true}};
            ++stats_.register_writes;
        }
        void copy(uint32_t source, uint32_t destination) {
            if (!count_) {
                load(source);
                store(destination);
                return;
            }
            auto reg = find(source);
            if (reg == no_machine_value) {
                load(source);
                reg = find(source);
            } else {
                ++stats_.register_reads;
            }
            // Equivalent values share one register, but retain every live arena
            // backing. Overwriting one backing must not change its other aliases.
            forget_slot(module_.values[destination].slot);
            cache_[reg].push_back({destination, true});
            ++stats_.coalesced_copies;
        }
        const AllocationStats &stats() const {
            return stats_;
        }

      private:
        struct Cached {
            uint32_t value{no_machine_value};
            bool dirty{false};
        };
        X64Emitter &emitter_;
        const MachineIR &module_;
        uint32_t count_, pc_{0};
        std::array<std::vector<Cached>, 4> cache_{};
        AllocationStats stats_;
        uint32_t find(uint32_t value) const {
            for (uint32_t reg = 0; reg < count_; ++reg)
                for (const auto &alias : cache_[reg])
                    if (alias.value == value)
                        return reg;
            return no_machine_value;
        }
        void forget_slot(uint32_t slot) {
            for (uint32_t reg = 0; reg < count_; ++reg)
                std::erase_if(cache_[reg],
                              [&](const auto &alias) { return module_.values[alias.value].slot == slot; });
        }
        void spill(uint32_t reg, bool eviction) {
            bool dirty = false;
            for (const auto &alias : cache_[reg]) {
                if (!alias.dirty)
                    continue;
                emitter_.cached_to_memory(reg, module_.values[alias.value].slot);
                ++stats_.memory_writes;
                dirty = true;
            }
            stats_.spills += eviction && dirty;
        }
        uint32_t allocate() {
            uint32_t victim = 0, farthest = 0;
            for (uint32_t reg = 0; reg < count_; ++reg) {
                if (cache_[reg].empty())
                    return reg;
                uint32_t distance = UINT32_MAX;
                for (const auto &alias : cache_[reg]) {
                    const auto &uses = module_.values[alias.value].uses;
                    const auto next = std::upper_bound(uses.begin(), uses.end(), pc_);
                    if (next != uses.end())
                        distance = std::min(distance, *next);
                }
                if (distance >= farthest) {
                    victim = reg;
                    farthest = distance;
                }
            }
            spill(victim, true);
            cache_[victim] = {};
            return victim;
        }
    };
} // namespace mplx::jit
