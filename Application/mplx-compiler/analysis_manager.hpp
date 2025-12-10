#pragma once

#include "ir_analysis.hpp"
#include "range_analysis.hpp"
#include <array>
#include <map>
#include <memory>
#include <set>

namespace mplx {
    enum class AnalysisKind : uint8_t {
        ControlFlow,
        PostDominance,
        Loops,
        DefUse,
        Liveness,
        Effects,
        Ranges,
        Count
    };
    using PreservedAnalyses = uint32_t;
    constexpr PreservedAnalyses preserve(AnalysisKind kind) {
        return uint32_t{1} << static_cast<unsigned>(kind);
    }
    constexpr PreservedAnalyses preserve_cfg = preserve(AnalysisKind::ControlFlow) |
                                               preserve(AnalysisKind::PostDominance) |
                                               preserve(AnalysisKind::Loops);
    struct AnalysisCounters {
        uint64_t requests{0}, computations{0}, hits{0}, invalidations{0};
    };
    using AnalysisStatistics = std::array<AnalysisCounters, static_cast<size_t>(AnalysisKind::Count)>;
    struct PostDominance {
        // Virtual exit has index blocks.size(). Non-exiting blocks have no idom.
        std::vector<uint32_t> immediate, enter, leave;
        std::vector<bool> reaches_exit;
        uint32_t virtual_exit{no_id};
        bool postdominates(uint32_t definition, uint32_t use) const;
    };
    struct NaturalLoop {
        uint32_t header{no_id}, parent{no_id}, depth{1};
        std::vector<uint32_t> blocks, latches;
        std::vector<std::pair<uint32_t, uint32_t>> exits;
    };
    struct LoopAnalysis {
        std::vector<NaturalLoop> loops;
        std::vector<std::vector<uint32_t>> components;
        std::vector<uint32_t> component_of;
        std::vector<bool> cyclic, irreducible;
    };
    struct ValueLocation {
        uint32_t block{no_id}, instruction{no_id}, predecessor{no_id};
        bool operator==(const ValueLocation &) const = default;
    };
    struct DefUseAnalysis {
        std::vector<ValueLocation> definitions;
        std::vector<std::vector<ValueLocation>> uses;
    };
    struct ValueLiveness {
        std::vector<std::set<uint32_t>> live_in, live_out;
        std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> edge_live;
    };
    struct EffectSummary {
        bool reads_local{false}, writes_local{false}, may_trap{false}, may_diverge{false};
        bool reads_external{false}, writes_external{false}, unknown_call{false};
        bool can_discard() const;
        bool operator==(const EffectSummary &) const = default;
    };
    struct ModuleEffects {
        std::vector<std::vector<uint32_t>> callees, callers, components;
        std::vector<bool> recursive;
        std::vector<EffectSummary> functions;
    };
    EffectSummary instruction_effects(const IRInstruction &instruction);
    PostDominance analyze_postdominance(const IRFunction &function, const ControlFlow &cfg);
    LoopAnalysis analyze_loops(const IRFunction &function, const ControlFlow &cfg);
    DefUseAnalysis analyze_def_use(const IRFunction &function);
    ValueLiveness analyze_liveness(const IRFunction &function, const ControlFlow &cfg);
    ModuleEffects analyze_effects(const IRModule &module);

    // Keys compare canonical content, not hashes: a fingerprint collision cannot
    // cause reuse. Results are immutable and belong to exactly one IR revision.
    class IRAnalysisManager {
      public:
        explicit IRAnalysisManager(const IRModule &module);
        IRAnalysisManager(const IRModule &module, const IRAnalysisManager &previous);
        std::shared_ptr<const ControlFlow> control_flow(uint32_t function);
        std::shared_ptr<const PostDominance> postdominance(uint32_t function);
        std::shared_ptr<const LoopAnalysis> loops(uint32_t function);
        std::shared_ptr<const DefUseAnalysis> def_use(uint32_t function);
        std::shared_ptr<const ValueLiveness> liveness(uint32_t function);
        std::shared_ptr<const ModuleEffects> effects();
        std::shared_ptr<const RangeAnalysis> ranges(uint32_t function);
        void finish_pass(const IRModule &before, PreservedAnalyses preserved);
        void adopt_cache(const IRAnalysisManager &previous);
        const AnalysisStatistics &statistics() const;

      private:
        struct Entry {
            std::string key;
            std::shared_ptr<const void> value;
        };
        const IRModule &module_;
        std::map<std::pair<AnalysisKind, uint32_t>, Entry> cache_;
        AnalysisStatistics statistics_{};
        std::string key(AnalysisKind kind, uint32_t function) const;
        template <class T, class Compute>
        std::shared_ptr<const T> get(AnalysisKind kind, uint32_t function, Compute compute);
    };
    std::string analysis_statistics_json(const AnalysisStatistics &statistics);
    std::string dump_analyses_json(const IRModule &module);
} // namespace mplx
