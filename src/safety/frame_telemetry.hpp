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

    // Inicia o cambia el PID objetivo a monitorear
    void StartMonitoring(uint32_t target_pid);
    void StopMonitoring();

    // Registra manualmente un intervalo entre cuadros (en milisegundos) en el buffer circular
    // (usado tanto por el callback ETW/muestreador como por los tests unitarios)
    void RecordFrameIntervalMs(double dt_ms);

    // Reinicia las métricas acumuladas
    void ResetMetrics(uint32_t new_pid = 0);

    // Calcula y devuelve la instantánea matemática actual (Avg FPS, 1% Low, 0.1% Low, StdDev)
    FrameMetricsSnapshot GetSnapshot() const;

    // Genera un reporte legible post-partida
    std::string FormatSessionReport(const std::string& game_name = "Game Session") const;

    // Guarda el reporte de la sesión en disco (creando el directorio padre si no existe)
    bool SaveSessionReportToFile(const std::string& game_name = "Game Session", const std::string& file_path = "logs/session_reports.log") const;

    // Función matemática pura y estática para calcular métricas desde un vector de frametimes (en ms)
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
