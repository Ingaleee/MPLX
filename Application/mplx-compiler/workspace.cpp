#include "workspace.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace mplx {
    std::filesystem::path utf8_path(std::string_view text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
    namespace {
        constexpr size_t max_files = 256;
        constexpr size_t max_depth = 64;
        constexpr size_t max_edges = 4096;
        constexpr size_t max_functions = 65536;
        constexpr size_t max_import_metadata = 16 * 1024 * 1024;
        constexpr size_t max_source = 1024 * 1024;
        constexpr size_t max_total_source = 16 * max_source;
        constexpr size_t max_cached_files = 512;
        constexpr size_t max_cached_bytes = 32 * max_source;

        std::string utf8(const std::filesystem::path &path) {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char *>(value.data()), value.size()};
        }

        std::string path_key(std::string value) {
#if defined(_WIN32)
            for (auto &c : value) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c + ('a' - 'A'));
                }
            }
#endif
            return value;
        }

        std::string interface_key(std::span<const ImportedFunction> imports) {
            // Exact, length-delimited data: cache correctness does not depend on a hash collision assumption.
            std::string key;
            auto field = [&](const std::string &text) { key += std::to_string(text.size()) + ":" + text; };
            for (const auto &value : imports) {
                field(value.name);
                field(value.definition_path);
                field(value.original_name);
                field(type_name(value.type));
                for (size_t p = 0; p < value.parameters.size(); ++p) {
                    field(type_name(value.parameters[p]));
                    field(value.parameter_names[p]);
                }
                key += ';';
            }
            return key;
        }

        ImportedFunction imported_function(const std::string &path, const std::string &alias,
                                           const Function &function) {
            ImportedFunction result;
            result.name = alias + "::" + function.name;
            result.definition_path = path;
            result.original_name = function.name;
            result.declaration = function.name_span;
            result.type = parse_type(function.returnType);
            for (const auto &parameter : function.params) {
                result.parameters.push_back(parse_type(parameter.typeName));
                result.parameter_names.push_back(parameter.name);
            }
            return result;
        }

        std::string identity(const std::string &path, const std::string &name) {
            return path + "::" + name;
        }

        std::string statistics_json(const BuildStatistics &s) {
            std::string result = "{\"parsed\":" + std::to_string(s.parsed) +
                                 ",\"syntax_hits\":" + std::to_string(s.syntax_hits) +
                                 ",\"checked\":" + std::to_string(s.checked) +
                                 ",\"semantic_hits\":" + std::to_string(s.semantic_hits) +
                                 ",\"files\":" + std::to_string(s.files) +
                                 ",\"source_bytes\":" + std::to_string(s.source_bytes) +
                                 ",\"linked_functions\":" + std::to_string(s.linked_functions) +
                                 ",\"artifact_hit\":" + (s.artifact_hit ? "true" : "false") +
                                 ",\"affected\":[";
            for (size_t i = 0; i < s.affected.size(); ++i) {
                if (i) {
                    result += ',';
                }
                result += json_quote(s.affected[i]);
            }
            return result + "]}";
        }
    } // namespace

    struct WorkspaceCompiler::Impl {
        struct Parsed {
            std::string source;
            Module module;
            std::vector<Diagnostic> diagnostics;
            uint64_t revision{0};
        };

        struct Cached {
            std::shared_ptr<const Parsed> parsed;
            std::shared_ptr<const SemanticResult> semantic;
            std::string interface;
            uint64_t semantic_revision{0};
            uint64_t used{0};
        };

        struct Linked {
            std::string key;
            std::shared_ptr<const VerifiedModule> artifact;
            IRModule ir;
            OptimizationReport optimization;
        };

        std::filesystem::path root;
        SourceProvider provider;
        std::map<std::string, Cached> cache;
        std::map<std::string, Linked> artifacts;
        uint64_t revision{0};
        uint64_t clock{0};

        Impl(std::filesystem::path directory, SourceProvider source_provider)
            : root(std::filesystem::weakly_canonical(std::filesystem::absolute(directory))),
              provider(std::move(source_provider)) {
        }

        std::string canonical(const std::string &text) const {
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string::npos) {
                throw std::invalid_argument("invalid workspace path");
            }
            const auto input = utf8_path(text);
            const auto absolute =
                std::filesystem::weakly_canonical(input.is_absolute() ? input : root / input);
            const auto relative = absolute.lexically_relative(root);
            if (relative.empty() || relative.is_absolute()) {
                throw std::invalid_argument("path is outside workspace root");
            }
            for (const auto &component : relative) {
                if (component == "..") {
                    throw std::invalid_argument("path escapes workspace root");
                }
            }
            if (relative.extension() != ".mplx") {
                throw std::invalid_argument("workspace sources must have the .mplx extension");
            }
            return path_key(utf8(relative));
        }

        std::optional<std::string> read(const std::string &path) const {
            if (provider) {
                return provider(path);
            }
            std::ifstream input(root / utf8_path(path), std::ios::binary | std::ios::ate);
            if (!input) {
                return std::nullopt;
            }
            const auto size = input.tellg();
            if (size < 0 || size > static_cast<std::streamoff>(max_source)) {
                throw std::runtime_error("source exceeds 1 MiB limit");
            }
            std::string source(static_cast<size_t>(size), '\0');
            input.seekg(0);
            if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size()))) {
                throw std::runtime_error("cannot read workspace source");
            }
            return source;
        }

        std::shared_ptr<const Parsed> parse(const std::string &path, const std::string &source,
                                            BuildStatistics &statistics) {
            auto &node = cache[path];
            node.used = ++clock;
            if (node.parsed && node.parsed->source == source) {
                ++statistics.syntax_hits;
                return node.parsed;
            }
            auto parsed = std::make_shared<Parsed>();
            parsed->source = source;
            parsed->revision = ++revision;
            Lexer lexer(source);
            Parser parser(lexer.Lex());
            parsed->module = parser.parse();
            parsed->diagnostics = parser.structuredDiagnostics();
            node.parsed = parsed;
            ++statistics.parsed;
            return parsed;
        }

        void trim(const std::set<std::string> &active) {
            size_t bytes = 0;
            for (const auto &[path, node] : cache) {
                (void)path;
                if (node.parsed) {
                    bytes += node.parsed->source.size();
                }
            }
            while (cache.size() > max_cached_files || bytes > max_cached_bytes) {
                auto oldest = cache.end();
                for (auto it = cache.begin(); it != cache.end(); ++it) {
                    if (!active.contains(it->first) &&
                        (oldest == cache.end() || it->second.used < oldest->second.used)) {
                        oldest = it;
                    }
                }
                if (oldest == cache.end()) {
                    break;
                }
                bytes -= oldest->second.parsed ? oldest->second.parsed->source.size() : 0;
                cache.erase(oldest);
            }
            // Only one assembled artifact is retained per entry; bound independent entries as well.
            while (artifacts.size() > 8) {
                artifacts.erase(artifacts.begin());
            }
        }

        WorkspaceResult build(const std::string &entry_text, std::span<const SourceOverlay> input_overlays,
                              bool optimize, bool emit_artifact) {
            WorkspaceResult result;
            std::map<std::string, std::string> overlays;
            auto error = [&](const std::string &path, const char *code, const std::string &message,
                             SourceSpan span = {}) {
                result.diagnostics.push_back(
                    {path, {code, message, span.start.line, span.start.column, span}});
            };
            try {
                result.entry = canonical(entry_text);
                if (input_overlays.size() > max_files) {
                    throw std::invalid_argument("too many source overlays");
                }
                size_t overlay_bytes = 0;
                for (const auto &overlay : input_overlays) {
                    const auto path = canonical(overlay.path);
                    if (overlay.source.size() > max_source ||
                        overlay.source.size() > max_total_source - overlay_bytes) {
                        throw std::invalid_argument("overlay source budget exceeded");
                    }
                    if (!overlays.emplace(path, overlay.source).second) {
                        throw std::invalid_argument("duplicate source overlay");
                    }
                    overlay_bytes += overlay.source.size();
                }
            } catch (const std::exception &e) {
                error(entry_text, "MPLX4001", e.what());
                return result;
            }

            std::map<std::string, std::shared_ptr<const Parsed>> units;
            std::map<std::string, unsigned> colors;
            std::map<std::string, std::string> resolved_paths;
            std::vector<std::string> stack;
            std::vector<std::string> order;
            size_t function_count = 0;

            std::function<void(const std::string &, const std::string &, SourceSpan)> visit;
            visit = [&](const std::string &path, const std::string &caller, SourceSpan import_span) {
                if (colors[path] == 2) {
                    return;
                }
                if (colors[path] == 1) {
                    std::string chain;
                    const auto begin = std::find(stack.begin(), stack.end(), path);
                    for (auto it = begin; it != stack.end(); ++it) {
                        chain += *it + " -> ";
                    }
                    error(caller, "MPLX4003", "import cycle: " + chain + path, import_span);
                    return;
                }
                if (stack.size() >= max_depth || units.size() >= max_files) {
                    error(caller, "MPLX4004", "dependency graph exceeds depth/file budget", import_span);
                    return;
                }
                std::optional<std::string> source;
                try {
                    const auto overlay = overlays.find(path);
                    source = overlay != overlays.end() ? std::optional(overlay->second) : read(path);
                    if (!source) {
                        error(caller, "MPLX4002", "module not found: " + path, import_span);
                        colors[path] = 2;
                        return;
                    }
                    if (source->size() > max_source ||
                        source->size() > max_total_source - result.statistics.source_bytes) {
                        error(caller, "MPLX4004", "workspace source budget exceeded", import_span);
                        colors[path] = 2;
                        return;
                    }
                    result.statistics.source_bytes += source->size();
                    units[path] = parse(path, *source, result.statistics);
                    if (units[path]->module.functions.size() > max_functions - function_count) {
                        error(path, "MPLX4004", "workspace exceeds 65536-function budget");
                        units.erase(path);
                        colors[path] = 2;
                        return;
                    }
                    function_count += units[path]->module.functions.size();
                } catch (const std::exception &e) {
                    error(caller, "MPLX4002", "cannot load " + path + ": " + e.what(), import_span);
                    colors[path] = 2;
                    return;
                }
                colors[path] = 1;
                stack.push_back(path);
                const auto &unit = *units.at(path);
                for (const auto &diagnostic : unit.diagnostics) {
                    result.diagnostics.push_back({path, diagnostic});
                }
                std::set<std::string> aliases;
                for (const auto &import : unit.module.imports) {
                    if (result.dependencies.size() >= max_edges) {
                        error(path, "MPLX4004", "dependency graph exceeds 4096-edge budget", import.span);
                        break;
                    }
                    if (!aliases.insert(import.alias).second ||
                        parse_type(import.alias) != ValueType::Invalid) {
                        error(path, "MPLX4005", "duplicate or reserved import namespace: " + import.alias,
                              import.alias_span);
                        continue;
                    }
                    try {
                        const auto requested = utf8_path(import.path);
                        if (import.path.empty() || requested.is_absolute() ||
                            import.path.find('\\') != std::string::npos) {
                            throw std::invalid_argument(
                                "imports require a relative path with '/' separators");
                        }
                        const auto requested_path = utf8(utf8_path(path).parent_path() / requested);
                        auto resolved = resolved_paths.find(requested_path);
                        if (resolved == resolved_paths.end()) {
                            resolved =
                                resolved_paths.emplace(requested_path, canonical(requested_path)).first;
                        }
                        const auto &target = resolved->second;
                        result.dependencies.push_back(
                            {path, target, import.alias, import.span, import.path_span, import.alias_span});
                        visit(target, path, import.path_span);
                    } catch (const std::exception &e) {
                        error(path, "MPLX4001", e.what(), import.path_span);
                    }
                }
                stack.pop_back();
                colors[path] = 2;
                order.push_back(path);
            };
            visit(result.entry, result.entry, {});
            result.statistics.files = units.size();
            std::set<std::string> active;
            for (const auto &[path, unit] : units) {
                (void)unit;
                active.insert(path);
            }

            std::map<std::string, std::vector<ImportedFunction>> imported;
            std::map<std::string, std::shared_ptr<const SemanticResult>> semantics;
            size_t binding_count = 0;
            size_t binding_bytes = 0;
            for (const auto &path : order) {
                const auto &unit = *units.at(path);
                auto &bindings = imported[path];
                for (const auto &edge : result.dependencies) {
                    if (edge.from != path || !units.contains(edge.to)) {
                        continue;
                    }
                    for (const auto &function : units.at(edge.to)->module.functions) {
                        if (function.exported) {
                            size_t bytes = edge.alias.size() + edge.to.size() + 2 * function.name.size() + 2;
                            for (const auto &parameter : function.params) {
                                bytes += parameter.name.size() + 1;
                            }
                            if (binding_count >= max_functions ||
                                bytes > max_import_metadata - binding_bytes) {
                                error(path, "MPLX4004", "expanded import interface exceeds metadata budget",
                                      edge.span);
                                break;
                            }
                            ++binding_count;
                            binding_bytes += bytes;
                            bindings.push_back(imported_function(edge.to, edge.alias, function));
                        }
                    }
                }
                auto &node = cache.at(path);
                const auto interface = interface_key(bindings);
                if (node.semantic && node.semantic_revision == unit.revision && node.interface == interface) {
                    ++result.statistics.semantic_hits;
                } else {
                    auto checked = analyze_module(unit.module, bindings);
                    node.semantic = std::make_shared<const SemanticResult>(std::move(checked));
                    node.semantic_revision = unit.revision;
                    node.interface = interface;
                    ++result.statistics.checked;
                    result.statistics.affected.push_back(path);
                }
                semantics[path] = node.semantic;
                WorkspaceFile file{path, unit.source, unit.revision, node.semantic->model, unit.diagnostics};
                // Declaration positions change independently of export signatures.
                for (auto &symbol : file.semantic.symbols) {
                    if (!symbol.imported) {
                        symbol.definition_path = path;
                        if (symbol.original_name.empty()) {
                            symbol.original_name = symbol.name;
                        }
                        continue;
                    }
                    const auto binding = std::find_if(bindings.begin(), bindings.end(),
                                                      [&](const auto &b) { return b.name == symbol.name; });
                    if (binding != bindings.end()) {
                        symbol.declaration = binding->declaration;
                    }
                }
                if (file.diagnostics.empty()) {
                    file.diagnostics = node.semantic->diagnostics;
                    for (const auto &diagnostic : file.diagnostics) {
                        result.diagnostics.push_back({path, diagnostic});
                    }
                }
                result.files.push_back(std::move(file));
            }
            std::sort(result.files.begin(), result.files.end(),
                      [](const auto &a, const auto &b) { return a.path < b.path; });
            if (!result.ok() || !emit_artifact) {
                trim(active);
                return result;
            }

            std::string artifact_key = optimize ? "optimized;" : "raw;";
            for (const auto &[path, unit] : units) {
                artifact_key +=
                    std::to_string(path.size()) + ":" + path + ":" + std::to_string(unit->revision) + ";";
            }
            auto &assembled = artifacts[result.entry];
            if (assembled.artifact && assembled.key == artifact_key) {
                result.artifact = assembled.artifact;
                result.ir = assembled.ir;
                result.optimization = assembled.optimization;
                result.optimization.reused = true;
                result.statistics.artifact_hit = true;
                result.statistics.linked_functions = result.ir.functions.size();
                trim(active);
                return result;
            }

            std::map<std::string, uint32_t> functions;
            // Put entry-file functions first; dependent names are path-qualified.
            std::vector<std::string> link_order{result.entry};
            for (const auto &[path, unit] : units) {
                (void)unit;
                if (path != result.entry) {
                    link_order.push_back(path);
                }
            }
            for (const auto &path : link_order) {
                for (const auto &function : units.at(path)->module.functions) {
                    functions.emplace(identity(path, function.name), static_cast<uint32_t>(functions.size()));
                }
            }
            for (const auto &path : link_order) {
                const auto &unit = *units.at(path);
                const auto &local = semantics.at(path)->ir;
                for (size_t i = 0; i < unit.module.functions.size(); ++i) {
                    auto function = local.functions.at(i);
                    function.name = path == result.entry ? function.name : identity(path, function.name);
                    for (auto &block : function.blocks) {
                        for (auto &instruction : block.instructions) {
                            if (instruction.op != IROp::Call) {
                                continue;
                            }
                            const auto target = instruction.index;
                            if (target < unit.module.functions.size()) {
                                instruction.index =
                                    functions.at(identity(path, unit.module.functions[target].name));
                            } else {
                                const auto &binding =
                                    imported[path].at(target - unit.module.functions.size());
                                instruction.index =
                                    functions.at(identity(binding.definition_path, binding.original_name));
                            }
                        }
                    }
                    result.ir.functions.push_back(std::move(function));
                }
            }
            if (optimize) {
                result.optimization = run_optimization_pipeline(result.ir);
            }
            for (const auto &diagnostic : verify_ir(result.ir)) {
                result.diagnostics.push_back({result.entry, diagnostic});
            }
            if (result.ok()) {
                Compiler compiler(false);
                auto compiled = compiler.compile(result.ir);
                for (const auto &message : compiled.diags) {
                    error(result.entry, "MPLX4006", message);
                }
                if (result.ok()) {
                    result.artifact = VerifiedModule::create(std::move(compiled.bc));
                    assembled = {artifact_key, result.artifact, result.ir, result.optimization};
                    result.statistics.linked_functions = result.ir.functions.size();
                }
            }
            trim(active);
            return result;
        }
    };

    WorkspaceCompiler::WorkspaceCompiler(std::filesystem::path root, SourceProvider provider)
        : impl_(std::make_unique<Impl>(std::move(root), std::move(provider))) {
    }

    WorkspaceCompiler::~WorkspaceCompiler() = default;

    WorkspaceResult WorkspaceCompiler::build(const std::string &entry,
                                             std::span<const SourceOverlay> overlays, bool optimize,
                                             bool emit_artifact) {
        return impl_->build(entry, overlays, optimize, emit_artifact);
    }

    std::string WorkspaceCompiler::canonical_path(const std::string &path) const {
        return impl_->canonical(path);
    }

    std::string workspace_statistics_json(const BuildStatistics &statistics) {
        return statistics_json(statistics);
    }
    std::string workspace_diagnostics_json(const WorkspaceResult &result) {
        std::string json = "{\"diagnostics\":[";
        for (size_t i = 0; i < result.diagnostics.size(); ++i) {
            if (i) {
                json += ',';
            }
            const auto &value = result.diagnostics[i];
            const auto &diagnostic = value.diagnostic;
            json += "{\"path\":" + json_quote(value.path) + ",\"code\":" + json_quote(diagnostic.code) +
                    ",\"message\":" + json_quote(diagnostic.message) +
                    ",\"line\":" + std::to_string(diagnostic.line) +
                    ",\"col\":" + std::to_string(diagnostic.col) +
                    ",\"range\":" + span_json(diagnostic.span) + "}";
        }
        return json + "]}";
    }

    std::string workspace_json(const WorkspaceResult &result) {
        auto json = workspace_diagnostics_json(result);
        json.pop_back();
        json += ",\"schema_version\":2,\"entry\":" + json_quote(result.entry) +
                ",\"statistics\":" + statistics_json(result.statistics) + ",\"files\":[";
        for (size_t i = 0; i < result.files.size(); ++i) {
            if (i) {
                json += ',';
            }
            const auto &file = result.files[i];
            auto analysis = diagnostics_json(file.diagnostics);
            analysis.pop_back();
            analysis += ",\"schema_version\":1,\"semantic\":" + semantic_json(file.semantic) + "}";
            json += "{\"path\":" + json_quote(file.path) + ",\"source\":" + json_quote(file.source) +
                    ",\"revision\":" + std::to_string(file.revision) + ",\"analysis\":" + analysis + "}";
        }
        json += "],\"dependencies\":[";
        for (size_t i = 0; i < result.dependencies.size(); ++i) {
            if (i) {
                json += ',';
            }
            const auto &edge = result.dependencies[i];
            json += "{\"from\":" + json_quote(edge.from) + ",\"to\":" + json_quote(edge.to) +
                    ",\"alias\":" + json_quote(edge.alias) + ",\"range\":" + span_json(edge.span) +
                    ",\"path_range\":" + span_json(edge.path_span) +
                    ",\"alias_range\":" + span_json(edge.alias_span) + "}";
        }
        return json + "]}";
    }
} // namespace mplx
