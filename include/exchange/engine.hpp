#pragma once
#include "exchange/types.hpp"
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace exchange {
struct BookMetrics {
    std::size_t active_orders{}, bid_levels{}, ask_levels{}, reserved_ids{};
    bool operator==(const BookMetrics&) const = default;
};
// One engine represents one instrument. Not thread safe; no callbacks or I/O.
class Engine {
public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = default;
    Engine& operator=(Engine&&) = default;
    Result add(OrderId id, Side side, Value price, Value quantity, TimeInForce tif = TimeInForce::Gtc);
    Result market(OrderId id, Side side, Value quantity); // consumes available liquidity, then expires
    Result amend(OrderId id, Value price, Value remaining_quantity); // side/ID remain fixed
    Result cancel(OrderId id);
    std::vector<Order> orders() const; // buys descending, then sells ascending; FIFO
    std::uint64_t sequence() const { return sequence_; }
    bool valid() const; // O(n) diagnostic, not called by the hot path
    BookMetrics metrics() const { return {active_.size(), bids_.size(), asks_.size(), used_.size()}; }
private:
    using Queue = std::list<Order>;
    using Bids = std::map<Value, Queue, std::greater<Value>>;
    using Asks = std::map<Value, Queue>;
    // Index 0 is a bid level, index 1 an ask level. Use explicit indices because
    // a standard library may use the same iterator type for both map types.
    using Level = std::variant<Bids::iterator, Asks::iterator>;
    struct Location { Level level; Queue::iterator order; };
    void remove(const Location& location);
    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> active_;
    std::unordered_set<OrderId> used_;
    std::uint64_t sequence_{};
    std::uint64_t next_sequence();
    Result submit(OrderId id, Side side, Value price, Value quantity, TimeInForce tif, bool market);
    template<class Book> void match(Order& incoming, Book& opposite, Result& result, bool market);
    template<class Book> void rest(Order order, Book& book);
};
}
