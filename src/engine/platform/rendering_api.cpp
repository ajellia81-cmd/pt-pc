#include "engine/core/ascii.h"
#include "engine/platform/rendering_api.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "engine/platform/os.h"

namespace pt {

namespace {

#ifdef _WIN32

struct RenderingApiDialog {
    std::wstring title;
    std::wstring message;
    HFONT font = nullptr;
    bool owns_font = false;
    int pressed = 0;
};

std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

constexpr int kDirectXButton = 1;
constexpr int kVulkanButton = 2;
constexpr DWORD kDialogStyle = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_BORDER;
constexpr DWORD kDialogExStyle = WS_EX_DLGMODALFRAME;

HFONT MessageFont(bool& owns_font) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        if (HFONT font = CreateFontIndirectW(&metrics.lfMessageFont)) {
            owns_font = true;
            return font;
        }
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

LRESULT CALLBACK RenderingApiDialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* dialog = reinterpret_cast<RenderingApiDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        dialog = static_cast<RenderingApiDialog*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    switch (message) {
    case WM_CREATE: {
        if (!dialog) return -1;
        dialog->font = MessageFont(dialog->owns_font);
        const HWND icon = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON, 24, 24, 40, 40, window, nullptr, nullptr, nullptr);
        SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr, MAKEINTRESOURCEW(32516))), 0);
        const HWND text = CreateWindowW(L"STATIC", dialog->message.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, 106, 18, 500, 60, window, nullptr, nullptr, nullptr);
        const HWND directx = CreateWindowW(L"BUTTON", L"DirectX 12", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                            220, 96, 170, 36, window, reinterpret_cast<HMENU>(kDirectXButton), nullptr, nullptr);
        const HWND vulkan = CreateWindowW(L"BUTTON", L"Vulkan", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                           410, 96, 170, 36, window, reinterpret_cast<HMENU>(kVulkanButton), nullptr, nullptr);
        for (const HWND control : {icon, text, directx, vulkan}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(dialog->font), TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == kDirectXButton || LOWORD(wparam) == kVulkanButton) {
            dialog->pressed = static_cast<int>(LOWORD(wparam));
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        if (dialog && dialog->owns_font) {
            DeleteObject(dialog->font);
            dialog->font = nullptr;
            dialog->owns_font = false;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wparam), GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

AskAnswer AskRenderingApiWindows(std::string_view title, std::string_view message, RenderingApi& out) {
    RenderingApiDialog dialog{Wide(title), Wide(message)};
    if (dialog.title.empty() || dialog.message.empty()) return AskAnswer::NotShown;

    constexpr wchar_t kClassName[] = L"PT_RenderingApiDialog";
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = RenderingApiDialogProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));
    window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window_class.lpszClassName = kClassName;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return AskAnswer::NotShown;

    HWND owner = GetActiveWindow();
    const bool restore_owner = owner && IsWindowEnabled(owner);
    if (restore_owner) EnableWindow(owner, FALSE);
    // The requested size is the client area: it must include both the explanation
    // and the buttons even when Windows adds a large caption at high DPI.
    RECT client_bounds{0, 0, 620, 150};
    AdjustWindowRectEx(&client_bounds, kDialogStyle, FALSE, kDialogExStyle);
    HWND window = CreateWindowExW(kDialogExStyle, kClassName, dialog.title.c_str(), kDialogStyle,
                                  CW_USEDEFAULT, CW_USEDEFAULT, client_bounds.right - client_bounds.left,
                                  client_bounds.bottom - client_bounds.top, owner, nullptr, instance, &dialog);
    if (!window) {
        if (restore_owner) EnableWindow(owner, TRUE);
        return AskAnswer::NotShown;
    }
    RECT work{};
    RECT bounds{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    GetWindowRect(window, &bounds);
    SetWindowPos(window, HWND_TOP, work.left + ((work.right - work.left) - (bounds.right - bounds.left)) / 2,
                 work.top + ((work.bottom - work.top) - (bounds.bottom - bounds.top)) / 2, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    MSG event{};
    while (IsWindow(window)) {
        const int result = GetMessageW(&event, nullptr, 0, 0);
        if (result <= 0) break;
        if (!IsDialogMessageW(window, &event)) {
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
    }
    if (restore_owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (dialog.pressed == kDirectXButton) {
        out = RenderingApi::D3D12;
        return AskAnswer::Chosen;
    }
    if (dialog.pressed == kVulkanButton) {
        out = RenderingApi::Vulkan;
        return AskAnswer::Chosen;
    }
    return AskAnswer::Closed;
}

#endif

}

std::string_view RenderingApiKey(RenderingApi api) {
    return api == RenderingApi::D3D12 ? "d3d12" : "vulkan";
}

std::string_view RenderingApiName(RenderingApi api) {
    return api == RenderingApi::D3D12 ? "DirectX 12" : "Vulkan";
}

bool ParseRenderingApi(std::string_view text, RenderingApi& out) {
    std::string v(text);
    v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c) != 0; }), v.end());
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return pt::AsciiLower(static_cast<char>(c)); });
    if (v == "vulkan") {
        out = RenderingApi::Vulkan;
        return true;
    }
    if (v == "d3d12" || v == "dx12" || v == "directx12") {
        out = RenderingApi::D3D12;
        return true;
    }
    return false;
}

