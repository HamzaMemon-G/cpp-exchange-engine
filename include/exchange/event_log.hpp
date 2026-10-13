#pragma once
#include "exchange/commands.hpp"
#include <filesystem>
#include <fstream>
#include <functional>

namespace exchange {
// Intent journal: valid state commands, including business rejections.
Engine replay_log(const std::filesystem::path& path,
                  const std::function<void(const Result&)>& observer = {});
// Offline conversion: validate, copy without overwriting, then change the copy's
// header to V2. Source and existing destinations are never modified.
void upgrade_log(const std::filesystem::path& source, const std::filesystem::path& destination);
class EventLog {
public:
    explicit EventLog(const std::filesystem::path& path);
    void append(std::uint64_t sequence, const Command& command);
private:
    std::ofstream stream_;
    std::uint64_t last_{};
    int version_{2};
};
}
