#pragma once
#include "exchange/types.hpp"
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace exchange {
// One engine represents one instrument. Not thread safe; no callbacks or I/O.
class Engine {
public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = default;
    Engine& operator=(Engine&&) = default;
    Result add(OrderId id, Side side, Value price, Value quantity);
    Result cancel(OrderId id);
    std::vector<Order> orders() const; // buys descending, then sells ascending; FIFO
    std::uint64_t sequence() const { return sequence_; }
    bool valid() const; // O(n) diagnostic, not called by the hot path
private:
    using Queue = std::list<Order>;
    using Bids = std::map<Value, Queue, std::greater<Value>>;
    using Asks = std::map<Value, Queue>;
    struct Location { Side side; Value price; Queue::iterator order; };
    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> active_;
    std::unordered_set<OrderId> used_;
    std::uint64_t sequence_{};
    std::uint64_t next_sequence();
    template<class Book> void match(Order& incoming, Book& opposite, Result& result);
    template<class Book> void rest(Order order, Book& book);
};
}
