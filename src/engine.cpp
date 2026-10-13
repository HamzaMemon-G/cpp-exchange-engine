#include "exchange/engine.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace exchange {
std::string status_name(Status s) {
    switch(s) {
    case Status::Accepted: return "ACCEPTED";
    case Status::DuplicateId: return "DUPLICATE_ID";
    case Status::UnknownId: return "UNKNOWN_ID";
    case Status::InvalidOrder: return "INVALID_ORDER";
    }
    throw std::logic_error("invalid status");
}
std::uint64_t Engine::next_sequence() {
    if (sequence_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("command sequence exhausted");
    return ++sequence_;
}
template<class Book> void Engine::match(Order& incoming, Book& book, Result& result, bool market) {
    while (incoming.quantity > 0 && !book.empty()) {
        auto level = book.begin();
        if (!market && (incoming.side == Side::Buy ? level->first > incoming.price : level->first < incoming.price))
            break;
        auto& queue = level->second;
        auto& maker = queue.front();
        const auto quantity = std::min(incoming.quantity, maker.quantity);
        result.trades.push_back({result.sequence, maker.id, incoming.id, maker.price, quantity});
        incoming.quantity -= quantity;
        maker.quantity -= quantity;
        if (maker.quantity == 0) {
            active_.erase(maker.id);
            queue.pop_front();
            if (queue.empty()) book.erase(level);
        }
    }
}
template<class Book> void Engine::rest(Order order, Book& book) {
    const auto level = book.try_emplace(order.price).first;
    auto& queue = level->second;
    queue.push_back(order);
    if constexpr (std::is_same_v<Book, Bids>)
        active_.emplace(order.id, Location{Level{std::in_place_index<0>, level}, std::prev(queue.end())});
    else
        active_.emplace(order.id, Location{Level{std::in_place_index<1>, level}, std::prev(queue.end())});
}
Result Engine::add(OrderId id, Side side, Value price, Value quantity, TimeInForce tif) {
    return submit(id, side, price, quantity, tif, false);
}
Result Engine::market(OrderId id, Side side, Value quantity) {
    return submit(id, side, 0, quantity, TimeInForce::Ioc, true);
}
Result Engine::submit(OrderId id, Side side, Value price, Value quantity, TimeInForce tif, bool market) {
    Result result{next_sequence(), Status::Accepted, {}};
    if (id == 0 || (!market && price <= 0) || quantity <= 0 ||
        (side != Side::Buy && side != Side::Sell) || (tif != TimeInForce::Gtc && tif != TimeInForce::Ioc)) {
        result.status = Status::InvalidOrder;
        return result;
    }
    if (used_.contains(id)) {
        result.status = Status::DuplicateId;
        return result;
    }
    used_.insert(id);
    Order incoming{id, side, price, quantity, result.sequence};
    if (side == Side::Buy) match(incoming, asks_, result, market);
    else match(incoming, bids_, result, market);
    if (incoming.quantity > 0) {
        if (tif == TimeInForce::Ioc) result.expired_quantity = incoming.quantity;
        else if (side == Side::Buy) rest(incoming, bids_);
        else rest(incoming, asks_);
    }
    return result;
}
void Engine::remove(const Location& loc) {
    if (loc.level.index() == 0) {
        const auto level = std::get<0>(loc.level);
        level->second.erase(loc.order);
        if (level->second.empty()) bids_.erase(level);
    } else {
        const auto level = std::get<1>(loc.level);
        level->second.erase(loc.order);
        if (level->second.empty()) asks_.erase(level);
    }
}
Result Engine::cancel(OrderId id) {
    Result result{next_sequence(), Status::Accepted, {}};
    if (id == 0) { result.status = Status::InvalidOrder; return result; }
    const auto found = active_.find(id);
    if (found == active_.end()) { result.status = Status::UnknownId; return result; }
    remove(found->second);
    active_.erase(found);
    return result;
}
Result Engine::amend(OrderId id, Value price, Value remaining_quantity) {
    Result result{next_sequence(), Status::Accepted, {}};
    if (id == 0 || price <= 0 || remaining_quantity <= 0) {
        result.status = Status::InvalidOrder;
        return result;
    }
    const auto found = active_.find(id);
    if (found == active_.end()) { result.status = Status::UnknownId; return result; }
    auto incoming = *found->second.order;
    if (price == incoming.price) {
        auto& order = *found->second.order;
        if (remaining_quantity > incoming.quantity) {
            // The existing price cannot cross the opposite book. An increase
            // only loses FIFO priority; splice preserves the node and ID handles.
            order.sequence = result.sequence;
            auto move_to_back = [&](auto level) {
                auto& queue = level->second;
                queue.splice(queue.end(), queue, found->second.order);
            };
            if (found->second.level.index() == 0) move_to_back(std::get<0>(found->second.level));
            else move_to_back(std::get<1>(found->second.level));
        }
        order.quantity = remaining_quantity; // reductions/no-ops retain original sequence and position
        return result;
    }
    remove(found->second);
    active_.erase(found); // ID remains reserved; this is the same active order, not a new submission
    incoming.price = price;
    incoming.quantity = remaining_quantity;
    incoming.sequence = result.sequence;
    if (incoming.side == Side::Buy) match(incoming, asks_, result, false);
    else match(incoming, bids_, result, false);
    if (incoming.quantity > 0) {
        if (incoming.side == Side::Buy) rest(incoming, bids_);
        else rest(incoming, asks_);
    }
    return result;
}
std::vector<Order> Engine::orders() const {
    std::vector<Order> result;
    result.reserve(active_.size());
    for (const auto& [price, queue] : bids_) {
        (void)price;
        result.insert(result.end(), queue.begin(), queue.end());
    }
    for (const auto& [price, queue] : asks_) {
        (void)price;
        result.insert(result.end(), queue.begin(), queue.end());
    }
    return result;
}
bool Engine::valid() const {
    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first) return false;
    std::unordered_set<OrderId> seen;
    auto check = [&](const auto& book, Side side) {
        for (auto level = book.begin(); level != book.end(); ++level) {
            const auto& [price, queue] = *level;
            if (queue.empty()) return false;
            std::uint64_t last = 0;
            for (auto it = queue.begin(); it != queue.end(); ++it) {
                const auto& order = *it;
                if (order.id == 0 || order.price <= 0 || order.price != price || order.side != side || order.quantity <= 0 ||
                    order.sequence <= last || order.sequence > sequence_ || !used_.contains(order.id) ||
                    !seen.insert(order.id).second) return false;
                const auto loc = active_.find(order.id);
                if (loc == active_.end() || loc->second.order != it) return false;
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(book)>, Bids>) {
                    if (loc->second.level.index() != 0 || std::get<0>(loc->second.level) != level) return false;
                } else {
                    if (loc->second.level.index() != 1 || std::get<1>(loc->second.level) != level) return false;
                }
                last = order.sequence;
            }
        }
        return true;
    };
    return check(bids_, Side::Buy) && check(asks_, Side::Sell) && seen.size() == active_.size();
}
}
