#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *, size_t);
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3)
        return 2;
    auto invoke = [&](const std::vector<uint8_t> &input) {
        // Optional last-input checkpoint survives a native crash or abort. The
        // ordinary regression runner performs no extra file writes.
        if (argc == 3) {
            std::ofstream checkpoint(argv[2], std::ios::binary | std::ios::trunc);
            if (!checkpoint)
                throw std::runtime_error("cannot write seed checkpoint");
            checkpoint.write(reinterpret_cast<const char *>(input.data()),
                             static_cast<std::streamsize>(input.size()));
            checkpoint.close();
            if (!checkpoint)
                throw std::runtime_error("failed seed checkpoint write");
        }
        LLVMFuzzerTestOneInput(input.data(), input.size());
    };
    std::mt19937 random(0x4d504c58);
    size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(argv[1])) {
        if (!entry.is_regular_file())
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in)
            return 1;
        const std::vector<uint8_t> seed{std::istreambuf_iterator<char>(in), {}};
        invoke(seed);
        ++count;
        for (int i = 0; i < 200; ++i) {
            auto input = seed;
            if (i % 5 == 0)
                input.resize(random() % (seed.size() + 1));
            for (int j = 0; j < 1 + i % 3 && !input.empty(); ++j)
                input[random() % input.size()] = static_cast<uint8_t>(random());
            invoke(input);
            ++count;
        }
    }
    if (!count)
        return 1;
    std::cout << count << " deterministic seed/mutation harness executions passed\n";
    return 0;
}
