#include "platform.hpp"
#if MPLX_POSIX
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace mplx::jit::plat {

  ExecMem alloc_executable(size_t size) {
    ExecMem m;
    m.size        = size;
    long pagesize = sysconf(_SC_PAGESIZE);
    size_t alloc  = ((size + pagesize - 1) / pagesize) * pagesize;
    void *p       = ::mmap(nullptr, alloc, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
      return {};
    m.ptr  = p;
    m.size = alloc;
    return m;
  }

  void free_executable(ExecMem &m) {
    if (m.ptr) {
      ::munmap(m.ptr, m.size);
      m.ptr  = nullptr;
      m.size = 0;
    }
  }

  void flush_icache(void * /*p*/, size_t /*n*/) { /* most POSIX CPUs have coherent I-cache */ }

} // namespace mplx::jit::plat
#endif
