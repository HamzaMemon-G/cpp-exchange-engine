#include "exchange/commands.hpp"
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace exchange {
namespace {
template<class T> T positive(const std::string& text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), value);
    if (ec != std::errc{} || end != text.data()+text.size() || value == 0 || text.front() == '-')
        throw std::invalid_argument("expected positive integer in range: " + text);
    return value;
}
}
Command parse_command(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::vector<std::string> words;
    for (std::string word; input >> word;) words.push_back(word);
    if (words.empty() || words.front().starts_with('#')) return {};
    if (((words[0] == "ADD" || words[0] == "IOC") && words.size() == 5) ||
        (words[0] == "MARKET" && words.size() == 4)) {
        Side side;
        if (words[2] == "BUY") side = Side::Buy;
        else if (words[2] == "SELL") side = Side::Sell;
        else throw std::invalid_argument("side must be BUY or SELL");
        if (words[0] == "MARKET")
            return {CommandKind::Market, positive<OrderId>(words[1]), side, 0, positive<Value>(words[3])};
        return {words[0] == "IOC" ? CommandKind::Ioc : CommandKind::Add, positive<OrderId>(words[1]), side,
                positive<Value>(words[3]), positive<Value>(words[4])};
    }
    if (words[0] == "CANCEL" && words.size() == 2)
        return {CommandKind::Cancel, positive<OrderId>(words[1]), Side::Buy, 0, 0};
    if (words[0] == "AMEND" && words.size() == 4)
        return {CommandKind::Amend, positive<OrderId>(words[1]), Side::Buy,
                positive<Value>(words[2]), positive<Value>(words[3])};
    if (words[0] == "BOOK" && words.size() == 1) return {CommandKind::Book};
    if (words[0] == "QUIT" && words.size() == 1) return {CommandKind::Quit};
    throw std::invalid_argument("expected ADD|IOC id BUY|SELL price quantity, MARKET id BUY|SELL quantity, AMEND id price remaining, CANCEL id, BOOK or QUIT");
}
std::string format_command(const Command& c) {
    if (c.kind == CommandKind::Add || c.kind == CommandKind::Ioc)
        return std::string(c.kind == CommandKind::Add ? "ADD " : "IOC ") + std::to_string(c.id) +
               (c.side == Side::Buy ? " BUY " : " SELL ") +
               std::to_string(c.price) + " " + std::to_string(c.quantity);
    if (c.kind == CommandKind::Market)
        return "MARKET " + std::to_string(c.id) + (c.side == Side::Buy ? " BUY " : " SELL ") +
               std::to_string(c.quantity);
    if (c.kind == CommandKind::Cancel) return "CANCEL " + std::to_string(c.id);
    if (c.kind == CommandKind::Amend)
        return "AMEND " + std::to_string(c.id) + " " + std::to_string(c.price) + " " + std::to_string(c.quantity);
    throw std::invalid_argument("only ADD, IOC, MARKET, AMEND and CANCEL are events");
}
Result execute(Engine& e, const Command& c) {
    if (c.kind == CommandKind::Add) return e.add(c.id, c.side, c.price, c.quantity);
    if (c.kind == CommandKind::Ioc) return e.add(c.id, c.side, c.price, c.quantity, TimeInForce::Ioc);
    if (c.kind == CommandKind::Market) return e.market(c.id, c.side, c.quantity);
    if (c.kind == CommandKind::Amend) return e.amend(c.id, c.price, c.quantity);
    if (c.kind == CommandKind::Cancel) return e.cancel(c.id);
    throw std::invalid_argument("not a state command");
}
std::string format_book(const Engine& e) {
    std::ostringstream out;
    out << "BOOK sequence=" << e.sequence() << '\n';
    for (const auto& o : e.orders())
        out << (o.side == Side::Buy ? "BUY" : "SELL") << " price=" << o.price
            << " remaining=" << o.quantity << " id=" << o.id << " sequence=" << o.sequence << '\n';
    out << "END_BOOK\n";
    return out.str();
}
}
