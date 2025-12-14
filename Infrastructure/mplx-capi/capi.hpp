#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#if defined(MPLX_CAPI_BUILD)
#define MPLX_API __declspec(dllexport)
#else
#define MPLX_API __declspec(dllimport)
#endif
#else
#define MPLX_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum { MPLX_ABI_VERSION = 1 };
typedef struct mplx_runtime_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t max_instructions;
    uint64_t max_frames;
    uint64_t max_values;
} mplx_runtime_options;
// Return codes: 0 success, 1 invalid arguments, 2 allocation failure,
// 3 runtime failure, 4 source diagnostics. Strings belong to caller via mplx_free.
MPLX_API void mplx_free(char *ptr);
MPLX_API int mplx_check_source(const char *source_utf8, char **out_json, char **out_error);
MPLX_API int mplx_run_from_source(const char *source_utf8, const char *entry_utf8, long long *out_result,
                                  char **out_error);
MPLX_API int mplx_run_from_source_ex(const char *source_utf8, const char *entry_utf8,
                                     const mplx_runtime_options *options, long long *out_result,
                                     char **out_error);
// Additive embedding API. A program is immutable and shareable; each instance
// owns execution state. Instances keep their program alive after handle release.
typedef struct mplx_program mplx_program;
typedef struct mplx_workspace mplx_workspace;
typedef struct mplx_instance mplx_instance;
enum {
    MPLX_OK = 0,
    MPLX_INVALID_ARGUMENT = 1,
    MPLX_ALLOCATION_FAILURE = 2,
    MPLX_RUNTIME_FAILURE = 3,
    MPLX_SOURCE_DIAGNOSTICS = 4,
    MPLX_INVALID_MODULE = 5,
    MPLX_BUSY = 6,
    MPLX_CANCELLED = 7
};
enum { MPLX_JIT_OFF = 0, MPLX_JIT_ON = 1, MPLX_JIT_AUTO = 2, MPLX_INSTANCE_CANCELLABLE = 1 };
typedef struct mplx_instance_options {
    uint32_t struct_size, abi_version, jit_mode, flags;
} mplx_instance_options;
typedef struct mplx_execution_stats {
    uint32_t struct_size, abi_version;
    uint64_t instructions, peak_operands, peak_locals, peak_frames;
    uint64_t jit_compilations, jit_executions, jit_fallbacks, native_calls;
} mplx_execution_stats;
MPLX_API int mplx_program_compile(const uint8_t *source, size_t size, uint32_t optimize,
                                  mplx_program **out_program, char **out_error);
MPLX_API int mplx_program_compile_file(const char *root_utf8, const char *entry_utf8, uint32_t optimize,
                                       mplx_program **out_program, char **out_error);
MPLX_API int mplx_program_load(const uint8_t *bytes, size_t size, mplx_program **out_program,
                               char **out_error);
MPLX_API int mplx_program_save(const mplx_program *program, uint8_t **out_bytes, size_t *out_size,
                               char **out_error);
MPLX_API int mplx_program_info(const mplx_program *program, char **out_json, char **out_error);
MPLX_API void mplx_program_destroy(mplx_program *program);
typedef struct mplx_source_overlay {
    const char *path_utf8;
    const uint8_t *source_utf8;
    size_t source_size;
} mplx_source_overlay;
MPLX_API int mplx_workspace_create(const char *root_utf8, mplx_workspace **out_workspace, char **out_error);
MPLX_API void mplx_workspace_destroy(mplx_workspace *workspace);
// Compilation is serialized per workspace; concurrent calls return MPLX_BUSY.
// Result programs retain their lifetime independently of the workspace cache.
MPLX_API int mplx_workspace_compile(mplx_workspace *workspace, const char *entry_utf8,
                                    const mplx_source_overlay *overlays, size_t overlay_count,
                                    uint32_t optimize, mplx_program **out_program, char **out_statistics_json,
                                    char **out_error);
MPLX_API void mplx_buffer_free(void *bytes);
MPLX_API int mplx_instance_create(const mplx_program *program, const mplx_instance_options *options,
                                  mplx_instance **out_instance, char **out_error);
MPLX_API void mplx_instance_destroy(mplx_instance *instance);
// Concurrent invokes on one instance return MPLX_BUSY. Cancellation/reset are
// nonblocking and may run from another thread; destroy needs external lifetime
// synchronization. Cancellation is latched until reset and forces interpretation.
MPLX_API int mplx_instance_cancel(mplx_instance *instance);
MPLX_API uint32_t mplx_instance_is_running(const mplx_instance *instance);
MPLX_API int mplx_instance_reset_cancel(mplx_instance *instance);
MPLX_API int mplx_instance_invoke(mplx_instance *instance, const char *entry, const int64_t *arguments,
                                  size_t count, const mplx_runtime_options *limits, int64_t *out_result,
                                  mplx_execution_stats *out_stats, char **out_error);
#ifdef __cplusplus
}
#endif
