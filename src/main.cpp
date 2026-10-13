#include "exchange/event_log.hpp"
#include <iostream>
#include <limits>
#include <memory>

using namespace exchange;
int main(int argc, char** argv) {
    try {
        std::filesystem::path input_path, log_path, replay_path, upgrade_source, upgrade_destination;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "exchange_cli [--input FILE] [--log FILE]\nexchange_cli --replay FILE\n"
                          << "exchange_cli --upgrade-log SOURCE --output-log DESTINATION\n";
                return 0;
            }
            if ((arg != "--input" && arg != "--log" && arg != "--replay" &&
                 arg != "--upgrade-log" && arg != "--output-log") || i+1 >= argc)
                throw std::invalid_argument("unknown or incomplete option: " + arg);
            auto& path = arg == "--input" ? input_path : arg == "--log" ? log_path :
                         arg == "--replay" ? replay_path : arg == "--upgrade-log" ? upgrade_source : upgrade_destination;
            if (!path.empty()) throw std::invalid_argument("duplicate option: " + arg);
            path = argv[++i];
        }
        if (!upgrade_source.empty() || !upgrade_destination.empty()) {
            if (upgrade_source.empty() || upgrade_destination.empty() || !input_path.empty() ||
                !log_path.empty() || !replay_path.empty())
                throw std::invalid_argument("upgrade requires --upgrade-log SOURCE --output-log DESTINATION exclusively");
            upgrade_log(upgrade_source, upgrade_destination);
            std::cout << "UPGRADED journal=" << upgrade_destination.string() << '\n';
            return 0;
        }
        if (!replay_path.empty()) {
            if (!input_path.empty() || !log_path.empty()) throw std::invalid_argument("--replay is exclusive");
            const auto replayed = replay_log(replay_path);
            std::cout << format_book(replayed);
            return 0;
        }
        std::ifstream file;
        if (!input_path.empty()) {
            file.open(input_path);
            if (!file) throw std::runtime_error("cannot open input file");
        }
        Engine engine;
        std::unique_ptr<EventLog> log;
        if (!input_path.empty() && !log_path.empty() && std::filesystem::exists(input_path) &&
            std::filesystem::exists(log_path) && std::filesystem::equivalent(input_path, log_path))
            throw std::invalid_argument("input and log must be different files");
        if (!log_path.empty()) {
            if (std::filesystem::exists(log_path)) engine = replay_log(log_path);
            log = std::make_unique<EventLog>(log_path);
        }
        std::istream& input = input_path.empty() ? std::cin : file;
        bool rejected = false;
        std::uint64_t line_number = 0;
        for (std::string line; std::getline(input, line);) {
            ++line_number;
            Command command;
            try { command = parse_command(line); }
            catch (const std::invalid_argument& e) {
                std::cerr << "ERROR line=" << line_number << ' ' << e.what() << '\n';
                rejected = true;
                continue;
            }
            if (command.kind == CommandKind::Empty) continue;
            if (command.kind == CommandKind::Quit) break;
            if (command.kind == CommandKind::Book) { std::cout << format_book(engine); continue; }
            if (engine.sequence() == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("command sequence exhausted");
            if (log) log->append(engine.sequence()+1, command);
            const auto result = execute(engine, command);
            std::cout << "RESULT sequence=" << result.sequence << " id=" << command.id
                      << " status=" << status_name(result.status) << '\n';
            rejected = rejected || result.status != Status::Accepted;
            for (const auto& trade : result.trades)
                std::cout << "TRADE sequence=" << trade.sequence << " maker=" << trade.maker
                          << " taker=" << trade.taker << " price=" << trade.price
                          << " quantity=" << trade.quantity << '\n';
            if (result.expired_quantity > 0)
                std::cout << "EXPIRED sequence=" << result.sequence << " id=" << command.id
                          << " quantity=" << result.expired_quantity << '\n';
        }
        if (input.bad()) throw std::runtime_error("input read failure");
        if (!std::cout) throw std::runtime_error("output write failure");
        return rejected ? 2 : 0;
    } catch (const std::exception& e) {
        std::cerr << "FATAL " << e.what() << '\n';
        return 1;
    }
}
