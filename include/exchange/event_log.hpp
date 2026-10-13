#pragma once
#include "exchange/commands.hpp"
#include <filesystem>
#include <fstream>
#include <functional>

namespace exchange {
// Intent journal: valid ADD/CANCEL attempts, including business rejections.
Engine replay_log(const std::filesystem::path& path,
                  const std::function<void(const Result&)>& observer = {});
class EventLog {
public:
    explicit EventLog(const std::filesystem::path& path);
    void append(std::uint64_t sequence, const Command& command);
private:
    std::ofstream stream_;
    std::uint64_t last_{};
};
}
