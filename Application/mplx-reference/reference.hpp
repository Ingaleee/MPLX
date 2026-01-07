#pragma once

#include "source.hpp"
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace mplx::reference {
    enum class ErrorKind {
        InvalidSource,
        InvalidEntry,
        Arithmetic,
        StepLimit,
        FrameLimit,
        ValueLimit,
        ContinuationLimit
    };
    class Error : public std::runtime_error {
      public:
        Error(ErrorKind kind, std::string message, std::string function, SourceSpan span);
        ErrorKind kind() const {
            return kind_;
        }
        const std::string &function() const {
            return function_;
        }
        SourceSpan span() const {
            return span_;
        }

      private:
        ErrorKind kind_;
        std::string function_;
        SourceSpan span_;
    };
    struct Limits {
        uint64_t steps{1'000'000};
        size_t frames{1024}, values{1'000'000};
        size_t continuations{1'000'000};
    };
    // Execution oracle for semantically valid source. It shares syntax structures
    // with the frontend, but no resolver, IR, lowerer, bytecode or arithmetic helpers.
    class Program {
      public:
        explicit Program(std::string source);
        Program(std::map<std::string, std::string> sources, std::string entry_file);
        ~Program();
        Program(const Program &) = delete;
        Program &operator=(const Program &) = delete;
        long long invoke(const std::string &entry = "main", std::span<const long long> arguments = {},
                         Limits limits = {});

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace mplx::reference
