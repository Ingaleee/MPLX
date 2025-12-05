#pragma once

#include "analysis_manager.hpp"
#include "ir_snapshot.hpp"
#include <functional>
#include <stdexcept>

namespace mplx {
    struct IRSize {
        size_t blocks{0}, instructions{0}, locals{0}, phis{0};
    };
    struct PassRecord {
        std::string name;
        IRSize before, after;
        uint64_t elapsed_microseconds{0};
        std::string before_fingerprint, after_fingerprint;
        std::vector<IRChange> changes;
        AnalysisStatistics analyses{};
        PreservedAnalyses preserved{0};
        std::string before_snapshot, after_snapshot, failure;
    };
    struct OptimizationReport {
        std::vector<PassRecord> passes;
        bool reused{false};
    };
    struct PassRunOptions {
        bool capture_snapshots{false};
        size_t snapshot_budget{64 * 1024 * 1024};
    };
    class IRPassFailure : public std::runtime_error {
      public:
        explicit IRPassFailure(PassRecord record);
        const PassRecord &record() const;

      private:
        PassRecord record_;
    };
    class IRPassManager {
      public:
        using Transformation = std::function<void(IRModule &)>;
        using AnalyzedTransformation = std::function<PreservedAnalyses(IRModule &, IRAnalysisManager &)>;
        void add(std::string name, Transformation transformation);
        void add(std::string name, AnalyzedTransformation transformation);
        OptimizationReport run(IRModule &module, PassRunOptions options = {},
                               const std::string &only = {}) const;

      private:
        std::vector<std::pair<std::string, AnalyzedTransformation>> passes_;
    };
    OptimizationReport run_optimization_pipeline(IRModule &module, PassRunOptions options = {},
                                                 const std::string &only = {});
    std::string optimization_report_json(const OptimizationReport &report);
    std::string optimization_trace_json(const OptimizationReport &report);
    std::string optimization_failure_json(const IRPassFailure &failure);
} // namespace mplx
