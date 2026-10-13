#include "exchange/workload.hpp"
#include "process_memory.hpp"
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
    std::uint64_t accepted{}, rejected{}, trades{}, volume{}, adds{}, cancels{}, iocs{}, markets{}, amendments{}, expired{};
    bool operator==(const Stats&) const = default;
};
void account(Stats& stats, const Command& command, const Result& result) {
    switch (command.kind) {
    case CommandKind::Add: ++stats.adds; break;
    case CommandKind::Cancel: ++stats.cancels; break;
    case CommandKind::Ioc: ++stats.iocs; break;
    case CommandKind::Market: ++stats.markets; break;
    case CommandKind::Amend: ++stats.amendments; break;
    default: throw std::runtime_error("unexpected benchmark command");
    }
    stats.expired += static_cast<std::uint64_t>(result.expired_quantity);
    if (result.status == Status::Accepted) ++stats.accepted; else ++stats.rejected;
    stats.trades += result.trades.size();
    for (const auto& t : result.trades) stats.volume += static_cast<std::uint64_t>(t.quantity);
}
void setup(Engine& engine, const Workload& workload) {
    for (const auto& command : workload.setup) {
        if (execute(engine, command).status != Status::Accepted)
            throw std::runtime_error("setup command rejected");
    }
}
void print_stats(const Stats& stats) {
    std::cout << " accepted=" << stats.accepted << " rejected=" << stats.rejected
              << " adds=" << stats.adds << " cancels=" << stats.cancels
              << " iocs=" << stats.iocs << " markets=" << stats.markets
              << " amendments=" << stats.amendments << " expired=" << stats.expired
              << " trades=" << stats.trades << " volume=" << stats.volume;
}
void print_metrics(const Engine& engine) {
    const auto metrics = engine.metrics();
    std::cout << " active_orders=" << metrics.active_orders << " bid_levels=" << metrics.bid_levels
              << " ask_levels=" << metrics.ask_levels << " reserved_ids=" << metrics.reserved_ids;
}
// Defined integer encoding: stable across compilers, endianness and size_t width.
void hash_word(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= value & 255;
        hash *= 1099511628211ULL;
        value >>= 8;
    }
}
void hash_result(std::uint64_t& hash, const Result& result) {
    hash_word(hash, result.sequence);
    hash_word(hash, static_cast<std::uint64_t>(result.status));
    hash_word(hash, result.trades.size());
    for (const auto& trade : result.trades) {
        hash_word(hash, trade.sequence);
        hash_word(hash, trade.maker);
        hash_word(hash, trade.taker);
        hash_word(hash, static_cast<std::uint64_t>(trade.price));
        hash_word(hash, static_cast<std::uint64_t>(trade.quantity));
    }
    // Keep historical GTC fingerprints stable; immediate expiry adds a tagged field.
    if (result.expired_quantity != 0) {
        hash_word(hash, 0x45585049524544ULL);
        hash_word(hash, static_cast<std::uint64_t>(result.expired_quantity));
    }
}
void hash_book(std::uint64_t& hash, const std::vector<Order>& orders) {
    hash_word(hash, orders.size());
    for (const auto& order : orders) {
        hash_word(hash, order.id);
        hash_word(hash, static_cast<std::uint64_t>(order.side));
        hash_word(hash, static_cast<std::uint64_t>(order.price));
        hash_word(hash, static_cast<std::uint64_t>(order.quantity));
        hash_word(hash, order.sequence);
    }
}
std::uint64_t fingerprint(const Workload& workload) {
    std::uint64_t hash = 14695981039346656037ULL;
    hash_word(hash, workload.setup.size());
    for (const auto* commands : {&workload.setup, &workload.commands}) {
        hash_word(hash, commands->size());
        for (const auto& c : *commands) {
            hash_word(hash, static_cast<std::uint64_t>(c.kind));
            hash_word(hash, c.id);
            hash_word(hash, static_cast<std::uint64_t>(c.side));
            hash_word(hash, static_cast<std::uint64_t>(c.price));
            hash_word(hash, static_cast<std::uint64_t>(c.quantity));
        }
    }
    return hash;
}
void memory_checkpoint(std::string_view phase, const Engine* engine = nullptr) {
    const auto memory = process_memory();
    std::cout << "memory phase=" << phase << " available=" << memory.available;
    if (memory.available)
        std::cout << " resident_bytes=" << memory.resident_bytes
                  << " process_peak_resident_bytes=" << memory.peak_resident_bytes;
    if (engine) print_metrics(*engine);
    std::cout << '\n';
}
}
int main(int argc, char** argv) {
    try {
        WorkloadOptions options;
        std::uint64_t repetitions = 5;
        bool latency = false, memory_only = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--latency") { latency = true; continue; }
            if (arg == "--memory-only") { memory_only = true; continue; }
            if (arg == "--help") {
                std::cout << "exchange_bench [--orders N] [--seed N] [--samples N] [--latency]\n"
                          << "  [--profile mixed|deep|cancel-heavy|sweep|id-churn|aggressive] [--levels N] [--depth N]\n"
                          << "  --memory-only: fresh-process memory pass; no warm-up or throughput timing\n";
                return 0;
            }
            if (arg != "--orders" && arg != "--seed" && arg != "--samples" &&
                arg != "--profile" && arg != "--levels" && arg != "--depth")
                throw std::invalid_argument("unknown option; use --help");
            if (++i >= argc) throw std::invalid_argument("missing option value");
            if (arg == "--profile") { options.profile = parse_profile(argv[i]); continue; }
            const auto value = number(argv[i]);
            if (arg == "--seed") options.seed = value;
            else if (arg == "--samples") repetitions = value;
            else {
                if (value > 10000000) throw std::invalid_argument("numeric option exceeds limit");
                if (arg == "--orders") options.count = static_cast<std::size_t>(value);
                else if (arg == "--levels") options.levels = static_cast<std::size_t>(value);
                else options.depth = static_cast<std::size_t>(value);
            }
        }
        if (repetitions == 0 || repetitions > 100) throw std::invalid_argument("samples: 1..100");
        if (memory_only && latency) throw std::invalid_argument("memory-only and latency are separate modes");
        if (memory_only) memory_checkpoint("before_generation");
        const auto workload = benchmark_workload(options);
        const auto& commands = workload.commands;
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
                  << "\nprofile=" << profile_name(options.profile) << " commands=" << commands.size()
                  << " seed=" << options.seed << " setup_commands=" << workload.setup.size()
                  << " levels=" << options.levels << " depth=" << options.depth
                  << " workload_fingerprint=" << fingerprint(workload) << '\n';
        if (memory_only) {
            std::cout << "mode=memory_only warmup=none timing=none\n";
            memory_checkpoint("after_generation");
            {
                Engine engine;
                setup(engine, workload);
                memory_checkpoint("after_setup", &engine);
                Stats stats;
                std::uint64_t result_hash = 14695981039346656037ULL;
                for (const auto& c : commands) {
                    const auto result = execute(engine, c);
                    account(stats, c, result);
                    hash_result(result_hash, result);
                }
                memory_checkpoint("after_commands", &engine);
                if (!engine.valid()) throw std::runtime_error("memory pass invariant failure");
                hash_book(result_hash, engine.orders());
                std::cout << "memory_result";
                print_stats(stats);
                std::cout << " result_fingerprint=" << result_hash << '\n';
            }
            memory_checkpoint("after_engine_destruction");
            return 0;
        }
        Stats expected_stats;
        std::vector<Order> expected_orders;
        std::uint64_t result_hash = 14695981039346656037ULL;
        {
            Engine warmup;
            setup(warmup, workload);
            for (const auto& c : commands) {
                const auto result = execute(warmup, c);
                account(expected_stats, c, result);
                hash_result(result_hash, result);
            }
            if (!warmup.valid()) throw std::runtime_error("warm-up invariant failure");
            expected_orders = warmup.orders();
            hash_book(result_hash, expected_orders);
        }
        std::cout << "samples=" << repetitions << " warmup=one_full_workload\n";
        std::vector<double> throughputs;
        Stats final_stats;
        for (std::uint64_t sample = 0; sample < repetitions; ++sample) {
            Engine engine;
            setup(engine, workload);
            Stats stats;
            const auto start = Clock::now();
            for (const auto& c : commands) account(stats, c, execute(engine, c));
            const auto end = Clock::now();
            if (!engine.valid() || stats != expected_stats || engine.orders() != expected_orders ||
                engine.sequence() != workload.setup.size()+commands.size())
                throw std::runtime_error("benchmark result mismatch");
            const double seconds = std::chrono::duration<double>(end-start).count();
            const double rate = static_cast<double>(commands.size())/seconds;
            throughputs.push_back(rate);
            std::cout << "sample=" << sample+1 << " seconds=" << seconds << " commands_per_second=" << rate << '\n';
            final_stats = stats;
            if (sample == 0) { std::cout << "final_book"; print_metrics(engine); std::cout << '\n'; }
        }
        std::sort(throughputs.begin(), throughputs.end());
        std::cout << "median_commands_per_second=" << throughputs[throughputs.size()/2]
                  << " result_fingerprint=" << result_hash;
        print_stats(final_stats);
        std::cout << '\n';
        if (latency) {
            Engine engine;
            setup(engine, workload);
            Stats latency_stats;
            std::vector<double> nanoseconds;
            nanoseconds.reserve(commands.size());
            for (const auto& c : commands) {
                const auto start = Clock::now();
                const auto result = execute(engine, c);
                const auto end = Clock::now();
                nanoseconds.push_back(std::chrono::duration<double, std::nano>(end-start).count());
                account(latency_stats, c, result);
            }
            if (latency_stats != final_stats || !engine.valid() || engine.orders() != expected_orders)
                throw std::runtime_error("latency pass mismatch");
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
