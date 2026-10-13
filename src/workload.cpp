#include "exchange/workload.hpp"
#include <random>
#include <stdexcept>

namespace exchange {
WorkloadProfile parse_profile(std::string_view name) {
    if (name == "mixed") return WorkloadProfile::Mixed;
    if (name == "deep") return WorkloadProfile::Deep;
    if (name == "cancel-heavy") return WorkloadProfile::CancelHeavy;
    if (name == "sweep") return WorkloadProfile::Sweep;
    if (name == "id-churn") return WorkloadProfile::IdChurn;
    throw std::invalid_argument("profile: mixed, deep, cancel-heavy, sweep, id-churn");
}
std::string_view profile_name(WorkloadProfile profile) {
    switch (profile) {
    case WorkloadProfile::Mixed: return "mixed";
    case WorkloadProfile::Deep: return "deep";
    case WorkloadProfile::CancelHeavy: return "cancel-heavy";
    case WorkloadProfile::Sweep: return "sweep";
    case WorkloadProfile::IdChurn: return "id-churn";
    }
    throw std::invalid_argument("invalid workload profile");
}
std::vector<Command> synthetic_workload(std::size_t count, std::uint64_t seed) {
    if (count > 10000000) throw std::invalid_argument("workload limit is 10 million commands");
    std::mt19937_64 random(seed);
    std::vector<Command> commands;
    commands.reserve(count);
    OrderId next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto action = random() % 100;
        if (action < 20 && next_id > 1) {
            commands.push_back({CommandKind::Cancel, 1+random()%(next_id-1), Side::Buy, 0, 0});
        } else {
            const auto side = random()%2 == 0 ? Side::Buy : Side::Sell;
            const auto price = static_cast<Value>(9900 + random()%201);
            const auto quantity = static_cast<Value>(1 + random()%100);
            commands.push_back({CommandKind::Add, next_id++, side, price, quantity});
        }
    }
    return commands;
}

Workload benchmark_workload(const WorkloadOptions& options) {
    if (options.count == 0 || options.count > 10000000)
        throw std::invalid_argument("orders: 1..10000000");
    if (options.levels == 0 || options.levels > 1000000 || options.depth == 0 ||
        options.depth > 1000000 || options.levels > 500000/options.depth)
        throw std::invalid_argument("levels/depth: positive; levels*depth <= 500000");
    (void)profile_name(options.profile); // validate before allocating
    if (options.profile == WorkloadProfile::Mixed)
        return {{}, synthetic_workload(options.count, options.seed)};
    Workload workload;
    workload.commands.reserve(options.count);
    OrderId next_id = 1;
    if (options.profile == WorkloadProfile::IdChurn) {
        while (workload.commands.size() < options.count) {
            const auto id = next_id++;
            workload.commands.push_back({CommandKind::Add, id, Side::Buy, 100, 1});
            if (workload.commands.size() < options.count)
                workload.commands.push_back({CommandKind::Cancel, id, Side::Buy, 0, 0});
        }
        return workload;
    }
    std::mt19937_64 random(options.seed);
    if (options.profile == WorkloadProfile::Sweep) {
        bool buys = false;
        while (workload.commands.size() < options.count) {
            const auto side = buys ? Side::Buy : Side::Sell;
            for (std::size_t level = 0; level < options.levels; ++level) {
                for (std::size_t order = 0; order < options.depth; ++order) {
                    if (workload.commands.size() == options.count) return workload;
                    const auto offset = static_cast<Value>(level);
                    const auto price = buys ? 1000000-offset : 1000001+offset;
                    workload.commands.push_back({CommandKind::Add, next_id++, side, price, 1});
                }
            }
            if (workload.commands.size() < options.count)
                workload.commands.push_back({CommandKind::Add, next_id++, buys ? Side::Sell : Side::Buy,
                    buys ? 1 : 2000000, static_cast<Value>(options.levels*options.depth)});
            buys = !buys;
        }
        return workload;
    }
    std::vector<OrderId> active;
    const auto initial = 2*options.levels*options.depth;
    workload.setup.reserve(initial);
    active.reserve(initial+options.count);
    for (const auto side : {Side::Buy, Side::Sell}) {
        for (std::size_t level = 0; level < options.levels; ++level) {
            for (std::size_t order = 0; order < options.depth; ++order) {
                const auto offset = static_cast<Value>(level);
                workload.setup.push_back({CommandKind::Add, next_id++, side,
                    side == Side::Buy ? 1000000-offset : 1000001+offset, 1});
                active.push_back(workload.setup.back().id);
            }
        }
    }
    const std::uint64_t cancel_percent = options.profile == WorkloadProfile::CancelHeavy ? 50 : 20;
    for (std::size_t i = 0; i < options.count; ++i) {
        if (random()%100 < cancel_percent && !active.empty()) {
            const auto index = static_cast<std::size_t>(random()%active.size());
            workload.commands.push_back({CommandKind::Cancel, active[index], Side::Buy, 0, 0});
            active[index] = active.back();
            active.pop_back();
        } else {
            const auto side = random()%2 == 0 ? Side::Buy : Side::Sell;
            const auto offset = static_cast<Value>(random()%options.levels);
            workload.commands.push_back({CommandKind::Add, next_id++, side,
                side == Side::Buy ? 1000000-offset : 1000001+offset, 1});
            active.push_back(workload.commands.back().id);
        }
    }
    return workload;
}
}
