// E2.4 (row 6): the adapter signature is read only when asked. Needs session/game_session.cpp, which links
// Windows-only code, so it is separate from wake_loop_unit_tests.cpp (that one runs on the host).

#include "session/game_session.hpp"
#include "fake_device_probe.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

// FakeDeviceProbe is final: wrap it and count the signature reads.
class CountingProbe final : public vegaflow::DeviceProbe {
public:
    FakeDeviceProbe inner;
    int signature_reads = 0;
    vegaflow::RawDeviceSnapshot ProbeSystem(bool d) override { return inner.ProbeSystem(d); }
    vegaflow::GameDiskType ProbeDiskType(const std::wstring& p) override { return inner.ProbeDiskType(p); }
    uint8_t QueryAppGpuPreference(const std::wstring& p) override { return inner.QueryAppGpuPreference(p); }
    uint64_t QueryAdapterSignature() override {
        ++signature_reads;
        return inner.QueryAdapterSignature();
    }
};

} // namespace

// E2.4
void Test115() {
    using namespace vegaflow;
    SystemHardwareProfile hw{};
    hw.adapter_signature = 7;
    CountingProbe probe;
    probe.inner.snapshot.adapter_signature = 9;

    const bool skipped = !session::RefreshHardwareProfileIfChanged(probe, hw, 0, false) &&
                         probe.signature_reads == 0 && hw.adapter_signature == 7;
    RecordResult(skipped, "Test 115.1: probe_signature=false -> no signature read, profile untouched");

    const bool read = session::RefreshHardwareProfileIfChanged(probe, hw, 0, true) &&
                      probe.signature_reads == 1 && hw.adapter_signature == 9;
    RecordResult(read, "Test 115.2: probe_signature=true (device event) -> signature read and profile re-read");
}

} // namespace vftest
