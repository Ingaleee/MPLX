#pragma once

#include "frontend.hpp"
#include "verified_module.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mplx {

    struct SourceOverlay {
        std::string path;
        std::string source;
    };

    struct WorkspaceDiagnostic {
        std::string path;
        Diagnostic diagnostic;
    };

    struct DependencyEdge {
        std::string from;
        std::string to;
        std::string alias;
        SourceSpan span;
        SourceSpan path_span;
        SourceSpan alias_span;
    };

    struct WorkspaceFile {
        std::string path;
        std::string source;
        uint64_t revision{0};
        SemanticModel semantic;
        std::vector<Diagnostic> diagnostics;
    };

    struct BuildStatistics {
        size_t parsed{0};
        size_t syntax_hits{0};
        size_t checked{0};
        size_t semantic_hits{0};
        size_t files{0};
        size_t source_bytes{0};
        size_t linked_functions{0};
        bool artifact_hit{false};
        std::vector<std::string> affected;
    };

    struct WorkspaceResult {
        std::string entry;
        std::vector<WorkspaceFile> files;
        std::vector<DependencyEdge> dependencies;
        std::vector<WorkspaceDiagnostic> diagnostics;
        BuildStatistics statistics;
        std::shared_ptr<const VerifiedModule> artifact;
        IRModule ir;
        OptimizationReport optimization;

        bool ok() const {
            return diagnostics.empty();
        }
    };

    // Providers receive a canonical, root-relative UTF-8 path.
    // No provider or overlay can widen the filesystem root.
    using SourceProvider = std::function<std::optional<std::string>(const std::string &)>;

    class WorkspaceCompiler {
      public:
        explicit WorkspaceCompiler(std::filesystem::path root, SourceProvider provider = {});
        ~WorkspaceCompiler();
        WorkspaceCompiler(const WorkspaceCompiler &) = delete;
        WorkspaceCompiler &operator=(const WorkspaceCompiler &) = delete;

        WorkspaceResult build(const std::string &entry, std::span<const SourceOverlay> overlays = {},
                              bool optimize = true, bool emit_artifact = true);
        std::string canonical_path(const std::string &path) const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    std::string workspace_json(const WorkspaceResult &result);
    std::string workspace_statistics_json(const BuildStatistics &statistics);
    std::string workspace_diagnostics_json(const WorkspaceResult &result);
    std::filesystem::path utf8_path(std::string_view text);

} // namespace mplx
