#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr int kDirectXButton = 1;
constexpr int kVulkanButton = 2;
constexpr DWORD kDialogStyle = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_BORDER;
constexpr DWORD kDialogExStyle = WS_EX_DLGMODALFRAME;

HFONT MessageFont() {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        if (HFONT font = CreateFontIndirectW(&metrics.lfMessageFont)) return font;
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        const HFONT font = MessageFont();
        const HWND icon = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON,
                                        24, 24, 40, 40, window, nullptr, nullptr, nullptr);
        SendMessageW(icon, STM_SETICON,
                     reinterpret_cast<WPARAM>(LoadIconW(nullptr, MAKEINTRESOURCEW(32516))), 0);
        const HWND text = CreateWindowW(L"STATIC",
                                        L"그래픽 API를 선택하세요.\n나중에 설정에서 바꿀 수 있습니다.",
                                        WS_CHILD | WS_VISIBLE | SS_LEFT, 82, 18, 500, 60,
                                        window, nullptr, nullptr, nullptr);
        const HWND directx = CreateWindowW(L"BUTTON", L"DirectX 12",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                           220, 96, 170, 36, window,
                                           reinterpret_cast<HMENU>(kDirectXButton), nullptr, nullptr);
        const HWND vulkan = CreateWindowW(L"BUTTON", L"Vulkan",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                          410, 96, 170, 36, window,
                                          reinterpret_cast<HMENU>(kVulkanButton), nullptr, nullptr);
        SendMessageW(icon, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(directx, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(vulkan, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == kDirectXButton || LOWORD(wparam) == kVulkanButton) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wparam), GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    constexpr wchar_t kClassName[] = L"PT_RenderingApiDialogPreview";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DialogProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));
    window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window_class.lpszClassName = kClassName;
    if (!RegisterClassW(&window_class)) return 1;

    RECT client_bounds{0, 0, 620, 150};
    AdjustWindowRectEx(&client_bounds, kDialogStyle, FALSE, kDialogExStyle);
    HWND window = CreateWindowExW(kDialogExStyle, kClassName, L"렌더링 API", kDialogStyle,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  client_bounds.right - client_bounds.left,
                                  client_bounds.bottom - client_bounds.top,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    RECT work{};
    RECT bounds{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    GetWindowRect(window, &bounds);
    SetWindowPos(window, HWND_TOP,
                 work.left + ((work.right - work.left) - (bounds.right - bounds.left)) / 2,
                 work.top + ((work.bottom - work.top) - (bounds.bottom - bounds.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

    MSG event{};
    while (GetMessageW(&event, nullptr, 0, 0) > 0) {
        TranslateMessage(&event);
        DispatchMessageW(&event);
    }
    return 0;
}
