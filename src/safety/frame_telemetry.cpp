#include "safety/frame_telemetry.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace vegaflow {

namespace {

// GUID para el proveedor Microsoft-Windows-DXGI: {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID kDxgiProviderGuid = {
    0xca11c036, 0x0102, 0x4a2d, { 0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xd3, 0xc9 }
};

struct EtwSessionContext {
    FrameTelemetryMonitor* monitor = nullptr;
    uint32_t target_pid = 0;
    int64_t last_timestamp = 0;
    double qpc_freq_ms = 1.0;
};

static VOID WINAPI EtwEventRecordCallback(PEVENT_RECORD pEventRecord) {
    if (!pEventRecord || !pEventRecord->UserContext) {
        return;
    }

    auto* ctx = reinterpret_cast<EtwSessionContext*>(pEventRecord->UserContext);
    if (!ctx->monitor) {
        return;
    }

    // Filtrar por el PID objetivo (si target_pid != 0)
    uint32_t event_pid = pEventRecord->EventHeader.ProcessId;
    if (ctx->target_pid != 0 && event_pid != ctx->target_pid) {
        return;
    }

    // Eventos DXGI Present:
    // 42: DXGIPresent_Start (DirectX 10/11/12)
    // 39: DXGIPresent_Stop
    // 40: DXGIPresentMPO_Start (Multiplane Overlay)
    USHORT event_id = pEventRecord->EventHeader.EventDescriptor.Id;
    if (event_id == 42 || event_id == 40 || event_id == 39) {
        int64_t current_ts = pEventRecord->EventHeader.TimeStamp.QuadPart;
        if (ctx->last_timestamp > 0 && current_ts > ctx->last_timestamp) {
            double dt_ms = static_cast<double>(current_ts - ctx->last_timestamp) / ctx->qpc_freq_ms;
            if (dt_ms > 0.1 && dt_ms <= 2000.0) {
                ctx->monitor->RecordFrameIntervalMs(dt_ms);
            }
        }
        ctx->last_timestamp = current_ts;
    }
}

std::wstring GetEtwSessionName() {
    return L"VegaFlowDxgi_" + std::to_wstring(GetCurrentProcessId());
}

} // anonymous namespace

FrameTelemetryMonitor::FrameTelemetryMonitor() {
    ring_buffer_.resize(kMaxSamplesWindow, 0.0);
}

FrameTelemetryMonitor::~FrameTelemetryMonitor() {
    StopMonitoring();
}

void FrameTelemetryMonitor::StartMonitoring(uint32_t target_pid) {
    if (running_.load(std::memory_order_relaxed)) {
        if (target_pid_ == target_pid) {
            return;
        }
        StopMonitoring();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_pid_ = target_pid;
    }

    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&FrameTelemetryMonitor::WorkerLoop, this);
}

void FrameTelemetryMonitor::StopMonitoring() {
    if (!running_.load(std::memory_order_relaxed)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return;
    }

    running_.store(false, std::memory_order_release);

    if (etw_session_active_.load(std::memory_order_relaxed)) {
        std::wstring session_name = GetEtwSessionName();
        size_t buf_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
        std::vector<uint8_t> prop_buf(buf_size, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(prop_buf.data());
        props->Wnode.BufferSize = static_cast<ULONG>(buf_size);
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        // ControlTraceW con EVENT_TRACE_CONTROL_STOP detiene inmediatamente el trace y desbloquea ProcessTrace
        ControlTraceW(0, session_name.c_str(), props, EVENT_TRACE_CONTROL_STOP);
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    etw_session_active_.store(false, std::memory_order_relaxed);
}

void FrameTelemetryMonitor::RecordFrameIntervalMs(double dt_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_buffer_.empty()) {
        ring_buffer_.resize(kMaxSamplesWindow, 0.0);
    }
    ring_buffer_[write_index_] = dt_ms;
    write_index_ = (write_index_ + 1) % kMaxSamplesWindow;
    if (sample_count_ < kMaxSamplesWindow) {
        sample_count_++;
    }
    total_frames_++;
}

void FrameTelemetryMonitor::ResetMetrics(uint32_t new_pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_buffer_.empty()) {
        ring_buffer_.resize(kMaxSamplesWindow, 0.0);
    } else {
        std::fill(ring_buffer_.begin(), ring_buffer_.end(), 0.0);
    }
    write_index_ = 0;
    sample_count_ = 0;
    total_frames_ = 0;
    if (new_pid != 0) {
        target_pid_ = new_pid;
    }
}

