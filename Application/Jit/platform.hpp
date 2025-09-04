#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#  define MPLX_WIN 1
#else
#  define MPLX_POSIX 1
#endif

namespace mplx::jit::plat {

struct ExecMem {
  void* ptr{nullptr};
  size_t size{0};
};

ExecMem alloc_executable(size_t size);
void free_executable(ExecMem&);
void flush_icache(void* p, size_t n);

}


