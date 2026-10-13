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
        if (!used.insert(c.id).second) { r.status = Status::DuplicateId; return r; }
        Order incoming{c.id, c.side, c.price, c.quantity, seq};
        while (incoming.quantity > 0) {
            auto best = book.end();
            for (auto it = book.begin(); it != book.end(); ++it) {
                if (it->side == incoming.side) continue;
                if (incoming.side == Side::Buy ? it->price > incoming.price : it->price < incoming.price) continue;
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
        if (incoming.quantity) book.push_back(incoming);
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
void workload_profiles() {
    for (const auto profile : {WorkloadProfile::Mixed, WorkloadProfile::Deep, WorkloadProfile::CancelHeavy,
                              WorkloadProfile::Sweep, WorkloadProfile::IdChurn}) {
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
                if (profile != WorkloadProfile::Mixed)
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
    for (const auto* contents : {"", "EXCHANGE_LOG_V1", "EXCHANGE_LOG_V2\n", "EXCHANGE_LOG_V1\n1\tADD 1 BUY 1 1",
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
        {"validation and bounds", validation}, {"40000 differential commands", differential},
        {"workload profiles", workload_profiles}, {"move and level lifetime", move_and_level_lifetime},
        {"event log", logging}};
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1; }
    }
}