FrameMetricsSnapshot FrameTelemetryMonitor::GetSnapshot() const {
    std::vector<double> samples;
    uint32_t pid = 0;
    uint64_t total_frames = 0;
    bool etw = etw_session_active_.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pid = target_pid_;
        total_frames = total_frames_;
        samples.reserve(sample_count_);
        if (sample_count_ < kMaxSamplesWindow) {
            samples.insert(samples.end(), ring_buffer_.begin(), ring_buffer_.begin() + sample_count_);
        } else {
            // Buffer circular lleno: el orden cronológico inicia en write_index_
            samples.insert(samples.end(), ring_buffer_.begin() + write_index_, ring_buffer_.end());
            samples.insert(samples.end(), ring_buffer_.begin(), ring_buffer_.begin() + write_index_);
        }
    }

    return ComputeMetricsFromSamples(samples, pid, total_frames, etw);
}

std::string FrameTelemetryMonitor::FormatSessionReport(const std::string& game_name) const {
    FrameMetricsSnapshot snap = GetSnapshot();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "========================================================\n";
    oss << " VEGAFLOW TELEMETRY REPORT: " << game_name << "\n";
    oss << "========================================================\n";
    oss << " Target PID            : " << snap.target_pid << "\n";
    oss << " Total Frames Captured : " << snap.total_frames_captured << "\n";
    oss << " Telemetry Provider    : " << (snap.etw_active ? "ETW (Microsoft-Windows-DXGI)" : "DWM/QPC Hybrid Timing") << "\n";
    oss << " Telemetry Status      : " << (snap.valid ? "VALID" : "INSUFFICIENT DATA") << "\n";
    oss << "--------------------------------------------------------\n";
    oss << " Average FPS           : " << snap.avg_fps << " FPS\n";
    oss << " 1% Low FPS            : " << snap.fps_1_percent_low << " FPS\n";
    oss << " 0.1% Low FPS          : " << snap.fps_0_1_percent_low << " FPS\n";
    oss << "--------------------------------------------------------\n";
    oss << " Average Frametime     : " << snap.avg_frametime_ms << " ms\n";
    oss << " Frametime StdDev      : " << snap.frametime_stddev_ms << " ms\n";
    oss << " Min Frametime         : " << snap.min_frametime_ms << " ms\n";
    oss << " Max Frametime         : " << snap.max_frametime_ms << " ms\n";
    oss << "========================================================\n";
    return oss.str();
}

