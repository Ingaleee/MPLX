#include "platform.hpp"
#if !defined(_WIN32)
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
namespace mplx::jit::plat {
    ExecMem alloc_writable(size_t size) {
        auto page = sysconf(_SC_PAGESIZE);
        if (page <= 0 || !size || size > std::numeric_limits<size_t>::max() - static_cast<size_t>(page))
            return {};
        auto rounded = (size + page - 1) / page * page;
        auto *ptr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return ptr == MAP_FAILED ? ExecMem{} : ExecMem{ptr, rounded};
    }
    bool seal_executable(const ExecMem &memory) {
        if (!memory.ptr || mprotect(memory.ptr, memory.size, PROT_READ | PROT_EXEC) != 0)
            return false;
        __builtin___clear_cache(static_cast<char *>(memory.ptr),
                                static_cast<char *>(memory.ptr) + memory.size);
        return true;
    }
    void free_executable(ExecMem &memory) {
        if (memory.ptr)
            munmap(memory.ptr, memory.size);
        memory = {};
    }
} // namespace mplx::jit::plat
#endif
