#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vegaflow {

struct FrameMetricsSnapshot {
    uint32_t target_pid = 0;
    uint64_t total_frames_captured = 0;
    double avg_fps = 0.0;
    double fps_1_percent_low = 0.0;
    double fps_0_1_percent_low = 0.0;
    double avg_frametime_ms = 0.0;
    double frametime_stddev_ms = 0.0;
    double min_frametime_ms = 0.0;
    double max_frametime_ms = 0.0;
    bool etw_active = false;
    bool valid = false;
};

class FrameTelemetryMonitor {
public:
    FrameTelemetryMonitor();
    ~FrameTelemetryMonitor();

    FrameTelemetryMonitor(const FrameTelemetryMonitor&) = delete;
    FrameTelemetryMonitor& operator=(const FrameTelemetryMonitor&) = delete;

    // Starts or changes the target PID to monitor
    void StartMonitoring(uint32_t target_pid);
    void StopMonitoring();

    // Manually records an interval between frames (in milliseconds) in the circular buffer
    // (used by both the ETW callback/sampler and the unit tests)
    void RecordFrameIntervalMs(double dt_ms);

    // Resets the accumulated metrics
    void ResetMetrics(uint32_t new_pid = 0);

    // Computes and returns the current mathematical snapshot (Avg FPS, 1% Low, 0.1% Low, StdDev)
    FrameMetricsSnapshot GetSnapshot() const;

    // Generates a human-readable post-match report
    std::string FormatSessionReport(const std::string& game_name = "Game Session") const;

    // Saves the session report to disk (creating the parent directory if it does not exist)
    bool SaveSessionReportToFile(const std::string& game_name = "Game Session", const std::string& file_path = "logs/session_reports.log") const;

    // Pure static math function to compute metrics from a vector of frametimes (in ms)
    static FrameMetricsSnapshot ComputeMetricsFromSamples(const std::vector<double>& frametimes_ms, uint32_t pid = 0, uint64_t total_frames = 0, bool etw_active = false);

private:
    static constexpr size_t kMaxSamplesWindow = 2048; // Ventana circular pre-asignada (~16 KB RAM)

    mutable std::mutex mutex_;
    std::vector<double> ring_buffer_;
    size_t write_index_ = 0;
    size_t sample_count_ = 0;
    uint64_t total_frames_ = 0;
    uint32_t target_pid_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> etw_session_active_{false};
    std::thread worker_thread_;

    void WorkerLoop();
};

} // namespace vegaflow