bool FrameTelemetryMonitor::SaveSessionReportToFile(const std::string& game_name, const std::string& file_path) const {
    try {
        std::filesystem::path p(file_path);
        std::ofstream out(p, std::ios::app);
        if (!out.is_open() && p.has_parent_path()) {
            CreateDirectoryW(p.parent_path().wstring().c_str(), nullptr);
            out.clear();
            out.open(p, std::ios::app);
        }

        if (!out.is_open()) {
            return false;
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char time_buf[64] = {0};
        snprintf(time_buf, sizeof(time_buf), "[%04u-%02u-%02u %02u:%02u:%02u] ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        out << time_buf << "Session Report: " << game_name << "\n";
        out << FormatSessionReport(game_name) << "\n\n";
        out.flush();

        return !out.fail();
    } catch (...) {
        return false;
    }
}

FrameMetricsSnapshot FrameTelemetryMonitor::ComputeMetricsFromSamples(
    const std::vector<double>& frametimes_ms,
    uint32_t pid,
    uint64_t total_frames,
    bool etw_active)
{
    FrameMetricsSnapshot snapshot{};
    snapshot.target_pid = pid;
    snapshot.total_frames_captured = total_frames;
    snapshot.etw_active = etw_active;

    // 1. Filtrar valores fuera de rango (<= 0.1 ms o > 2000.0 ms)
    std::vector<double> valid_samples;
    valid_samples.reserve(frametimes_ms.size());
    for (double dt : frametimes_ms) {
        if (dt > 0.1 && dt <= 2000.0) {
            valid_samples.push_back(dt);
        }
    }

    if (valid_samples.empty()) {
        snapshot.valid = false;
        return snapshot;
    }

    if (snapshot.total_frames_captured == 0) {
        snapshot.total_frames_captured = static_cast<uint64_t>(valid_samples.size());
    }
    snapshot.valid = true;

    // 2. Promedio de frametime y FPS
    const double n = static_cast<double>(valid_samples.size());
    double sum_frametime = 0.0;
    for (double dt : valid_samples) {
        sum_frametime += dt;
    }
    snapshot.avg_frametime_ms = sum_frametime / n;
    snapshot.avg_fps = (snapshot.avg_frametime_ms > 0.0) ? (1000.0 / snapshot.avg_frametime_ms) : 0.0;

    // 3. Desviación estándar del tiempo de cuadro (frametime_stddev_ms)
    double variance_sum = 0.0;
    for (double dt : valid_samples) {
        double diff = dt - snapshot.avg_frametime_ms;
        variance_sum += diff * diff;
    }
    snapshot.frametime_stddev_ms = std::sqrt(variance_sum / n);

    // 4. Mínimo, Máximo, y Percentiles ordenando una copia
    std::sort(valid_samples.begin(), valid_samples.end());
    snapshot.min_frametime_ms = valid_samples.front();
    snapshot.max_frametime_ms = valid_samples.back();

    const size_t sample_size = valid_samples.size();

    // 1% Low FPS: promedio del 1% de cuadros más lentos (índice floor(0.99 * N) hasta N - 1)
    size_t idx_99 = static_cast<size_t>(std::floor(0.99 * static_cast<double>(sample_size)));
    if (idx_99 >= sample_size) {
        idx_99 = sample_size - 1;
    }
    double sum_slowest_1_pct = 0.0;
    size_t count_1_pct = 0;
    for (size_t i = idx_99; i < sample_size; ++i) {
        sum_slowest_1_pct += valid_samples[i];
        count_1_pct++;
    }
    double frametime_99th = (count_1_pct > 0) ? (sum_slowest_1_pct / static_cast<double>(count_1_pct)) : valid_samples[idx_99];
    snapshot.fps_1_percent_low = (frametime_99th > 0.0) ? (1000.0 / frametime_99th) : 0.0;

    // 0.1% Low FPS: promedio del 0.1% de cuadros más lentos (índice floor(0.999 * N) hasta N - 1)
    size_t idx_99_9 = static_cast<size_t>(std::floor(0.999 * static_cast<double>(sample_size)));
    if (idx_99_9 >= sample_size) {
        idx_99_9 = sample_size - 1;
    }
    double sum_slowest_0_1_pct = 0.0;
    size_t count_0_1_pct = 0;
    for (size_t i = idx_99_9; i < sample_size; ++i) {
        sum_slowest_0_1_pct += valid_samples[i];
        count_0_1_pct++;
    }
    double frametime_99_9th = (count_0_1_pct > 0) ? (sum_slowest_0_1_pct / static_cast<double>(count_0_1_pct)) : valid_samples[idx_99_9];
    snapshot.fps_0_1_percent_low = (frametime_99_9th > 0.0) ? (1000.0 / frametime_99_9th) : 0.0;

    return snapshot;
}

void FrameTelemetryMonitor::WorkerLoop() {
    std::wstring session_name = GetEtwSessionName();
    size_t buf_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    std::vector<uint8_t> prop_buf(buf_size, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(prop_buf.data());
    props->Wnode.BufferSize = static_cast<ULONG>(buf_size);
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // Resolución QPC
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    props->LogFileNameOffset = 0;

    // Detener cualquier sesión previa huérfana con el mismo nombre
    ControlTraceW(0, session_name.c_str(), props, EVENT_TRACE_CONTROL_STOP);

    LARGE_INTEGER qpc_freq;
    QueryPerformanceFrequency(&qpc_freq);
    double qpc_freq_ms = (qpc_freq.QuadPart > 0) ? (static_cast<double>(qpc_freq.QuadPart) / 1000.0) : 10000.0;

    uint32_t current_target_pid = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_target_pid = target_pid_;
    }

    EtwSessionContext etw_ctx{};
    etw_ctx.monitor = this;
    etw_ctx.target_pid = current_target_pid;
    etw_ctx.last_timestamp = 0;
    etw_ctx.qpc_freq_ms = qpc_freq_ms;

    TRACEHANDLE session_handle = 0;
    ULONG status = StartTraceW(&session_handle, session_name.c_str(), props);
    bool etw_active = false;

    if (status == ERROR_SUCCESS) {
        ENABLE_TRACE_PARAMETERS params{};
        params.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;

        status = EnableTraceEx2(
            session_handle,
            &kDxgiProviderGuid,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER,
            TRACE_LEVEL_INFORMATION,
            0,
            0,
            0,
            &params
        );

        if (status == ERROR_SUCCESS) {
            EVENT_TRACE_LOGFILEW log_file{};
            wchar_t session_name_buf[256] = {0};
            wcsncpy_s(session_name_buf, session_name.c_str(), _TRUNCATE);
            log_file.LoggerName = session_name_buf;
            log_file.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
            log_file.EventRecordCallback = &EtwEventRecordCallback;
            log_file.Context = &etw_ctx;

            TRACEHANDLE trace_handle = OpenTraceW(&log_file);
            if (trace_handle != (TRACEHANDLE)INVALID_PROCESSTRACE_HANDLE) {
                etw_session_active_.store(true, std::memory_order_release);
                etw_active = true;

                // ProcessTrace bloquea hasta que ControlTraceW(..., EVENT_TRACE_CONTROL_STOP) es invocado
                ProcessTrace(&trace_handle, 1, nullptr, nullptr);

                CloseTrace(trace_handle);
                etw_session_active_.store(false, std::memory_order_release);
            }
        }

        ControlTraceW(session_handle, session_name.c_str(), props, EVENT_TRACE_CONTROL_STOP);
    }

    // Muestreador de respaldo (Fallback): cuando no hay elevación ETW o mientras el monitor esté activo
    if (!etw_active && running_.load(std::memory_order_relaxed)) {
        HMODULE dwmapi_mod = LoadLibraryW(L"dwmapi.dll");
        typedef HRESULT (WINAPI *PFN_DwmGetCompositionTimingInfo)(HWND, DWM_TIMING_INFO*);
        auto pfnDwmTiming = dwmapi_mod ? reinterpret_cast<PFN_DwmGetCompositionTimingInfo>(
            reinterpret_cast<void*>(GetProcAddress(dwmapi_mod, "DwmGetCompositionTimingInfo"))) : nullptr;

        uint64_t last_cFrame = 0;
        LARGE_INTEGER last_qpc;
        QueryPerformanceCounter(&last_qpc);

        while (running_.load(std::memory_order_relaxed)) {
            uint32_t pid = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pid = target_pid_;
            }

            bool frame_recorded = false;
            if (pfnDwmTiming) {
                DWM_TIMING_INFO dti{};
                dti.cbSize = sizeof(dti);
                if (pfnDwmTiming(NULL, &dti) == S_OK) {
                    if (last_cFrame != 0 && dti.cFrame > last_cFrame) {
                        uint64_t frame_delta = dti.cFrame - last_cFrame;
                        LARGE_INTEGER cur_qpc;
                        QueryPerformanceCounter(&cur_qpc);
                        double elapsed_ms = static_cast<double>(cur_qpc.QuadPart - last_qpc.QuadPart) / qpc_freq_ms;
                        double frame_time_ms = elapsed_ms / static_cast<double>(frame_delta);

                        bool process_active = true;
                        if (pid != 0) {
                            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                            if (hProc) {
                                DWORD exit_code = 0;
                                if (GetExitCodeProcess(hProc, &exit_code) && exit_code != STILL_ACTIVE) {
                                    process_active = false;
                                }
                                CloseHandle(hProc);
                            } else {
                                process_active = false;
                            }
                        }

                        if (process_active && frame_time_ms > 0.1 && frame_time_ms <= 2000.0) {
                            RecordFrameIntervalMs(frame_time_ms);
                        }
                        last_qpc = cur_qpc;
                        last_cFrame = dti.cFrame;
                        frame_recorded = true;
                    } else if (last_cFrame == 0) {
                        last_cFrame = dti.cFrame;
                        QueryPerformanceCounter(&last_qpc);
                        frame_recorded = true;
                    }
                }
            }

            if (!frame_recorded && pid != 0) {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProc) {
                    DWORD exit_code = 0;
                    if (GetExitCodeProcess(hProc, &exit_code) && exit_code == STILL_ACTIVE) {
                        LARGE_INTEGER cur_qpc;
                        QueryPerformanceCounter(&cur_qpc);
                        double elapsed_ms = static_cast<double>(cur_qpc.QuadPart - last_qpc.QuadPart) / qpc_freq_ms;
                        if (elapsed_ms >= 1.0) {
                            if (elapsed_ms > 0.1 && elapsed_ms <= 2000.0) {
                                RecordFrameIntervalMs(elapsed_ms);
                            }
                            last_qpc = cur_qpc;
                        }
                    }
                    CloseHandle(hProc);
                }
            }

            // Pausas cortas (~4 ms en fragmentos de 1 ms) para responder a StopMonitoring en < 5 ms
            for (int i = 0; i < 4 && running_.load(std::memory_order_relaxed); ++i) {
                Sleep(1);
            }
        }

        if (dwmapi_mod) {
            FreeLibrary(dwmapi_mod);
        }
    }
}

} // namespace vegaflow
