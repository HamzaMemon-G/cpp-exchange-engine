#pragma once
#include "exchange/engine.hpp"
#include <string_view>

namespace exchange {
enum class CommandKind { Add, Cancel, Book, Quit, Empty, Ioc, Market, Amend };
struct Command {
    CommandKind kind{CommandKind::Empty};
    OrderId id{};
    Side side{};
    Value price{};
    Value quantity{};
    bool operator==(const Command&) const = default;
};
// Throws invalid_argument; parser failures do not consume an engine sequence.
Command parse_command(std::string_view line);
std::string format_command(const Command& command);
Result execute(Engine& engine, const Command& command);
std::string format_book(const Engine& engine);
}
