// Unit test: the GPU memory shown and fed to live tuning is the global PDH sum, DXGI's own-process figure only as fallback.

#include "ports/memory_probe.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test120() {
    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    vegaflow::VideoMemoryTelemetry t;
    t.local_usage_bytes = 50 * kMb;  // DXGI: only VegaFlow's process
    t.gpu_shared_usage_bytes = 1500 * kMb;
    t.gpu_dedicated_usage_bytes = 500 * kMb;

    t.valid = true;
    const bool pdh_sum = vegaflow::GpuUsageBytes(t) == 2000 * kMb;

    t.valid = false;  // PDH unavailable: DXGI's number is all there is
    const bool dxgi_fallback = vegaflow::GpuUsageBytes(t) == 50 * kMb;

    vegaflow::VideoMemoryTelemetry empty;
    const bool empty_zero = vegaflow::GpuUsageBytes(empty) == 1;  // RED-RUN DEMO: deliberately wrong

    RecordResult(pdh_sum && dxgi_fallback && empty_zero, "Test 120: GPU usage is the PDH sum when valid, DXGI otherwise",
                 std::string("PdhSum=") + (pdh_sum ? "1" : "0") + ", DxgiFallback=" + (dxgi_fallback ? "1" : "0") +
                     ", Empty=" + (empty_zero ? "1" : "0"));
}

} // namespace vftest
