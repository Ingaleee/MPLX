#include "capi.hpp"
#include "frontend.hpp"
#include "module_format.hpp"
#include "vm.hpp"
#include <array>
#include <barrier>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
namespace {
    size_t checks = 0;
    void expect(bool value, const std::string &message) {
        ++checks;
        if (!value)
            throw std::runtime_error(message);
    }
    const std::string source = "fn sum(a:i32,b:bool)->i64{return i64(a)+i64(b);}"
                               "fn fib(n:i64)->i64{if(n<=1){return n;}return fib(n-1)+fib(n-2);}"
                               "fn bad()->i64{\nreturn 1/0;\n}"
                               "fn spin()->i64{while(true){}return 0;}"
                               "fn main()->i64{return fib(10);}";
    auto module() {
        auto a = mplx::analyze_source(source);
        expect(a.ok(), mplx::diagnostics_json(a.diagnostics));
        return mplx::VerifiedModule::create(std::move(a.bytecode));
    }
    uint64_t get(const std::vector<uint8_t> &data, size_t offset, size_t width) {
        uint64_t v = 0;
        for (size_t b = 0; b < width; ++b)
            v |= uint64_t(data[offset + b]) << (8 * b);
        return v;
    }
    void put(std::vector<uint8_t> &data, size_t offset, uint64_t value, size_t width) {
        for (size_t b = 0; b < width; ++b)
            data[offset + b] = static_cast<uint8_t>(value >> (8 * b));
    }
    void checksum(std::vector<uint8_t> &data) {
        put(data, 48, mplx::crc32c(std::span(data).subspan(64)), 4);
        put(data, 52, 0, 4);
        put(data, 52, mplx::crc32c(std::span(data).first(64)), 4);
    }
    void rejected(const std::vector<uint8_t> &bytes, mplx::ModuleError code) {
        try {
            (void)mplx::deserialize_module(bytes);
            expect(false, "corrupt module accepted");
        } catch (const mplx::ModuleFormatError &e) {
            expect(e.code() == code, "wrong module error: " + std::string(e.what()));
        }
    }
    void format(const char *fixture) {
        std::ifstream input(fixture, std::ios::binary);
        expect(bool(input), "missing independent module fixture");
        const std::vector<uint8_t> golden{std::istreambuf_iterator<char>(input), {}};
        auto independent = mplx::deserialize_module(golden);
        expect(mplx::VM(mplx::CompiledProgram::create(independent)).run() == 42,
               "independent fixture execution");
        expect(independent->bytecode().consts.at(1) == INT64_MIN, "little-endian signed constant");
        mplx::Bytecode expected;
        expected.functions = {{"main", 0, 0, 0}};
        expected.consts = {42, INT64_MIN};
        expected.code = {mplx::OP_PUSH_CONST, 0, 0, 0, 0, mplx::OP_RET, mplx::OP_HALT};
        expect(mplx::serialize_module(*mplx::VerifiedModule::create(std::move(expected))) == golden,
               "independent encoder agrees byte for byte");
        std::array<uint8_t, 32> zero{};
        expect(mplx::crc32c(zero) == 0x8a9136aa, "RFC 3720 CRC32C zero vector");
        zero.fill(255);
        expect(mplx::crc32c(zero) == 0x62a8ab43, "RFC 3720 CRC32C ones vector");
        std::iota(zero.begin(), zero.end(), uint8_t{0});
        expect(mplx::crc32c(zero) == 0x46dd794e, "RFC incrementing vector");
        auto original = module();
        const auto bytes = mplx::serialize_module(*original);
        auto loaded = mplx::deserialize_module(bytes);
        expect(mplx::serialize_module(*loaded) == bytes, "canonical round trip");
        expect(mplx::module_info_json(*loaded) == mplx::module_info_json(*original),
               "export metadata round trip");
        auto p = mplx::CompiledProgram::create(loaded);
        mplx::VM vm(p);
        vm.setJitMode(mplx::VM::JitMode::On);
        expect(vm.run() == 55, "module execution");
        const std::array<long long, 2> arguments = {41, 1};
        expect(vm.run("sum", arguments) == 42, "typed module invocation");
        try {
            (void)vm.run("bad");
            expect(false, "expected source trap");
        } catch (const mplx::RuntimeError &e) {
            expect(e.span().start.line == 2 && e.function() == "bad", "source map round trip");
        }
        auto copy = bytes;
        copy[0] ^= 1;
        rejected(copy, mplx::ModuleError::Header);
        copy = bytes;
        copy[8] = 2;
        rejected(copy, mplx::ModuleError::Version);
        copy = bytes;
        copy[16] = 3;
        rejected(copy, mplx::ModuleError::Features);
        copy = bytes;
        copy.back() ^= 1;
        rejected(copy, mplx::ModuleError::Checksum);
        copy = bytes;
        put(copy, 24, UINT64_MAX, 8);
        rejected(copy, mplx::ModuleError::Size);
        copy = bytes;
        put(copy, 64 + 8, UINT64_MAX, 8);
        checksum(copy);
        rejected(copy, mplx::ModuleError::Directory);
        copy = bytes;
        put(copy, 64 + 24, UINT64_MAX, 8);
        checksum(copy);
        rejected(copy, mplx::ModuleError::Directory);
        copy = bytes;
        auto functions = static_cast<size_t>(get(copy, 96 + 8, 8));
        copy[functions + 19] = 255;
        checksum(copy);
        rejected(copy, mplx::ModuleError::Metadata);
        copy = bytes;
        auto code = static_cast<size_t>(get(copy, 160 + 8, 8));
        copy[code] = 255;
        checksum(copy);
        rejected(copy, mplx::ModuleError::Bytecode);
        copy = bytes;
        auto maps = static_cast<size_t>(get(copy, 192 + 8, 8));
        put(copy, maps, UINT32_MAX, 4);
        checksum(copy);
        rejected(copy, mplx::ModuleError::Bytecode);
        for (size_t n = 0; n < bytes.size(); ++n) {
            std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + n);
            try {
                (void)mplx::deserialize_module(truncated);
                expect(false, "truncated module accepted");
            } catch (const mplx::ModuleFormatError &) {
                ++checks;
            }
        }
        std::mt19937 rng(0x4d504c58);
        for (int n = 0; n < 2000; ++n) {
            copy = bytes;
            copy[rng() % copy.size()] ^= static_cast<uint8_t>(1 + rng() % 255);
            try {
                (void)mplx::deserialize_module(copy);
                expect(false, "mutation accepted without repaired checksum");
            } catch (const mplx::ModuleFormatError &) {
                ++checks;
            }
        }
    }
    void shared() {
        auto p = mplx::CompiledProgram::create(module());
        std::barrier start(8);
        std::array<std::thread, 8> workers;
        std::array<uint64_t, 8> compilations{};
        std::array<bool, 8> okay{};
        for (size_t n = 0; n < workers.size(); ++n)
            workers[n] = std::thread([&, n] {
                try {
                    mplx::VM vm(p);
                    vm.setJitMode(mplx::VM::JitMode::On);
                    start.arrive_and_wait();
                    okay[n] = true;
                    for (int i = 0; i < 100; ++i) {
                        std::array<long long, 2> args = {i, 1};
                        okay[n] &= vm.run("sum", args) == i + 1;
                        compilations[n] += vm.stats().jit_compilations;
                    }
                } catch (...) {
                    okay[n] = false;
                }
            });
        for (auto &worker : workers)
            worker.join();
        for (bool result : okay)
            expect(result, "parallel independent VM execution");
#if defined(MPLX_WITH_JIT) && (defined(_M_X64) || defined(__x86_64__))
        expect(std::accumulate(compilations.begin(), compilations.end(), uint64_t{0}) == 1,
               "shared native image compiled more than once");
#endif
        mplx::VM vm(p);
        vm.setJitMode(mplx::VM::JitMode::On);
        for (auto args : {std::array<long long, 2>{2147483648LL, 0}, std::array<long long, 2>{1, 2}})
            try {
                (void)vm.run("sum", args);
                expect(false, "invalid typed argument accepted");
            } catch (const mplx::RuntimeError &e) {
                expect(e.code() == mplx::RuntimeErrorCode::InvalidEntry, "typed boundary error");
            }
        std::array<long long, 2> args = {41, 1};
        expect(vm.run("sum", args) == 42, "VM recovery after invalid arguments");
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            mplx::VM limited(p);
            limited.setJitMode(mode);
            limited.setLimits({1000, 16, 1});
            try {
                (void)limited.run("sum", args);
                expect(false, "host arguments escaped live-value limit");
            } catch (const mplx::RuntimeError &e) {
                expect(e.code() == mplx::RuntimeErrorCode::ValueLimit && limited.stats().instructions == 0,
                       "argument budget before entry");
            }
            mplx::Bytecode bc;
            bc.functions = {{"main", 0, 0, 0}};
            bc.functions[0].return_type = mplx::ValueType::Bool;
            bc.consts = {2};
            bc.code = {mplx::OP_PUSH_CONST, 0, 0, 0, 0, mplx::OP_RET, mplx::OP_HALT};
            mplx::VM invalid(std::move(bc));
            invalid.setJitMode(mode);
            try {
                (void)invalid.run();
                expect(false, "invalid bool export result accepted");
            } catch (const mplx::RuntimeError &e) {
                expect(e.code() == mplx::RuntimeErrorCode::InvalidState, "return type boundary");
            }
        }
        for (auto mode : {mplx::VM::JitMode::Off, mplx::VM::JitMode::On}) {
            mplx::VM a(p), b(p);
            a.setJitMode(mplx::VM::JitMode::Off);
            b.setJitMode(mode);
            std::array<long long, 1> arg = {12};
            expect(a.run("fib", arg) == b.run("fib", arg), "argument native result");
            expect(a.stats().instructions == b.stats().instructions &&
                       a.stats().peak_operands == b.stats().peak_operands &&
                       a.stats().peak_locals == b.stats().peak_locals,
                   "argument stats");
        }
    }
    struct Error {
        char *value = nullptr;
        ~Error() {
            mplx_free(value);
        }
    };
    struct Program {
        mplx_program *value = nullptr;
        ~Program() {
            mplx_program_destroy(value);
        }
    };
    struct Instance {
        mplx_instance *value = nullptr;
        ~Instance() {
            mplx_instance_destroy(value);
        }
    };
    auto stats() {
        return mplx_execution_stats{sizeof(mplx_execution_stats), MPLX_ABI_VERSION, 0, 0, 0, 0, 0, 0, 0, 0};
    }
    void contexts() {
        Program p;
        {
            Error e;
            expect(mplx_program_compile(reinterpret_cast<const uint8_t *>(source.data()), source.size(), 1,
                                        &p.value, &e.value) == 0,
                   "C compile");
        }
        uint8_t *bytes = nullptr;
        size_t size = 0;
        {
            Error e;
            expect(mplx_program_save(p.value, &bytes, &size, &e.value) == 0, "C save");
        }
        Program imported;
        {
            Error e;
            expect(mplx_program_load(bytes, size, &imported.value, &e.value) == 0, "C load");
        }
        mplx_buffer_free(bytes);
        Instance a, b;
        {
            Error e;
            expect(mplx_instance_create(imported.value, nullptr, &a.value, &e.value) == 0, "instance A");
        }
        {
            Error e;
            expect(mplx_instance_create(imported.value, nullptr, &b.value, &e.value) == 0, "instance B");
        }
        mplx_program_destroy(imported.value);
        imported.value = nullptr;
        int64_t answer = 0;
        std::array<int64_t, 2> args = {40, 1};
        {
            Error e;
            auto s = stats();
            expect(mplx_instance_invoke(a.value, "sum", args.data(), args.size(), nullptr, &answer, &s,
                                        &e.value) == 0 &&
                       answer == 41,
                   "retained program lifetime");
        }
        {
            Error e;
            auto s = stats();
            args[0] = 41;
            expect(mplx_instance_invoke(b.value, "sum", args.data(), 2, nullptr, &answer, &s, &e.value) ==
                           0 &&
                       answer == 42,
                   "independent instance");
        }
        {
            Error e;
            auto s = stats();
            expect(mplx_instance_invoke(a.value, "bad", nullptr, 0, nullptr, &answer, &s, &e.value) == 3 &&
                       answer == 0 && s.instructions > 0,
                   "failure stats");
            expect(e.value && std::string(e.value).find("\"range\"") != std::string::npos,
                   "structured runtime error");
        }
        expect(mplx_instance_cancel(a.value) == 0, "cancel latch");
        {
            Error e;
            auto s = stats();
            expect(mplx_instance_invoke(a.value, "main", nullptr, 0, nullptr, &answer, &s, &e.value) ==
                           MPLX_CANCELLED &&
                       s.instructions == 0,
                   "precancel");
        }
        expect(mplx_instance_reset_cancel(a.value) == 0, "reset");
        {
            Error e;
            auto s = stats();
            expect(mplx_instance_invoke(a.value, "main", nullptr, 0, nullptr, &answer, &s, &e.value) == 0 &&
                       answer == 55,
                   "recovery");
        }
        mplx_runtime_options long_run{sizeof(mplx_runtime_options), 1, UINT64_MAX, 1024, 1000000};
        auto work = std::async(std::launch::async, [&] {
            Error e;
            auto s = stats();
            int64_t result = 0;
            return mplx_instance_invoke(a.value, "spin", nullptr, 0, &long_run, &result, &s, &e.value);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool busy = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!mplx_instance_is_running(a.value)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            Error e;
            auto s = stats();
            if (mplx_instance_invoke(a.value, "sum", args.data(), 2, nullptr, &answer, &s, &e.value) ==
                MPLX_BUSY) {
                busy = true;
                break;
            }
            std::this_thread::yield();
        }
        // Always cancel/join before assertions so a failed check cannot hang destruction.
        auto reset_status = mplx_instance_reset_cancel(a.value);
        mplx_instance_cancel(a.value);
        auto status = work.get();
        expect(busy && reset_status == MPLX_BUSY, "concurrent invocation/reset rejected");
        expect(status == MPLX_CANCELLED, "cross-thread cancellation");
        expect(mplx_instance_reset_cancel(a.value) == 0, "post-cancel reset");
        {
            Error e;
            auto s = stats();
            s.struct_size = 1;
            expect(mplx_instance_invoke(a.value, "main", nullptr, 0, nullptr, &answer, &s, &e.value) ==
                       MPLX_INVALID_ARGUMENT,
                   "stats ABI validation");
        }
        mplx_instance_options options{sizeof(mplx_instance_options), 1, MPLX_JIT_ON, 0};
        Instance native;
        {
            Error e;
            expect(mplx_instance_create(p.value, &options, &native.value, &e.value) == 0, "native instance");
        }
        expect(mplx_instance_cancel(native.value) == MPLX_INVALID_ARGUMENT, "uncancellable native instance");
        {
            Error e;
            auto s = stats();
            expect(mplx_instance_invoke(native.value, "sum", args.data(), 2, nullptr, &answer, &s,
                                        &e.value) == 0 &&
                       answer == 42,
                   "native C args");
        }
    }
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2 || argc > 3)
            return 2;
        std::string suite = argv[1];
        if (suite == "module-format" && argc == 3)
            format(argv[2]);
        else if (suite == "shared-program")
            shared();
        else if (suite == "embedding-contexts")
            contexts();
        else
            return 2;
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
