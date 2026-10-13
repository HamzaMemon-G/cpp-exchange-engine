#pragma once
#include "exchange/commands.hpp"

namespace exchange {
std::vector<Command> synthetic_workload(std::size_t count, std::uint64_t seed);

enum class WorkloadProfile { Mixed, Deep, CancelHeavy, Sweep, IdChurn, Aggressive };
WorkloadProfile parse_profile(std::string_view name);
std::string_view profile_name(WorkloadProfile profile);
struct WorkloadOptions {
    std::size_t count{100000};
    std::uint64_t seed{42};
    WorkloadProfile profile{WorkloadProfile::Mixed};
    std::size_t levels{1024}; // per side for deep/cancel-heavy; per sweep for sweep
    std::size_t depth{8};    // orders per level in the initial book or each sweep
};
struct Workload {
    std::vector<Command> setup; // untimed, applied to every fresh engine
    std::vector<Command> commands;
};
Workload benchmark_workload(const WorkloadOptions& options);
}
