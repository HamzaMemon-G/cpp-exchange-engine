#include "exchange/workload.hpp"
#include <random>
#include <stdexcept>

namespace exchange {
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
}
