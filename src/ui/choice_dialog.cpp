#include "ui/choice_dialog.hpp"

#include <algorithm>
#include <cstring>

namespace vegaflow::ui {

namespace {

constexpr wchar_t kDialogClass[] = L"VegaFlowChoiceDialog";
constexpr int kBtnBaseId = 5000;
constexpr int kBtnW = 170;
constexpr int kBtnH = 34;
constexpr int kMargin = 16;

struct DialogState {
    const std::vector<DialogButton>* buttons = nullptr;
    std::wstring body;
    int cancel_result = 0;
    bool monospace = false;
    int result = 0;
    bool done = false;
    HFONT font = nullptr;
    HFONT mono = nullptr;
};

int g_open = 0;

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
            return TRUE;
        case WM_COMMAND: {
            const int idx = LOWORD(wp) - kBtnBaseId;
            if (!st || HIWORD(wp) != BN_CLICKED || idx < 0 || idx >= static_cast<int>(st->buttons->size())) break;
            const DialogButton& b = (*st->buttons)[static_cast<size_t>(idx)];
            if (b.on_click) b.on_click();
            if (b.closes) {
                st->result = b.result;
                st->done = true;
            }
            return 0;
        }
        case WM_CLOSE: // the X
            if (st) {
                st->result = st->cancel_result;
                st->done = true;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool ChoiceDialogOpen() { return g_open > 0; }

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) return false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return false;
    }
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(mem);
    bool ok = false;
    if (OpenClipboard(owner)) {
        EmptyClipboard();
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr; // the system owns the memory after a success
        CloseClipboard();
    }
    if (!ok) GlobalFree(mem);
    return ok;
}

int ShowChoiceDialog(HWND owner, const std::wstring& title, const std::wstring& body, const std::vector<DialogButton>& buttons, int cancel_result, bool monospace) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kDialogClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return cancel_result;
        registered = true;
    }

    DialogState st;
    st.buttons = &buttons;
    st.body = body;
    st.cancel_result = cancel_result;
    st.monospace = monospace;
    st.result = cancel_result;
    st.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    st.mono = monospace ? CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas")
                          : nullptr;

    const int n = static_cast<int>(buttons.size());
    const int width = std::max(520, n * (kBtnW + 8) + 2 * kMargin);
    const int height = monospace ? 520 : 300;
    RECT ow = {0, 0, 1280, 720};
    if (owner) GetWindowRect(owner, &ow);
    const int x = ow.left + std::max(0, (static_cast<int>(ow.right - ow.left) - width) / 2);
    const int y = ow.top + std::max(0, (static_cast<int>(ow.bottom - ow.top) - height) / 3);

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClass, title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, width, height, owner,
                               nullptr, inst, &st);
    if (!dlg) {
        if (st.font) DeleteObject(st.font);
        if (st.mono) DeleteObject(st.mono);
        return cancel_result;
    }
    RECT client;
    GetClientRect(dlg, &client);
    const int cw = client.right, ch = client.bottom;

    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st.body.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, kMargin, kMargin, cw - 2 * kMargin,
                                ch - 3 * kMargin - kBtnH, dlg, nullptr, inst, nullptr);
    if (edit) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(st.mono ? st.mono : st.font), TRUE);
    int bx = cw - kMargin;
    for (int i = n - 1; i >= 0; --i) { // the first button is the left-most, the last the right-most
        bx -= kBtnW;
        HWND b = CreateWindowExW(0, L"BUTTON", buttons[static_cast<size_t>(i)].label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, bx,
                                 ch - kMargin - kBtnH, kBtnW, kBtnH, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnBaseId + i)), inst, nullptr);
        if (b) SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
        bx -= 8;
    }

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    ++g_open;
    MSG m = {};
    while (!st.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE && GetAncestor(m.hwnd, GA_ROOT) == dlg) {
            st.result = st.cancel_result;
            st.done = true;
            break;
        }
        if (IsDialogMessageW(dlg, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (!st.done && m.message == WM_QUIT) PostQuitMessage(static_cast<int>(m.wParam)); // the app is quitting: let its own loop see it
    --g_open;
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    if (st.font) DeleteObject(st.font);
    if (st.mono) DeleteObject(st.mono);
    return st.result;
}

} // namespace vegaflow::ui
