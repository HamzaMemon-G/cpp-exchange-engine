#pragma once
#include "exchange/commands.hpp"

namespace exchange {
std::vector<Command> synthetic_workload(std::size_t count, std::uint64_t seed);
}
