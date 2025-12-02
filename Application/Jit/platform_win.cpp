#include "platform.hpp"
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
namespace mplx::jit::plat {
    ExecMem alloc_writable(size_t size) {
        auto *ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        return {ptr, ptr ? size : 0};
    }
    bool seal_executable(const ExecMem &memory) {
        DWORD previous = 0;
        if (!memory.ptr || !VirtualProtect(memory.ptr, memory.size, PAGE_EXECUTE_READ, &previous))
            return false;
        return FlushInstructionCache(GetCurrentProcess(), memory.ptr, memory.size) != 0;
    }
    void free_executable(ExecMem &memory) {
        if (memory.ptr)
            VirtualFree(memory.ptr, 0, MEM_RELEASE);
        memory = {};
    }
} // namespace mplx::jit::plat
#endif
