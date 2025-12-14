#include "capi.hpp"
#include <stdio.h>
#include <string.h>

// Compiled as C11, separately from the C++ tests: verify the public C ABI.
static int workspace_contract(void) {
    static const char main_source[] =
        "import \"lib.virtual.mplx\" as lib;fn main()->i64{return lib::value();}";
    static const char library_source[] = "export fn value()->i64{return 42;}";
    mplx_source_overlay overlays[] = {
        {"main.virtual.mplx", (const uint8_t *)main_source, sizeof(main_source) - 1},
        {"lib.virtual.mplx", (const uint8_t *)library_source, sizeof(library_source) - 1}};
    mplx_workspace *workspace = NULL;
    mplx_program *first = NULL, *second = NULL;
    mplx_instance *instance = NULL;
    char *summary = NULL, *error = NULL;
    int64_t answer = 0;
    int result = 1;
    if (mplx_workspace_create(".", &workspace, &error) != MPLX_OK)
        goto cleanup;
    if (mplx_workspace_compile(workspace, "main.virtual.mplx", overlays, 2, 1, &first, &summary, &error) !=
        MPLX_OK)
        goto cleanup;
    if (!summary || !strstr(summary, "\"parsed\":2"))
        goto cleanup;
    mplx_free(summary);
    summary = NULL;
    if (mplx_workspace_compile(workspace, "main.virtual.mplx", overlays, 2, 1, &second, &summary, &error) !=
        MPLX_OK)
        goto cleanup;
    if (!summary || !strstr(summary, "\"artifact_hit\":true"))
        goto cleanup;
    mplx_free(summary);
    summary = NULL;
    if (mplx_instance_create(second, NULL, &instance, &error) != MPLX_OK)
        goto cleanup;
    mplx_workspace_destroy(workspace);
    workspace = NULL;
    mplx_program_destroy(first);
    first = NULL;
    mplx_program_destroy(second);
    second = NULL;
    if (mplx_instance_invoke(instance, "main", NULL, 0, NULL, &answer, NULL, &error) != MPLX_OK ||
        answer != 42)
        goto cleanup;
    result = 0;
cleanup:
    if (result && error)
        fprintf(stderr, "%s\n", error);
    mplx_free(summary);
    mplx_free(error);
    mplx_instance_destroy(instance);
    mplx_program_destroy(first);
    mplx_program_destroy(second);
    mplx_workspace_destroy(workspace);
    return result;
}
int main(void) {
    static const char source[] = "fn sum(a:i32,b:bool)->i64{return i64(a)+i64(b);}";
    mplx_program *program = NULL, *loaded = NULL;
    mplx_instance *instance = NULL;
    uint8_t *bytes = NULL;
    size_t size = 0;
    char *error = NULL;
    int result = 1;
    int64_t answer = 0;
    const int64_t arguments[] = {41, 1};
    mplx_execution_stats stats = {0};
    stats.struct_size = sizeof(stats);
    stats.abi_version = MPLX_ABI_VERSION;
    if (mplx_program_compile((const uint8_t *)source, strlen(source), 1, &program, &error) != MPLX_OK)
        goto cleanup;
    if (mplx_program_save(program, &bytes, &size, &error) != MPLX_OK)
        goto cleanup;
    if (mplx_program_load(bytes, size, &loaded, &error) != MPLX_OK)
        goto cleanup;
    if (mplx_instance_create(loaded, NULL, &instance, &error) != MPLX_OK)
        goto cleanup;
    mplx_program_destroy(loaded);
    loaded = NULL;
    mplx_program_destroy(program);
    program = NULL;
    mplx_buffer_free(bytes);
    bytes = NULL;
    if (mplx_instance_invoke(instance, "sum", arguments, 2, NULL, &answer, &stats, &error) != MPLX_OK)
        goto cleanup;
    if (answer != 42 || stats.instructions == 0 || stats.peak_frames != 1)
        goto cleanup;
    if (mplx_instance_cancel(instance) != MPLX_OK)
        goto cleanup;
    if (mplx_instance_invoke(instance, "sum", arguments, 2, NULL, &answer, &stats, &error) != MPLX_CANCELLED)
        goto cleanup;
    mplx_free(error);
    error = NULL;
    if (mplx_instance_reset_cancel(instance) != MPLX_OK)
        goto cleanup;
    if (mplx_instance_invoke(instance, "sum", arguments, 2, NULL, &answer, &stats, &error) != MPLX_OK ||
        answer != 42)
        goto cleanup;
    puts("C11 embedding ABI passed");
    result = workspace_contract();
cleanup:
    if (result && error)
        fprintf(stderr, "%s\n", error);
    mplx_free(error);
    mplx_buffer_free(bytes);
    mplx_instance_destroy(instance);
    mplx_program_destroy(loaded);
    mplx_program_destroy(program);
    return result;
}
