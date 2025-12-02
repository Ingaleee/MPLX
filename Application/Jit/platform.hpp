#pragma once
#include <cstddef>
#include <utility>
#if defined(_WIN32)
#define MPLX_WIN 1
#else
#define MPLX_POSIX 1
#endif
namespace mplx::jit::plat {
    struct ExecMem {
        void *ptr{nullptr};
        size_t size{0};
    };
    ExecMem alloc_writable(size_t size);
    bool seal_executable(const ExecMem &memory);
    void free_executable(ExecMem &memory);
    class ExecutableMemory {
      public:
        ExecutableMemory() = default;
        explicit ExecutableMemory(ExecMem memory) : memory_(memory) {
        }
        ~ExecutableMemory() {
            free_executable(memory_);
        }
        ExecutableMemory(const ExecutableMemory &) = delete;
        ExecutableMemory &operator=(const ExecutableMemory &) = delete;
        ExecutableMemory(ExecutableMemory &&other) noexcept : memory_(std::exchange(other.memory_, {})) {
        }
        ExecutableMemory &operator=(ExecutableMemory &&other) noexcept {
            if (this != &other) {
                free_executable(memory_);
                memory_ = std::exchange(other.memory_, {});
            }
            return *this;
        }
        void *data() const {
            return memory_.ptr;
        }
        size_t size() const {
            return memory_.size;
        }
        bool seal() const {
            return seal_executable(memory_);
        }

      private:
        ExecMem memory_;
    };
} // namespace mplx::jit::plat
