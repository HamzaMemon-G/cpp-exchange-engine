#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace exchange {
using OrderId = std::uint64_t;
using Value = std::int64_t;
enum class Side { Buy, Sell };
struct Order {
    OrderId id{};
    Side side{};
    Value price{};
    Value quantity{};
    std::uint64_t sequence{};
    bool operator==(const Order&) const = default;
};
struct Trade {
    std::uint64_t sequence{};
    OrderId maker{};
    OrderId taker{};
    Value price{};
    Value quantity{};
    bool operator==(const Trade&) const = default;
};
enum class Status { Accepted, DuplicateId, UnknownId, InvalidOrder };
struct Result {
    std::uint64_t sequence{};
    Status status{};
    std::vector<Trade> trades;
    bool operator==(const Result&) const = default;
};
std::string status_name(Status status);
}
