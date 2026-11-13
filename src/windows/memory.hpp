#pragma once

// Real Windows implementations of MemoryProbe and MemoryControl (single owner of ntdll/DXGI memory I/O).

#include "ports/memory_control.hpp"
#include "ports/memory_probe.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

class WindowsMemoryProbe final : public MemoryProbe {
public:
    SystemMemoryTelemetry SampleSystemMemory() const override;
    bool SampleProcessDiskReadBytes(uint32_t pid, uint64_t& out_read_bytes) const override;
    std::string QueryProcessImageName(uint32_t pid) const override;
    uint32_t CurrentProcessId() const override;
};

class WindowsMemoryControl final : public MemoryControl {
public:
    WindowsMemoryControl();
    uint32_t TrimProcessWorkingSets(const std::vector<uint32_t>& pids) override;
    StandbyPurgeOutcome PurgeStandbyList(bool dry_run) override;
};

MemoryProbe& DefaultWindowsMemoryProbe();
MemoryControl& DefaultWindowsMemoryControl();
uint64_t QueryCurrentProcessPrivateWorkingSetBytes();

} // namespace vegaflow
