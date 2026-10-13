#include "exchange/event_log.hpp"
#include <charconv>
#include <limits>
#include <stdexcept>

namespace exchange {
namespace {
constexpr auto header = "EXCHANGE_LOG_V1";
std::string complete_line(std::ifstream& in) {
    std::string line;
    if (!std::getline(in, line) || in.eof())
        throw std::runtime_error("truncated log: every line must end with newline");
    return line;
}
}
Engine replay_log(const std::filesystem::path& path, const std::function<void(const Result&)>& observer) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read log: " + path.string());
    if (complete_line(in) != header) throw std::runtime_error("unsupported log header");
    Engine engine;
    while (in.peek() != std::char_traits<char>::eof()) {
        const auto line = complete_line(in);
        const auto tab = line.find('\t');
        if (tab == std::string::npos) throw std::runtime_error("malformed log record");
        std::uint64_t seq{};
        const auto [end, ec] = std::from_chars(line.data(), line.data()+tab, seq);
        if (ec != std::errc{} || end != line.data()+tab || engine.sequence() ==
            std::numeric_limits<std::uint64_t>::max() || seq != engine.sequence()+1)
            throw std::runtime_error("invalid log sequence");
        const auto text = line.substr(tab+1);
        const auto command = parse_command(text);
        if (format_command(command) != text || std::to_string(seq) != line.substr(0, tab))
            throw std::runtime_error("noncanonical log record");
        const auto result = execute(engine, command);
        if (observer) observer(result);
    }
    if (in.bad()) throw std::runtime_error("log read failure");
    return engine;
}
EventLog::EventLog(const std::filesystem::path& path) {
    const bool exists = std::filesystem::exists(path);
    if (exists) last_ = replay_log(path).sequence(); // fail closed before append
    stream_.open(path, std::ios::binary | std::ios::app);
    if (!stream_) throw std::runtime_error("cannot open log for append");
    if (!exists) {
        stream_ << header << '\n';
        stream_.flush();
        if (!stream_) throw std::runtime_error("cannot write log header");
    }
}
void EventLog::append(std::uint64_t sequence, const Command& command) {
    if (last_ == std::numeric_limits<std::uint64_t>::max() || sequence != last_+1)
        throw std::runtime_error("append sequence mismatch");
    // Round trip enforces domain validation even for callers other than the CLI.
    const auto text = format_command(command);
    if (parse_command(text) != command) throw std::invalid_argument("invalid event command");
    stream_ << sequence << '\t' << text << '\n';
    stream_.flush();
    if (!stream_) throw std::runtime_error("log append failed; stop and inspect log before retry");
    last_ = sequence;
}
}
