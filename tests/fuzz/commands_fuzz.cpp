#include "exchange/commands.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool condition) { if (!condition) std::abort(); }
}

// Mutate text, then execute valid commands on a fresh engine. Bound work per input;
// this is parser/state fuzzing, not a large-book performance or durability test.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 4096) return 0;
    std::string_view text{reinterpret_cast<const char*>(data), size};
    exchange::Engine engine;
    for (int count = 0; count < 64 && !text.empty(); ++count) {
        const auto newline = text.find('\n');
        const auto line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline+1);
        exchange::Command command;
        try { command = exchange::parse_command(line); }
        catch (const std::invalid_argument&) { continue; }
        if (command.kind == exchange::CommandKind::Quit) break;
        if (command.kind == exchange::CommandKind::Empty || command.kind == exchange::CommandKind::Book) {
            check(engine.valid());
            continue;
        }
        check(exchange::parse_command(exchange::format_command(command)) == command);
        const auto before = engine.orders();
        const auto sequence = engine.sequence();
        const auto result = exchange::execute(engine, command);
        check(result.sequence == sequence+1 && engine.sequence() == result.sequence);
        check(result.expired_quantity >= 0 && engine.valid());
        if (result.status != exchange::Status::Accepted) {
            check(result.trades.empty() && result.expired_quantity == 0 && engine.orders() == before);
            continue;
        }
        if (command.kind == exchange::CommandKind::Cancel) {
            check(result.trades.empty() && result.expired_quantity == 0);
            continue;
        }
        auto side = command.side;
        if (command.kind == exchange::CommandKind::Amend) {
            bool found = false;
            for (const auto& order : before) if (order.id == command.id) { side = order.side; found = true; }
            check(found);
        }
        auto remaining = command.quantity;
        for (const auto& trade : result.trades) {
            check(trade.sequence == result.sequence && trade.taker == command.id && trade.maker != trade.taker);
            check(trade.price > 0 && trade.quantity > 0 && trade.quantity <= remaining);
            check(command.kind == exchange::CommandKind::Market ||
                  (side == exchange::Side::Buy ? trade.price <= command.price : trade.price >= command.price));
            remaining -= trade.quantity; // bounded subtraction, even for maximum input quantities
        }
        if (command.kind == exchange::CommandKind::Ioc || command.kind == exchange::CommandKind::Market) {
            check(result.expired_quantity == remaining);
            for (const auto& order : engine.orders()) check(order.id != command.id);
        } else check(result.expired_quantity == 0);
    }
    return 0;
}
