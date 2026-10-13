#include "exchange/event_log.hpp"
#include "exchange/workload.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>

using namespace exchange;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void throws(F fn) {
    bool thrown = false;
    try { fn(); } catch (const std::exception&) { thrown = true; }
    require(thrown, "expected exception");
}
void matching() {
    Engine e;
    require(e.add(1, Side::Buy, 99, 4).trades.empty(), "noncrossing buy");
    require(e.add(2, Side::Sell, 101, 10).trades.empty(), "noncrossing sell");
    e.add(3, Side::Sell, 101, 3);
    e.add(4, Side::Sell, 102, 6);
    const auto a = e.add(5, Side::Buy, 102, 5);
    require(a.trades == std::vector<Trade>{{5, 2, 5, 101, 5}}, "partial fill at maker price");
    const auto b = e.add(6, Side::Buy, 102, 10);
    require(b.trades == std::vector<Trade>{{6, 2, 6, 101, 5}, {6, 3, 6, 101, 3}, {6, 4, 6, 102, 2}},
            "FIFO preserved and multiple price levels");
    require(e.orders() == std::vector<Order>{{1, Side::Buy, 99, 4, 1}, {4, Side::Sell, 102, 4, 4}}, "book residuals");
    require(e.cancel(4).status == Status::Accepted, "cancel partial maker");
    require(e.add(7, Side::Sell, 98, 9).trades == std::vector<Trade>{{8, 1, 7, 99, 4}}, "sell executes at bid");
    require(e.orders() == std::vector<Order>{{7, Side::Sell, 98, 5, 8}}, "incoming remainder");
    require(e.add(8, Side::Buy, 98, 5).trades.size() == 1 && e.orders().empty(), "exact fill removes levels");
    require(e.valid(), "matching invariants");
}
void cancellation() {
    Engine e;
    for (OrderId i = 1; i <= 4; ++i) e.add(i, Side::Buy, 100, 2);
    require(e.cancel(2).status == Status::Accepted, "cancel middle");
    e.cancel(1);
    e.cancel(4);
    require(e.add(5, Side::Sell, 100, 2).trades == std::vector<Trade>{{8, 3, 5, 100, 2}}, "remaining FIFO");
    require(e.cancel(3).status == Status::UnknownId, "filled ID is not cancellable");
    require(e.cancel(999).status == Status::UnknownId, "unknown cancellation");
    require(e.add(2, Side::Sell, 100, 1).status == Status::DuplicateId, "cancelled ID reserved");
    require(e.add(5, Side::Sell, 100, 1).status == Status::DuplicateId, "filled taker ID reserved");
    require(e.cancel(0).status == Status::InvalidOrder, "zero cancellation ID rejected by core");
    require(e.valid(), "cancellation invariants");
}
void validation() {
    Engine e;
    require(e.add(0, Side::Buy, 1, 1).status == Status::InvalidOrder, "zero ID");
    require(e.add(1, Side::Buy, 0, 1).status == Status::InvalidOrder, "zero price");
    require(e.add(1, Side::Buy, 1, -1).status == Status::InvalidOrder, "negative quantity");
    require(e.add(1, static_cast<Side>(99), 1, 1).status == Status::InvalidOrder, "invalid side");
    const auto max = std::numeric_limits<Value>::max();
    const auto id = std::numeric_limits<OrderId>::max();
    require(e.add(id, Side::Buy, max, max).status == Status::Accepted, "numeric maximums");
    e.add(1, Side::Buy, max, max);
    const auto result = e.add(2, Side::Sell, max, max);
    require(result.trades == std::vector<Trade>{{7, id, 2, max, max}}, "safe max quantity subtraction");
    require(e.orders().front().quantity == max && e.valid(), "no aggregate overflow");
    for (const auto* line : {"ADD 1 BUY 0 1", "ADD -1 BUY 1 1", "ADD 1 BUY -1 1", "ADD 1 BUY 1 0",
             "ADD 1 BOTH 1 1", "ADD 1 BUY 9223372036854775808 1", "CANCEL 18446744073709551616",
             "ADD 1 BUY 1 1 extra", "CANCEL", "CANCEL 0", "BOOK extra", "add 1 BUY 1 1", "QUIT extra",
             "ADD 1 BUY 1.0 1", "ADD +1 BUY 1 1"}) throws([&] { parse_command(line); });
    require(parse_command("  # comment").kind == CommandKind::Empty, "comments");
    require(parse_command("\t").kind == CommandKind::Empty, "blank line");
    require(parse_command(" ADD 1 BUY 1 2\r").quantity == 2, "CRLF input");
}
void immediate_orders() {
    Engine engine;
    engine.add(1, Side::Sell, 100, 5);
    engine.add(2, Side::Sell, 100, 4);
    engine.add(3, Side::Sell, 101, 6);
    engine.add(4, Side::Sell, 102, 8);
    const auto ioc = engine.add(5, Side::Buy, 101, 18, TimeInForce::Ioc);
    require(ioc.status == Status::Accepted && ioc.expired_quantity == 3 &&
            ioc.trades == std::vector<Trade>{{5, 1, 5, 100, 5}, {5, 2, 5, 100, 4}, {5, 3, 5, 101, 6}},
            "IOC respects limit, best price and FIFO; remainder expires");
    require(engine.orders() == std::vector<Order>{{4, Side::Sell, 102, 8, 4}}, "IOC never rests");
    require(engine.cancel(5).status == Status::UnknownId &&
            engine.add(5, Side::Buy, 100, 1).status == Status::DuplicateId, "expired IOC ID reserved");
    const auto partial = engine.market(6, Side::Buy, 3);
    require(partial.trades == std::vector<Trade>{{8, 4, 6, 102, 3}} && partial.expired_quantity == 0,
            "market executes at maker price");
    engine.add(7, Side::Sell, 102, 2);
    const auto sweep = engine.market(8, Side::Buy, 9);
    require(sweep.trades == std::vector<Trade>{{10, 4, 8, 102, 5}, {10, 7, 8, 102, 2}} &&
            sweep.expired_quantity == 2 && engine.orders().empty(), "market retains maker FIFO and expires excess");
    const auto empty = engine.market(9, Side::Sell, 10);
    require(empty.status == Status::Accepted && empty.trades.empty() && empty.expired_quantity == 10,
            "empty market accepted and fully expired");
    require(engine.add(9, Side::Buy, 1, 1).status == Status::DuplicateId && engine.valid(),
            "empty market ID reserved");
    Engine sells;
    sells.add(1, Side::Buy, 100, 5);
    sells.add(2, Side::Buy, 99, 7);
    const auto limit = sells.add(3, Side::Sell, 100, 7, TimeInForce::Ioc);
    require(limit.trades == std::vector<Trade>{{3, 1, 3, 100, 5}} && limit.expired_quantity == 2,
            "sell IOC refuses worse price");
    const auto market = sells.market(4, Side::Sell, 4);
    require(market.trades == std::vector<Trade>{{4, 2, 4, 99, 4}} &&
            sells.orders() == std::vector<Order>{{2, Side::Buy, 99, 3, 2}}, "sell market partial maker");
    require(sells.add(5, Side::Buy, 98, 3, TimeInForce::Ioc).expired_quantity == 3 && sells.valid(),
            "noncrossing IOC expires whole order");
    Engine bounds;
    const auto max = std::numeric_limits<Value>::max();
    bounds.add(1, Side::Sell, max, max);
    require(bounds.market(2, Side::Buy, max).trades == std::vector<Trade>{{2, 1, 2, max, max}},
            "market reaches maximum tick without price sentinel");
    bounds.add(3, Side::Buy, 1, 3);
    require(bounds.market(4, Side::Sell, 5).expired_quantity == 2 && bounds.orders().empty(),
            "market reaches minimum positive tick");
    require(bounds.market(5, Side::Buy, 0).status == Status::InvalidOrder &&
            bounds.market(0, Side::Buy, 1).status == Status::InvalidOrder &&
            bounds.market(5, static_cast<Side>(99), 1).status == Status::InvalidOrder,
            "invalid market arguments");
    require(bounds.add(5, Side::Buy, 1, 1, static_cast<TimeInForce>(99)).status == Status::InvalidOrder,
            "invalid time in force");
    require(bounds.market(5, Side::Buy, max).expired_quantity == max && bounds.valid(),
            "invalid arguments do not reserve ID; max expiration safe");
    for (const auto* text : {"IOC 1 BUY 0 1", "IOC 1 SELL 1 0", "IOC 1 BUY 1", "IOC 1 BOTH 1 1",
                            "MARKET 1 BUY 0", "MARKET 1 SELL -1", "MARKET 1 BOTH 1", "MARKET 0 BUY 1",
                            "MARKET 1 BUY 1 extra", "MARKET 1 BUY 9223372036854775808"})
        throws([&] { parse_command(text); });
    for (const auto* text : {"IOC 1 BUY 100 3", "IOC 2 SELL 99 4", "MARKET 3 BUY 5", "MARKET 4 SELL 6"})
        require(format_command(parse_command(text)) == text, "immediate command canonical roundtrip");
}
// Deliberately simple O(n) reference: flat vector, best maker selected by scan.
class Reference {
public:
    std::vector<Order> book;
    std::unordered_set<OrderId> used;
    std::uint64_t seq{};
    Result run(const Command& c) {
        Result r{++seq, Status::Accepted, {}};
        if (c.kind == CommandKind::Cancel) {
            auto it = std::find_if(book.begin(), book.end(), [&](auto o) { return o.id == c.id; });
            if (it == book.end()) r.status = Status::UnknownId; else book.erase(it);
            return r;
        }
        Order incoming{c.id, c.side, c.price, c.quantity, seq};
        if (c.kind == CommandKind::Amend) {
            const auto found = std::find_if(book.begin(), book.end(), [&](const Order& order) { return order.id == c.id; });
            if (found == book.end()) { r.status = Status::UnknownId; return r; }
            if (c.price == found->price && c.quantity <= found->quantity) {
                found->quantity = c.quantity;
                return r;
            }
            incoming.side = found->side;
            book.erase(found);
        } else if (!used.insert(c.id).second) { r.status = Status::DuplicateId; return r; }
        while (incoming.quantity > 0) {
            auto best = book.end();
            for (auto it = book.begin(); it != book.end(); ++it) {
                if (it->side == incoming.side) continue;
                if (c.kind != CommandKind::Market &&
                    (incoming.side == Side::Buy ? it->price > incoming.price : it->price < incoming.price)) continue;
                if (best == book.end() ||
                    (incoming.side == Side::Buy ? it->price < best->price : it->price > best->price) ||
                    (it->price == best->price && it->sequence < best->sequence)) best = it;
            }
            if (best == book.end()) break;
            const auto q = std::min(incoming.quantity, best->quantity);
            r.trades.push_back({seq, best->id, incoming.id, best->price, q});
            incoming.quantity -= q;
            best->quantity -= q;
            if (!best->quantity) book.erase(best);
        }
        if (incoming.quantity) {
            if (c.kind == CommandKind::Ioc || c.kind == CommandKind::Market) r.expired_quantity = incoming.quantity;
            else book.push_back(incoming);
        }
        return r;
    }
    std::vector<Order> orders() const {
        auto sorted = book;
        std::sort(sorted.begin(), sorted.end(), [](const Order& a, const Order& b) {
            if (a.side != b.side) return a.side == Side::Buy;
            if (a.price != b.price) return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
            return a.sequence < b.sequence;
        });
        return sorted;
    }
};
void differential() {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        Engine engine, repeat;
        Reference reference;
        std::mt19937_64 random(seed);
        Value submitted = 0, executed = 0, cancelled = 0;
        for (std::size_t i = 0; i < 2000; ++i) {
            Command c;
            if (random()%4 == 0) c = {CommandKind::Cancel, 1+random()%500, Side::Buy, 0, 0};
            else c = {CommandKind::Add, 1+random()%500, random()%2 ? Side::Buy : Side::Sell,
                      static_cast<Value>(95+random()%11), static_cast<Value>(1+random()%30)};
            const auto before = engine.orders();
            const auto got = execute(engine, c);
            require(got == reference.run(c), "random reference result mismatch");
            require(got == execute(repeat, c), "deterministic results");
            require(engine.orders() == reference.orders(), "random reference book mismatch");
            require(engine.orders() == repeat.orders(), "deterministic book");
            require(engine.valid(), "random book invariant");
            if (got.status == Status::Accepted) {
                if (c.kind == CommandKind::Add) submitted += c.quantity;
                else for (const auto& o : before) if (o.id == c.id) cancelled += o.quantity;
            }
            for (const auto& t : got.trades) {
                require(t.quantity > 0 && t.maker != t.taker, "positive distinct trade");
                executed += t.quantity;
            }
            Value resting = 0;
            for (const auto& o : engine.orders()) resting += o.quantity;
            require(submitted == 2*executed + cancelled + resting, "quantity conservation");
        }
    }
    require(synthetic_workload(1000, 42) == synthetic_workload(1000, 42), "workload deterministic");
    require(synthetic_workload(1000, 42) != synthetic_workload(1000, 43), "seed changes workload");
}
void immediate_differential() {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        Engine engine;
        Reference reference;
        std::mt19937_64 random(seed+100);
        Value submitted = 0, traded = 0, cancelled = 0, expired = 0;
        for (std::size_t index = 0; index < 3000; ++index) {
            const auto action = random()%100;
            Command command;
            if (action < 20) command = {CommandKind::Cancel, 1+random()%1000, Side::Buy, 0, 0};
            else {
                const auto kind = action < 60 ? CommandKind::Add : action < 80 ? CommandKind::Ioc : CommandKind::Market;
                command = {kind, 1+random()%1000, random()%2 ? Side::Buy : Side::Sell,
                           kind == CommandKind::Market ? 0 : static_cast<Value>(95+random()%11),
                           static_cast<Value>(1+random()%30)};
            }
            const auto before = engine.orders();
            const auto result = execute(engine, command);
            require(result == reference.run(command), "immediate random reference result");
            require(engine.orders() == reference.orders() && engine.valid(), "immediate random reference book");
            if (result.status == Status::Accepted) {
                if (command.kind == CommandKind::Cancel) {
                    for (const auto& order : before) if (order.id == command.id) cancelled += order.quantity;
                } else submitted += command.quantity;
            }
            Value command_filled = 0;
            for (const auto& trade : result.trades) {
                require(trade.quantity > 0 && trade.maker != trade.taker, "immediate positive distinct trade");
                require(command.kind == CommandKind::Market ||
                        (command.side == Side::Buy ? trade.price <= command.price : trade.price >= command.price),
                        "IOC execution respects limit");
                command_filled += trade.quantity;
                traded += trade.quantity;
            }
            require(result.expired_quantity >= 0, "nonnegative expired quantity");
            expired += result.expired_quantity;
            if ((command.kind == CommandKind::Ioc || command.kind == CommandKind::Market) &&
                result.status == Status::Accepted)
                require(command_filled+result.expired_quantity == command.quantity, "immediate quantity disposition");
            Value resting = 0;
            for (const auto& order : engine.orders()) resting += order.quantity;
            require(submitted == 2*traded+cancelled+expired+resting, "immediate quantity conservation");
        }
    }
}
void amendments() {
    Engine engine;
    engine.add(1, Side::Buy, 100, 10);
    engine.add(2, Side::Buy, 100, 8);
    require(engine.amend(1, 100, 6).status == Status::Accepted &&
            engine.orders() == std::vector<Order>{{1, Side::Buy, 100, 6, 1}, {2, Side::Buy, 100, 8, 2}},
            "same-price reduction retains FIFO sequence");
    engine.amend(1, 100, 6);
    require(engine.orders().front().sequence == 1, "no-op amendment retains priority");
    require(engine.market(3, Side::Sell, 2).trades == std::vector<Trade>{{5, 1, 3, 100, 2}},
            "reduced maker fills first");
    require(engine.amend(1, 100, 7).status == Status::Accepted &&
            engine.orders() == std::vector<Order>{{2, Side::Buy, 100, 8, 2}, {1, Side::Buy, 100, 7, 6}},
            "increase uses remaining quantity and loses priority");
    require(engine.market(4, Side::Sell, 8).trades == std::vector<Trade>{{7, 2, 4, 100, 8}},
            "increased order follows older maker");
    engine.add(5, Side::Sell, 102, 4);
    engine.add(6, Side::Sell, 103, 6);
    const auto cross = engine.amend(1, 103, 12);
    require(cross.trades == std::vector<Trade>{{10, 5, 1, 102, 4}, {10, 6, 1, 103, 6}} &&
            engine.orders() == std::vector<Order>{{1, Side::Buy, 103, 2, 10}},
            "price amendment crosses multiple levels and rests remainder with new priority");
    const auto before = engine.orders();
    require(engine.amend(1, 0, 1).status == Status::InvalidOrder &&
            engine.amend(1, 100, 0).status == Status::InvalidOrder &&
            engine.amend(0, 100, 1).status == Status::InvalidOrder && engine.orders() == before,
            "invalid amendment preserves active order");
    require(engine.amend(2, 100, 1).status == Status::UnknownId, "filled order cannot amend");
    engine.cancel(1);
    require(engine.amend(1, 100, 1).status == Status::UnknownId &&
            engine.add(1, Side::Sell, 100, 1).status == Status::DuplicateId, "cancelled amended ID stays reserved");
    Engine sell;
    sell.add(1, Side::Sell, 110, 4);
    sell.add(2, Side::Sell, 110, 4);
    sell.amend(1, 111, 3); // remove old level member, change side's price
    sell.amend(1, 110, 2); // even a reduction loses priority when price changes
    require(sell.market(3, Side::Buy, 5).trades ==
            std::vector<Trade>{{5, 2, 3, 110, 4}, {5, 1, 3, 110, 1}}, "sell price change goes to back of level");
    require(sell.orders() == std::vector<Order>{{1, Side::Sell, 110, 1, 4}} && sell.valid(), "amended partial maker retains queue");
    Engine moved(std::move(sell));
    require(moved.amend(1, 109, 2).status == Status::Accepted && moved.valid(), "amend moved engine level handles");
    const auto max = std::numeric_limits<Value>::max();
    require(moved.amend(1, max, max).status == Status::Accepted &&
            moved.market(4, Side::Buy, max).trades.front().quantity == max && moved.valid(), "maximum amendment quantities");
    for (const auto* text : {"AMEND 0 100 1", "AMEND 1 0 1", "AMEND 1 100 0", "AMEND 1 BUY 100 1",
                            "AMEND 1 100", "AMEND 1 100 -1", "AMEND 1 100 9223372036854775808"})
        throws([&] { parse_command(text); });
    require(format_command(parse_command("AMEND 1 101 6")) == "AMEND 1 101 6", "amend canonical roundtrip");
}
void amendment_differential() {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        Engine engine;
        Reference reference;
        std::mt19937_64 random(seed+200);
        Value submitted = 0, traded = 0, cancelled = 0, expired = 0;
        for (std::size_t index = 0; index < 3000; ++index) {
            const auto action = random()%100;
            const auto kind = action < 15 ? CommandKind::Cancel : action < 35 ? CommandKind::Amend :
                              action < 65 ? CommandKind::Add : action < 85 ? CommandKind::Ioc : CommandKind::Market;
            const Command command{kind, 1+random()%1000,
                kind == CommandKind::Cancel || kind == CommandKind::Amend ? Side::Buy : random()%2 ? Side::Buy : Side::Sell,
                kind == CommandKind::Market || kind == CommandKind::Cancel ? 0 : static_cast<Value>(95+random()%11),
                kind == CommandKind::Cancel ? 0 : static_cast<Value>(1+random()%30)};
            const auto before = engine.orders();
            const auto result = execute(engine, command);
            require(result == reference.run(command), "amendment random reference result");
            require(engine.orders() == reference.orders() && engine.valid(), "amendment random reference book");
            if (result.status == Status::Accepted) {
                if (kind == CommandKind::Cancel || kind == CommandKind::Amend) {
                    for (const auto& order : before) if (order.id == command.id) {
                        if (kind == CommandKind::Cancel) cancelled += order.quantity;
                        else if (command.quantity > order.quantity) submitted += command.quantity-order.quantity;
                        else cancelled += order.quantity-command.quantity;
                    }
                } else submitted += command.quantity;
            }
            for (const auto& trade : result.trades) traded += trade.quantity;
            expired += result.expired_quantity;
            Value resting = 0;
            for (const auto& order : engine.orders()) resting += order.quantity;
            require(submitted == 2*traded+cancelled+expired+resting, "amendment quantity conservation");
        }
    }
}
void workload_profiles() {
    for (const auto profile : {WorkloadProfile::Mixed, WorkloadProfile::Deep, WorkloadProfile::CancelHeavy,
                              WorkloadProfile::Sweep, WorkloadProfile::IdChurn, WorkloadProfile::Aggressive}) {
        const WorkloadOptions options{1000, 42, profile, 16, 3};
        const auto workload = benchmark_workload(options);
        const auto repeated = benchmark_workload(options);
        require(workload.setup == repeated.setup && workload.commands == repeated.commands,
                "profile reproducibility");
        require(workload.commands.size() == options.count, "profile exact command count");
        require(parse_profile(profile_name(profile)) == profile, "profile name roundtrip");
        Engine engine;
        Reference reference;
        std::size_t cancellations = 0, trades = 0;
        for (const auto* commands : {&workload.setup, &workload.commands}) {
            for (const auto& command : *commands) {
                const auto result = execute(engine, command);
                require(result == reference.run(command), "profile reference result");
                require(engine.orders() == reference.orders() && engine.valid(), "profile reference book");
                if (profile != WorkloadProfile::Mixed && profile != WorkloadProfile::Aggressive)
                    require(result.status == Status::Accepted, "specialized profiles contain no rejections");
                if (command.kind == CommandKind::Cancel) ++cancellations;
                trades += result.trades.size();
                const auto metrics = engine.metrics();
                require(metrics.active_orders == reference.book.size() && metrics.reserved_ids == reference.used.size(),
                        "book metrics reflect reference state");
            }
        }
        if (profile == WorkloadProfile::Mixed)
            require(workload.commands == synthetic_workload(options.count, options.seed) && workload.setup.empty(),
                    "mixed preserves original workload");
        if (profile == WorkloadProfile::Deep || profile == WorkloadProfile::CancelHeavy) {
            require(workload.setup.size() == 2*options.levels*options.depth && trades == 0,
                    "deep setup and noncrossing workload");
            require(cancellations > 0, "deep profile exercises successful cancellation");
        }
        if (profile == WorkloadProfile::Sweep) require(trades > 500, "sweep exercises many makers");
        if (profile == WorkloadProfile::IdChurn)
            require(engine.metrics().active_orders == 0 && engine.metrics().reserved_ids == 500 && cancellations == 500,
                    "ID churn retains IDs after draining book");
    }
    require(benchmark_workload({1, 0, WorkloadProfile::Sweep, 1, 1}).commands.size() == 1,
            "partial sweep exact bound");
    require(benchmark_workload({3, 0, WorkloadProfile::IdChurn, 1, 1}).commands.back().kind == CommandKind::Add,
            "odd churn count");
    throws([] { parse_profile("bogus"); });
    throws([] { benchmark_workload({0, 0, WorkloadProfile::Mixed, 1, 1}); });
    throws([] { benchmark_workload({1, 0, WorkloadProfile::Deep, 0, 1}); });
    throws([] { benchmark_workload({1, 0, WorkloadProfile::Deep, 1000000, 1000000}); });
    throws([] { benchmark_workload({1, 0, static_cast<WorkloadProfile>(99), 1, 1}); });
}
void move_and_level_lifetime() {
    Engine source;
    for (OrderId id = 1; id <= 100; ++id) {
        source.add(id, Side::Buy, static_cast<Value>(90+id%10), 2);
        source.add(id+100, Side::Sell, static_cast<Value>(110+id%10), 2);
    }
    Engine moved(std::move(source));
    require(moved.valid(), "move construction populated multilevel index");
    Engine target;
    target.add(999, Side::Buy, 1, 1);
    target.add(1000, Side::Sell, 1000, 1);
    target = std::move(moved);
    require(target.valid(), "move assignment replaces populated destination");
    for (OrderId id = 1; id <= 100; id += 2) {
        require(target.cancel(id).status == Status::Accepted, "cancel moved bid");
        require(target.cancel(id+100).status == Status::Accepted, "cancel moved ask");
        require(target.valid(), "moved cancellation index");
    }
    target.add(201, Side::Buy, 120, 100);
    require(target.valid(), "sweep erases moved ask levels");
    for (OrderId id = 2; id <= 100; id += 2) {
        require(target.cancel(id).status == Status::Accepted, "cancel final moved bid levels");
        require(target.cancel(id+100).status == Status::UnknownId, "filled moved ask absent");
    }
    require(target.orders().empty() && target.valid(), "all moved levels removed");
}
void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    if (!out) throw std::runtime_error("cannot write test fixture");
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void immediate_logging() {
    const auto dir = std::filesystem::current_path()/"exchange-test-fixtures";
    std::filesystem::create_directories(dir);
    const auto path = dir/"immediate.log";
    std::filesystem::remove(path);
    Engine engine;
    std::vector<Command> commands;
    std::mt19937_64 random(991);
    for (OrderId id = 1; id <= 2000; ++id) {
        const auto kind = id%5 == 0 ? CommandKind::Cancel : id%5 == 1 ? CommandKind::Add :
                          id%5 == 2 ? CommandKind::Ioc : id%5 == 3 ? CommandKind::Market : CommandKind::Amend;
        commands.push_back({kind, kind == CommandKind::Cancel || kind == CommandKind::Amend ? 1+random()%id : id,
            kind == CommandKind::Cancel || kind == CommandKind::Amend ? Side::Buy : random()%2 ? Side::Buy : Side::Sell,
            kind == CommandKind::Market || kind == CommandKind::Cancel ? 0 : static_cast<Value>(95+random()%11),
            kind == CommandKind::Cancel ? 0 : static_cast<Value>(1+random()%20)});
    }
    commands.push_back({CommandKind::Market, 1, Side::Buy, 0, 1}); // duplicate across order types
    std::vector<Result> expected;
    {
        EventLog log(path);
        for (const auto& command : commands) {
            log.append(engine.sequence()+1, command);
            expected.push_back(execute(engine, command));
        }
    }
    require(read_file(path).starts_with("EXCHANGE_LOG_V2\n"), "new journals use V2");
    std::vector<Result> replayed;
    auto restored = replay_log(path, [&](const Result& result) { replayed.push_back(result); });
    require(replayed == expected && restored.orders() == engine.orders() && restored.valid(),
            "V2 replay includes expired quantities and trades");
    const Command continuation{CommandKind::Market, 2001, Side::Sell, 0, 100};
    {
        EventLog log(path);
        log.append(restored.sequence()+1, continuation);
        execute(restored, continuation);
    }
    require(replay_log(path).orders() == restored.orders() && replay_log(path).sequence() == restored.sequence(),
            "resume V2 market command");
    const auto legacy = dir/"legacy.log";
    const auto upgraded = dir/"upgraded.log";
    const std::string legacy_bytes = "EXCHANGE_LOG_V1\n1\tADD 10 SELL 100 5\n2\tCANCEL 999\n";
    write_file(legacy, legacy_bytes);
    {
        EventLog log(legacy);
        throws([&] { log.append(3, {CommandKind::Ioc, 11, Side::Buy, 100, 7}); });
        require(read_file(legacy) == legacy_bytes, "V1 refuses new command without writing bytes");
        log.append(3, {CommandKind::Cancel, 999, Side::Buy, 0, 0});
    }
    const auto source_bytes = read_file(legacy);
    std::filesystem::remove(upgraded);
    upgrade_log(legacy, upgraded);
    require(read_file(legacy) == source_bytes && read_file(upgraded) ==
            "EXCHANGE_LOG_V2"+source_bytes.substr(std::string("EXCHANGE_LOG_V1").size()),
            "upgrade modifies only copied header");
    require(replay_log(legacy).orders() == replay_log(upgraded).orders() && replay_log(upgraded).sequence() == 3,
            "upgrade preserves state and sequence");
    {
        EventLog log(upgraded);
        log.append(4, {CommandKind::Ioc, 11, Side::Buy, 100, 7});
    }
    std::vector<Result> results;
    require(replay_log(upgraded, [&](const Result& result) { results.push_back(result); }).orders().empty() &&
            results.back().expired_quantity == 2, "upgraded copy accepts new command after history");
    const auto destination_bytes = read_file(upgraded);
    throws([&] { upgrade_log(legacy, upgraded); });
    throws([&] { upgrade_log(legacy, legacy); });
    require(read_file(upgraded) == destination_bytes && read_file(legacy) == source_bytes,
            "upgrade refuses overwrite and source alias");
    const auto bad = dir/"immediate-malformed.log";
    const auto no_output = dir/"upgrade-must-not-exist.log";
    std::filesystem::remove(no_output);
    for (const auto* bytes : {"EXCHANGE_LOG_V1\n1\tMARKET 1 BUY 1\n", "EXCHANGE_LOG_V1\n1\tAMEND 1 100 1\n",
            "EXCHANGE_LOG_V2\n1\tAMEND 1 100 0\n",
            "EXCHANGE_LOG_V1\n1\tIOC 1 BUY 1 1\n", "EXCHANGE_LOG_V2\n1\tMARKET 1 BUY 1",
            "EXCHANGE_LOG_V2\n1\tIOC 1 BUY 0 1\n", "EXCHANGE_LOG_V2\n1\tMARKET 1 BUY 0\n",
            "EXCHANGE_LOG_V2\n1\tMARKET 01 BUY 1\n", "EXCHANGE_LOG_V2\n1\tIOC 1  BUY 1 1\n",
            "EXCHANGE_LOG_V2\n1\tADD 1 BUY 1 1\n3\tMARKET 2 SELL 1\n"}) {
        write_file(bad, bytes);
        throws([&] { replay_log(bad); });
        throws([&] { EventLog log(bad); });
        throws([&] { upgrade_log(bad, no_output); });
        require(read_file(bad) == bytes && !std::filesystem::exists(no_output),
                "malformed V1/V2 fail closed before upgrade output");
    }
}
void logging() {
    // CTest uses the build directory; never touches user logs.
    const auto dir = std::filesystem::current_path()/"exchange-test-fixtures";
    std::filesystem::create_directories(dir);
    const auto path = dir/"roundtrip.log";
    write_file(path, "EXCHANGE_LOG_V1\n");
    Engine original;
    auto commands = synthetic_workload(3000, 77);
    commands.push_back({CommandKind::Add, 1, Side::Buy, 100, 1});
    std::vector<Result> expected;
    {
        EventLog log(path);
        for (const auto& c : commands) {
            log.append(original.sequence()+1, c);
            expected.push_back(execute(original, c));
        }
    }
    std::vector<Result> actual;
    auto replayed = replay_log(path, [&](const auto& r) { actual.push_back(r); });
    require(actual == expected && replayed.orders() == original.orders(), "replay results and book");
    require(replayed.valid(), "move-return replay invariants");
    Engine moved;
    moved = std::move(replayed);
    require(moved.valid(), "move assignment index iterators");
    {
        EventLog log(path);
        const Command cancel{CommandKind::Cancel, 999999, Side::Buy, 0, 0};
        log.append(moved.sequence()+1, cancel);
        execute(moved, cancel);
        throws([&] { log.append(1, cancel); });
        throws([&] { log.append(moved.sequence()+1, {CommandKind::Add, 0, Side::Buy, 1, 1}); });
    }
    require(replay_log(path).sequence() == moved.sequence(), "append resumes sequence");
    const auto bad = dir/"malformed.log";
    for (const auto* contents : {"", "EXCHANGE_LOG_V1", "EXCHANGE_LOG_V3\n", "EXCHANGE_LOG_V1\n1\tADD 1 BUY 1 1",
             "EXCHANGE_LOG_V1\n2\tADD 1 BUY 1 1\n", "EXCHANGE_LOG_V1\n1\tBOOK\n",
             "EXCHANGE_LOG_V1\n1\tADD 1 BUY 0 1\n", "EXCHANGE_LOG_V1\n01\tADD 1 BUY 1 1\n",
             "EXCHANGE_LOG_V1\n1\tADD 1  BUY 1 1\n", "EXCHANGE_LOG_V1\n\n",
             "EXCHANGE_LOG_V1\n1\tADD 1 BUY 1 1\n1\tCANCEL 1\n", "EXCHANGE_LOG_V1\n1 ADD 1 BUY 1 1\n"}) {
        write_file(bad, contents);
        throws([&] { replay_log(bad); });
        throws([&] { EventLog log(bad); });
        std::ifstream in(bad, std::ios::binary);
        const std::string unchanged{std::istreambuf_iterator<char>(in), {}};
        require(unchanged == contents, "bad log untouched");
    }
    write_file(bad, "EXCHANGE_LOG_V1\n");
    require(replay_log(bad).orders().empty(), "header only valid");
}
}
int main() {
    const std::pair<const char*, void(*)()> tests[] = {{"matching", matching}, {"cancellation", cancellation},
        {"validation and bounds", validation}, {"market and IOC", immediate_orders}, {"amendment priority", amendments},
        {"40000 differential commands", differential}, {"60000 immediate differential commands", immediate_differential},
        {"60000 amendment differential commands", amendment_differential},
        {"workload profiles", workload_profiles}, {"move and level lifetime", move_and_level_lifetime},
        {"event log", logging}, {"V2 immediate journal and upgrade", immediate_logging}};
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1; }
    }
}
