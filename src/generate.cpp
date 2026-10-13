#include "exchange/workload.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        exchange::WorkloadOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "exchange_generate [--orders N] [--seed N]\n"
                          << "  [--profile mixed|deep|cancel-heavy|sweep|id-churn|aggressive] [--levels N] [--depth N]\n"
                          << "Output includes untimed benchmark setup, then N command attempts, then BOOK.\n";
                return 0;
            }
            if ((arg != "--orders" && arg != "--seed" && arg != "--profile" &&
                 arg != "--levels" && arg != "--depth") || ++i >= argc)
                throw std::invalid_argument("invalid option; use --help");
            if (arg == "--profile") { options.profile = exchange::parse_profile(argv[i]); continue; }
            const std::string_view text = argv[i];
            std::uint64_t value{};
            const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
            if (ec != std::errc{} || end != text.data()+text.size()) throw std::invalid_argument("invalid number");
            if (arg == "--seed") options.seed = value;
            else {
                if (value > 10000000) throw std::invalid_argument("numeric option exceeds limit");
                if (arg == "--orders") options.count = static_cast<std::size_t>(value);
                else if (arg == "--levels") options.levels = static_cast<std::size_t>(value);
                else options.depth = static_cast<std::size_t>(value);
            }
        }
        const auto workload = exchange::benchmark_workload(options);
        for (const auto& c : workload.setup)
            std::cout << exchange::format_command(c) << '\n';
        for (const auto& c : workload.commands)
            std::cout << exchange::format_command(c) << '\n';
        std::cout << "BOOK\n";
        if (!std::cout) throw std::runtime_error("output write failure");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