bool RenderingApiSelectable() {
#ifdef _WIN32
#if defined(PT_D3D12_SELECTABLE)
    return true;
#else
    return os::GetEnv("PT_RENDERING_API_UI") == "1";
#endif
#else
    return false;
#endif
}

namespace {
bool g_d3d12_game = false;
}

bool D3D12RunsGame() {
#if defined(_WIN32)
    return g_d3d12_game;
#else
    return false;
#endif
}

void SetD3D12RunsGame(bool available) {
    g_d3d12_game = available;
}

namespace {
bool (*g_d3d12_probe)(std::string&) = nullptr;
int g_d3d12_usable = -1;
std::string g_d3d12_reason;
}

void SetD3D12Probe(bool (*probe)(std::string& reason)) {
    g_d3d12_probe = probe;
    g_d3d12_usable = -1;
}

void BlockD3D12(std::string reason) {
    g_d3d12_usable = 0;
    g_d3d12_reason = std::move(reason);
}

void SkipD3D12Probe() {
    if (g_d3d12_usable < 0) {
        g_d3d12_usable = 1;
        g_d3d12_reason.clear();
    }
}

bool D3D12Usable(std::string& reason) {
    if (!D3D12RunsGame()) {
        reason = "this build draws the game with Vulkan only";
        return false;
    }
    if (!g_d3d12_probe) {
        reason.clear();
        return true;
    }
    if (g_d3d12_usable < 0) {
        g_d3d12_usable = g_d3d12_probe(g_d3d12_reason) ? 1 : 0;
    }
    reason = g_d3d12_usable ? std::string() : g_d3d12_reason;
    return g_d3d12_usable == 1;
}

RenderingApi ChooseRenderingApi(std::string_view cli, std::string_view setting, std::string& note) {
    note.clear();
    const bool from_cli = !cli.empty();
    const std::string_view asked = from_cli ? cli : setting;
    const char* source = from_cli ? "--backend" : "pt.ini [display] rendering_api";
    RenderingApi api = RenderingApi::Vulkan;
    if (!asked.empty() && !ParseRenderingApi(asked, api)) {
        note = std::format("{} = '{}' is not vulkan or d3d12: using Vulkan", source, asked);
        return RenderingApi::Vulkan;
    }
    if (api == RenderingApi::D3D12) {
#ifndef _WIN32
        note = std::format("{} asks for DirectX 12, which exists on Windows only: using Vulkan", source);
        return RenderingApi::Vulkan;
#else
        if (!D3D12RunsGame()) {
            note = std::format("{} asks for DirectX 12, but this build draws the game with Vulkan only (the DirectX 12 backend is not "
                               "complete): using Vulkan",
                               source);
            return RenderingApi::Vulkan;
        }
        std::string reason;
        if (!D3D12Usable(reason)) {
            note = std::format("{} asks for DirectX 12, which cannot run on this PC ({}): using Vulkan", source, reason);
            return RenderingApi::Vulkan;
        }
#endif
    }
    return api;
}

AskAnswer AskRenderingApi(std::string_view title, std::string_view message, RenderingApi& out) {
#ifdef _WIN32
    if (const AskAnswer answer = AskRenderingApiWindows(title, message, out); answer != AskAnswer::NotShown) {
        return answer;
    }
#endif
    const std::string t(title);
    const std::string m(message);
    const SDL_MessageBoxButtonData buttons[] = {
        {0, 1, "DirectX 12"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, "Vulkan"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    data.title = t.c_str();
    data.message = m.c_str();
    data.numbuttons = 2;
    data.buttons = buttons;
    int pressed = -1;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        return AskAnswer::NotShown;
    }
    if (pressed != 1 && pressed != 2) {
        return AskAnswer::Closed;
    }
    out = pressed == 1 ? RenderingApi::D3D12 : RenderingApi::Vulkan;
    return AskAnswer::Chosen;
}

}
