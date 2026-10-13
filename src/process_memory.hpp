#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

// Process-wide observations, not an estimate of engine-owned allocations.
// Keep OS calls and file reads out of throughput and latency timing.
struct ProcessMemory {
    std::uint64_t resident_bytes{}, peak_resident_bytes{};
    bool available{};
};
inline ProcessMemory process_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return {};
    return {static_cast<std::uint64_t>(counters.WorkingSetSize),
            static_cast<std::uint64_t>(counters.PeakWorkingSetSize), true};
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    ProcessMemory result;
    std::string line;
    bool resident = false, peak = false;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            result.resident_bytes = std::stoull(line.substr(6))*1024;
            resident = true;
        } else if (line.starts_with("VmHWM:")) {
            result.peak_resident_bytes = std::stoull(line.substr(6))*1024;
            peak = true;
        }
    }
    result.available = resident && peak;
    return result;
#else
    return {};
#endif
}
