#include "exchange/event_log.hpp"
#include <charconv>
#include <limits>
#include <stdexcept>

namespace exchange {
namespace {
constexpr auto header_v1 = "EXCHANGE_LOG_V1";
constexpr auto header_v2 = "EXCHANGE_LOG_V2";
int log_version(std::string_view header) {
    if (header == header_v1) return 1;
    if (header == header_v2) return 2;
    throw std::runtime_error("unsupported log header");
}
void check_version(int version, const Command& command) {
    if (version == 1 && command.kind != CommandKind::Add && command.kind != CommandKind::Cancel)
        throw std::runtime_error("V1 journal supports only ADD/CANCEL; upgrade a copy to V2 for new commands");
}
std::string complete_line(std::ifstream& in) {
    std::string line;
    if (!std::getline(in, line) || in.eof())
        throw std::runtime_error("truncated log: every line must end with newline");
    return line;
}
Engine replay_impl(const std::filesystem::path& path, const std::function<void(const Result&)>& observer,
                   int* version_out = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read log: " + path.string());
    const auto version = log_version(complete_line(in));
    if (version_out) *version_out = version;
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
        check_version(version, command);
        if (format_command(command) != text || std::to_string(seq) != line.substr(0, tab))
            throw std::runtime_error("noncanonical log record");
        const auto result = execute(engine, command);
        if (observer) observer(result);
    }
    if (in.bad()) throw std::runtime_error("log read failure");
    return engine;
}
}
Engine replay_log(const std::filesystem::path& path, const std::function<void(const Result&)>& observer) {
    return replay_impl(path, observer);
}
void upgrade_log(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (std::filesystem::exists(destination)) throw std::runtime_error("upgrade destination already exists");
    (void)replay_log(source); // validate all records before creating any output
    if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none))
        throw std::runtime_error("cannot copy journal for upgrade");
    try {
        {
            std::fstream output(destination, std::ios::binary | std::ios::in | std::ios::out);
            if (!output) throw std::runtime_error("cannot open upgraded copy");
            output << header_v2; // both header versions have exactly the same length
            output.flush();
            if (!output) throw std::runtime_error("cannot write upgraded header");
        }
        (void)replay_log(destination); // catch invalid/truncated copied records too
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(destination, ignored); // only the copy created by this operation
        throw;
    }
}
EventLog::EventLog(const std::filesystem::path& path) {
    const bool exists = std::filesystem::exists(path);
    if (exists) last_ = replay_impl(path, {}, &version_).sequence(); // fail closed before append
    stream_.open(path, std::ios::binary | std::ios::app);
    if (!stream_) throw std::runtime_error("cannot open log for append");
    if (!exists) {
        stream_ << header_v2 << '\n';
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
    check_version(version_, command);
    stream_ << sequence << '\t' << text << '\n';
    stream_.flush();
    if (!stream_) throw std::runtime_error("log append failed; stop and inspect log before retry");
    last_ = sequence;
}
}
