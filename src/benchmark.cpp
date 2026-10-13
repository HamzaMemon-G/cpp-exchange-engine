#include "exchange/workload.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace exchange;
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t number(std::string_view s) {
    std::uint64_t n{};
    const auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), n);
    if (ec != std::errc{} || end != s.data()+s.size()) throw std::invalid_argument("invalid number");
    return n;
}
struct Stats {
    std::uint64_t accepted{}, rejected{}, trades{}, volume{};
    bool operator==(const Stats&) const = default;
};
void account(Stats& stats, const Result& result) {
    if (result.status == Status::Accepted) ++stats.accepted; else ++stats.rejected;
    stats.trades += result.trades.size();
    for (const auto& t : result.trades) stats.volume += static_cast<std::uint64_t>(t.quantity);
}
}
int main(int argc, char** argv) {
    try {
        std::uint64_t count = 100000, seed = 42, repetitions = 5;
        bool latency = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--latency") { latency = true; continue; }
            if (arg != "--orders" && arg != "--seed" && arg != "--samples")
                throw std::invalid_argument("use --orders N --seed N --samples N [--latency]");
            if (++i >= argc) throw std::invalid_argument("missing option value");
            const auto value = number(argv[i]);
            if (arg == "--orders") count = value;
            else if (arg == "--seed") seed = value;
            else repetitions = value;
        }
        if (count == 0 || count > 10000000 || repetitions == 0 || repetitions > 100)
            throw std::invalid_argument("orders: 1..10000000; samples: 1..100");
        const auto commands = synthetic_workload(static_cast<std::size_t>(count), seed);
        { Engine warmup; for (const auto& c : commands) execute(warmup, c); }
        std::cout << std::fixed << std::setprecision(3)
                  << "build=" << EXCHANGE_BUILD_MODE << " compiler="
#if defined(__clang__)
                  << "Clang " << __clang_version__
#elif defined(__GNUC__)
                  << "GCC " << __VERSION__
#elif defined(_MSC_VER)
                  << "MSVC " << _MSC_VER
#else
                  << "unknown"
#endif
                  << "\ncommands=" << count << " seed=" << seed << " samples=" << repetitions
                  << " warmup=one_full_workload\n";
        std::vector<double> throughputs;
        Stats final_stats;
        for (std::uint64_t sample = 0; sample < repetitions; ++sample) {
            Engine engine;
            Stats stats;
            const auto start = Clock::now();
            for (const auto& c : commands) account(stats, execute(engine, c));
            const auto end = Clock::now();
            if (!engine.valid()) throw std::runtime_error("benchmark invariant failure");
            const double seconds = std::chrono::duration<double>(end-start).count();
            const double rate = static_cast<double>(count)/seconds;
            throughputs.push_back(rate);
            std::cout << "sample=" << sample+1 << " seconds=" << seconds << " commands_per_second=" << rate << '\n';
            final_stats = stats;
        }
        std::sort(throughputs.begin(), throughputs.end());
        std::cout << "median_commands_per_second=" << throughputs[throughputs.size()/2]
                  << " accepted=" << final_stats.accepted << " rejected=" << final_stats.rejected
                  << " trades=" << final_stats.trades << " volume=" << final_stats.volume << '\n';
        if (latency) {
            Engine engine;
            Stats latency_stats;
            std::vector<double> nanoseconds;
            nanoseconds.reserve(commands.size());
            for (const auto& c : commands) {
                const auto start = Clock::now();
                const auto result = execute(engine, c);
                const auto end = Clock::now();
                nanoseconds.push_back(std::chrono::duration<double, std::nano>(end-start).count());
                account(latency_stats, result);
            }
            if (latency_stats != final_stats || !engine.valid()) throw std::runtime_error("latency pass mismatch");
            std::sort(nanoseconds.begin(), nanoseconds.end());
            auto percentile = [&](std::size_t p) { return nanoseconds[(nanoseconds.size()-1)*p/100]; };
            std::cout << "instrumented_latency_ns p50=" << percentile(50) << " p95=" << percentile(95)
                      << " p99=" << percentile(99) << " max=" << nanoseconds.back()
                      << " (includes two clock reads; separate pass)\n";
            std::vector<double> baseline;
            baseline.reserve(10000);
            for (int i = 0; i < 10000; ++i) {
                const auto start = Clock::now();
                const auto end = Clock::now();
                baseline.push_back(std::chrono::duration<double, std::nano>(end-start).count());
            }
            std::sort(baseline.begin(), baseline.end());
            std::cout << "empty_clock_pair_median_ns=" << baseline[baseline.size()/2] << '\n';
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
