#include "exchange/workload.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        std::uint64_t count = 100000, seed = 42;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if ((arg != "--orders" && arg != "--seed") || ++i >= argc)
                throw std::invalid_argument("use --orders N --seed N");
            const std::string_view text = argv[i];
            std::uint64_t value{};
            const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
            if (ec != std::errc{} || end != text.data()+text.size()) throw std::invalid_argument("invalid number");
            if (arg == "--orders") count = value; else seed = value;
        }
        if (count == 0 || count > 10000000) throw std::invalid_argument("orders: 1..10000000");
        for (const auto& c : exchange::synthetic_workload(static_cast<std::size_t>(count), seed))
            std::cout << exchange::format_command(c) << '\n';
        std::cout << "BOOK\n";
        if (!std::cout) throw std::runtime_error("output write failure");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
