#include "platform.hpp"
#if MPLX_WIN
#include <windows.h>
#include <cstring>

namespace mplx::jit::plat {

ExecMem alloc_executable(size_t size){
  ExecMem m;
  void* p = ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (p){ m.ptr = p; m.size = size; }
  return m;
}

void free_executable(ExecMem& m){
  if (m.ptr){ ::VirtualFree(m.ptr, 0, MEM_RELEASE); m.ptr = nullptr; m.size = 0; }
}

void flush_icache(void* p, size_t n){
  ::FlushInstructionCache(::GetCurrentProcess(), p, n);
}

} // namespace mplx::jit::plat
#endif


