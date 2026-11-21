#include "ui/app_identity.hpp"
#include "ui/gui_components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vegaflow {

// =============================================================================
// TAB 2: UMA MEMORY & CPU CORES (WITH COMPACT BARS AND DIRECT BUTTON)
// =============================================================================
void RenderTabMemory(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                     int contentLeft, int contentRight, int contentTop, int contentBottom, int /*contentW*/) {
    if (!state) return;

    // Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"MEMORIA UNIFICADA UMA & ASIGNACIÓN DE NÚCLEOS", CLR_ACCENT_CYAN, state->hFontHeader);
    DrawTextAt(hdcMem, contentLeft, contentTop + 20, L"Gestión en tiempo real de Standby List y afinidad de procesos de fondo", CLR_TEXT_MUTED, state->hFontSmall);

    // -------------------------------------------------------------------------
    // 1. Top Card: UMA Memory Manager & Purge
    // -------------------------------------------------------------------------
    int topMemY = contentTop + 46;
    int topMemH = 138;
    RECT rcTopMem = { contentLeft, topMemY, contentRight, topMemY + topMemH };
    DrawRoundedCard(hdcMem, rcTopMem, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    DrawTextAt(hdcMem, rcTopMem.left + 16, rcTopMem.top + 12, L"GESTOR DE MEMORIA UNIFICADA (UMA)", CLR_ACCENT_CYAN, state->hFontBold);

    RECT rcDxgiBadge = { rcTopMem.right - 195, rcTopMem.top + 10, rcTopMem.right - 16, rcTopMem.top + 32 };
    wchar_t dxgiBadgeText[64];
    const double thresh = state->data.dxgi_pressure_threshold;
    const int threshPct = static_cast<int>(thresh * 100.0 + 0.5);
    bool highPressure = (thresh > 0.0 && state->data.dxgi_pressure_ratio >= thresh) || state->data.uma_guard_triggered_live;
    if (highPressure) {
        swprintf_s(dxgiBadgeText, L"ALERTA UMA: %.0f%% (>%d%%)", state->data.dxgi_pressure_ratio * 100.0, threshPct);
        DrawBadge(hdcMem, rcDxgiBadge, dxgiBadgeText, CLR_ACCENT_AMBER, RGB(36, 26, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    } else {
        swprintf_s(dxgiBadgeText, L"PRESIÓN DXGI: %.0f%% [OK]", state->data.dxgi_pressure_ratio * 100.0);
        DrawBadge(hdcMem, rcDxgiBadge, dxgiBadgeText, CLR_ACCENT_CYAN, RGB(20, 36, 44), CLR_ACCENT_CYAN, state->hFontSmallBold);
    }

    wchar_t mLine[256];
    if (state->data.has_amd_driver) {
        bool vramPoolActive = state->data.amd_vram_override_active || state->data.amd_vram_reported_mb >= 2048;
        uint32_t activePoolMb = state->data.amd_vram_reported_mb > 0 ? state->data.amd_vram_reported_mb : (vramPoolActive ? 4096 : 512);
        wchar_t poolBadgeText[64];
        swprintf_s(poolBadgeText, L"POOL TEXTURAS: %u MB", activePoolMb);
        RECT rcPoolBadge = { rcTopMem.right - 390, rcTopMem.top + 10, rcTopMem.right - 203, rcTopMem.top + 32 };
        if (vramPoolActive) {
            DrawBadge(hdcMem, rcPoolBadge, poolBadgeText, CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
        } else {
            DrawBadge(hdcMem, rcPoolBadge, poolBadgeText, CLR_TEXT_MUTED, RGB(33, 38, 48), CLR_CARD_BORDER, state->hFontSmallBold);
        }

        swprintf_s(mLine, L"Total: %llu MB   |   Libre: %llu MB   |   Standby: %llu MB   |   VRAM Local: %llu / %llu MB   |   Pool: %u MB",
                   static_cast<unsigned long long>(state->data.total_ram_mb),
                   static_cast<unsigned long long>(state->data.free_ram_mb),
                   static_cast<unsigned long long>(state->data.standby_ram_mb),
                   static_cast<unsigned long long>(state->data.vram_local_used_mb),
                   static_cast<unsigned long long>(state->data.vram_local_budget_mb),
                   activePoolMb);
    } else {
        swprintf_s(mLine, L"Total: %llu MB   |   Libre: %llu MB   |   Standby: %llu MB   |   VRAM Local: %llu / %llu MB",
                   static_cast<unsigned long long>(state->data.total_ram_mb),
                   static_cast<unsigned long long>(state->data.free_ram_mb),
                   static_cast<unsigned long long>(state->data.standby_ram_mb),
                   static_cast<unsigned long long>(state->data.vram_local_used_mb),
                   static_cast<unsigned long long>(state->data.vram_local_budget_mb));
    }
    DrawTextAt(hdcMem, rcTopMem.left + 16, rcTopMem.top + 38, mLine, CLR_TEXT_TITLE, state->hFontRegular);

    // Styled multi-segment progress bar
    RECT rcBar = { rcTopMem.left + 16, rcTopMem.top + 62, rcTopMem.right - 16, rcTopMem.top + 76 };
    double totalRam = static_cast<double>(state->data.total_ram_mb > 0 ? state->data.total_ram_mb : 16384);
    double freeFrac = static_cast<double>(state->data.free_ram_mb) / totalRam;
    double standbyFrac = static_cast<double>(state->data.standby_ram_mb) / totalRam;
    double modFrac = static_cast<double>(state->data.modified_ram_mb) / totalRam;
    double usedFrac = std::max<double>(0.0, 1.0 - (freeFrac + standbyFrac + modFrac));

    std::vector<std::pair<double, COLORREF>> segments = {
        { usedFrac, CLR_ACCENT_CYAN },
        { standbyFrac, CLR_ACCENT_PURPLE },
        { modFrac, CLR_ACCENT_AMBER },
        { freeFrac, CLR_ACCENT_GREEN }
    };
    DrawMultiSegmentBar(hdcMem, rcBar, segments, CLR_BAR_BG, CLR_CARD_BORDER);

    // Bottom line: last purge info + direct button
    int botRowY = rcTopMem.top + 88;
    if (state->data.last_purge_after_free_mb > 0) {
        wchar_t repBuf[256];
        swprintf_s(repBuf, L"Última purga: %llu MB libres  |  Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral %d%%)",
                   static_cast<unsigned long long>(state->data.last_purge_after_free_mb),
                   state->data.uma_guard_interventions,
                   state->data.dxgi_pressure_ratio * 100.0,
                   threshPct);
        DrawTextAt(hdcMem, rcTopMem.left + 16, botRowY + 10, repBuf, CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        wchar_t repBuf[256];
        swprintf_s(repBuf, L"Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral %d%%)",
                   state->data.uma_guard_interventions,
                   state->data.dxgi_pressure_ratio * 100.0,
                   threshPct);
        DrawTextAt(hdcMem, rcTopMem.left + 16, botRowY + 10, repBuf,
                   highPressure ? CLR_ACCENT_AMBER : CLR_TEXT_MUTED,
                   state->hFontSmall);
    }

    RECT rcPurgeBtn = { rcTopMem.right - 260, botRowY, rcTopMem.right - 16, botRowY + 36 };
    bool isPurgeHov = (state->hovered_id == ID_ACT_PURGE_RAM);
    DrawActionButton(hdcMem, rcPurgeBtn, L"VACIAR STANDBY RAM AHORA", isPurgeHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_PURGE_RAM, state);

    // -------------------------------------------------------------------------
    // 2. Bottom Card: CPU Visual Map & Process Isolation
    // -------------------------------------------------------------------------
    int botCpuY = topMemY + topMemH + 12;
    RECT rcBotCpu = { contentLeft, botCpuY, contentRight, contentBottom };
    DrawRoundedCard(hdcMem, rcBotCpu, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    DrawTextAt(hdcMem, rcBotCpu.left + 16, rcBotCpu.top + 12, L"MAPA DE TOPOLOGÍA CPU & AISLAMIENTO (CPU SETS)", CLR_ACCENT_CYAN, state->hFontBold);

    // Compact legend
    DrawTextAt(hdcMem, rcBotCpu.left + 16, rcBotCpu.top + 34,
               L"[ ■ Juego (Cian) ]    [ ■ Segundo Plano (Morado) ]    [ ■ DPC / ISR (Ámbar) ]    [ ■ Estándar ]",
               CLR_TEXT_MUTED, state->hFontSmall);

    uint32_t totalThreads = state->data.logical_processors > 0 ? state->data.logical_processors : 12;
    int threadsPerRow = std::min<uint32_t>(12, std::max<uint32_t>(6, (totalThreads + 1) / 2));
    int boxW = 86;
    int boxH = 46;
    int gapX = 10;
    int gapY = 8;
    int startX = rcBotCpu.left + 16;
    int startY = rcBotCpu.top + 56;

    uint32_t minSetId = UINT32_MAX;
    for (uint32_t gid : state->data.game_cpu_sets) {
        if (gid < minSetId) minSetId = gid;
    }
    for (uint32_t bid : state->data.background_cpu_sets) {
        if (bid < minSetId) minSetId = bid;
    }
    uint32_t baseSetId = (minSetId != UINT32_MAX && minSetId >= 256) ? 256 : 0;

    for (uint32_t t = 0; t < totalThreads; ++t) {
        int row = static_cast<int>(t / threadsPerRow);
        int col = static_cast<int>(t % threadsPerRow);
        RECT rcCoreBox = { startX + col * (boxW + gapX), startY + row * (boxH + gapY), startX + col * (boxW + gapX) + boxW, startY + row * (boxH + gapY) + boxH };

        uint32_t expectedSetId = baseSetId + t;
        bool isGameSet = false;
        for (uint32_t gid : state->data.game_cpu_sets) {
            if (gid == t || gid == expectedSetId) { isGameSet = true; break; }
        }
        bool isBgSet = false;
        for (uint32_t bid : state->data.background_cpu_sets) {
            if (bid == t || bid == expectedSetId) { isBgSet = true; break; }
        }

        COLORREF boxBorder;
        std::wstring roleLabel;

        if (isGameSet) {
            boxBorder = CLR_ACCENT_CYAN;
            roleLabel = L"JUEGO";
        } else if (isBgSet) {
            boxBorder = CLR_ACCENT_PURPLE;
            roleLabel = L"FONDO";
        } else if (t < 2) {
            boxBorder = CLR_ACCENT_AMBER;
            roleLabel = L"DPC / ISR";
        } else {
            boxBorder = CLR_CARD_BORDER;
            roleLabel = L"ESTANDAR";
        }

        DrawRoundedCard(hdcMem, rcCoreBox, CLR_BAR_BG, boxBorder, 6);

        wchar_t threadTitle[32];
        swprintf_s(threadTitle, L"Hilo #%u", t);
        DrawTextAt(hdcMem, rcCoreBox.left + 8, rcCoreBox.top + 6, threadTitle, CLR_TEXT_TITLE, state->hFontSmallBold);
        DrawTextAt(hdcMem, rcCoreBox.left + 8, rcCoreBox.top + 24, roleLabel, boxBorder, state->hFontSmallBold);
    }

    int afterBoxesY = startY + 2 * (boxH + gapY) + 12;
    wchar_t bgStatsBuf[256];
    swprintf_s(bgStatsBuf, L"Procesos en segundo plano detectados: %u   |   Procesos aislados en núcleos secundarios: %u",
               state->data.noisy_bg_processes_count, state->data.isolated_bg_processes_count);
    DrawTextAt(hdcMem, rcBotCpu.left + 16, afterBoxesY, bgStatsBuf, CLR_TEXT_BODY, state->hFontRegular);

    // Clean action buttons
    int actBtnsY = afterBoxesY + 24;
    RECT rcIsolateBtn = { rcBotCpu.left + 16, actBtnsY, rcBotCpu.left + 330, actBtnsY + 38 };
    bool isIsoHov = (state->hovered_id == ID_ACT_ISOLATE_BG);
    DrawActionButton(hdcMem, rcIsolateBtn, L"Aislar Procesos en 2º Plano", isIsoHov, CLR_ACCENT_PURPLE, state->hFontSmallBold, ID_ACT_ISOLATE_BG, state);

    RECT rcRestoreCpuBtn = { rcIsolateBtn.right + 12, actBtnsY, rcIsolateBtn.right + 310, actBtnsY + 38 };
    bool isRestHov = (state->hovered_id == ID_ACT_RESTORE_CPU);
    DrawActionButton(hdcMem, rcRestoreCpuBtn, L"Restaurar Afinidad de CPU", isRestHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_RESTORE_CPU, state);
}

} // namespace vegaflow

