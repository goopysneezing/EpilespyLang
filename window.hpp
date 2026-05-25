#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <thread>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>

// Helper to convert hex string or named color to COLORREF
inline COLORREF parseColor(const std::string& colorName) {
    static const std::unordered_map<std::string, COLORREF> colorMap = {
        {"black", RGB(0, 0, 0)},
        {"white", RGB(255, 255, 255)},
        {"red", RGB(255, 0, 0)},
        {"green", RGB(0, 255, 0)},
        {"blue", RGB(0, 0, 255)},
        {"yellow", RGB(255, 255, 0)},
        {"purple", RGB(128, 0, 128)},
        {"orange", RGB(255, 165, 0)},
        {"cyan", RGB(0, 255, 255)},
        {"magenta", RGB(255, 0, 255)},
        {"gray", RGB(128, 128, 128)},
        {"lightgray", RGB(211, 211, 211)},
        {"darkgray", RGB(169, 169, 169)}
    };

    auto it = colorMap.find(colorName);
    if (it != colorMap.end()) return it->second;

    // Check if it is a hex color e.g. #ff0000
    if (colorName.size() == 7 && colorName[0] == '#') {
        try {
            int r = std::stoi(colorName.substr(1, 2), nullptr, 16);
            int g = std::stoi(colorName.substr(3, 2), nullptr, 16);
            int b = std::stoi(colorName.substr(5, 2), nullptr, 16);
            return RGB(r, g, b);
        } catch (...) {}
    }

    return RGB(255, 255, 255); // default to white
}

class WindowInstance {
public:
    HWND hwnd = nullptr;
    std::thread winThread;
    std::mutex mtx;
    bool isWindowOpen = false;
    int width = 0;
    int height = 0;
    std::string title;
    
    // Offscreen rendering resources
    HDC hdcMem = nullptr;
    HBITMAP hbmMem = nullptr;
    HBITMAP hbmOld = nullptr;

    // Input state
    double mouseX = 0;
    double mouseY = 0;
    bool mouseLeft = false;
    bool keys[256] = {false};

    WindowInstance(const std::string& title, int w, int h)
        : title(title), width(w), height(h) {
        isWindowOpen = true;
        winThread = std::thread(&WindowInstance::run, this);

        // Wait until HWND is created
        while (hwnd == nullptr && isWindowOpen) {
            std::this_thread::yield();
        }
    }

    ~WindowInstance() {
        close();
        if (winThread.joinable()) {
            winThread.join();
        }
    }

    void close() {
        if (hwnd != nullptr) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
    }

    bool isOpen() {
        std::lock_guard<std::mutex> lock(mtx);
        return isWindowOpen;
    }

    // Drawing methods
    void clear(const std::string& colorName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        RECT r = {0, 0, width, height};
        FillRect(hdcMem, &r, brush);
        DeleteObject(brush);
        invalidate();
    }

    void rect(int x, int y, int w, int h, const std::string& colorName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        RECT r = {x, y, x + w, y + h};
        FillRect(hdcMem, &r, brush);
        DeleteObject(brush);
        invalidate();
    }

    void rectEmpty(int x, int y, int w, int h, const std::string& colorName, int thickness) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

        Rectangle(hdcMem, x, y, x + w, y + h);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        invalidate();
    }

    void circle(int x, int y, int r, const std::string& colorName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);

        Ellipse(hdcMem, x - r, y - r, x + r, y + r);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        invalidate();
    }

    void circleEmpty(int x, int y, int r, const std::string& colorName, int thickness) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

        Ellipse(hdcMem, x - r, y - r, x + r, y + r);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        invalidate();
    }

    void ellipse(int x, int y, int rx, int ry, const std::string& colorName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);

        Ellipse(hdcMem, x - rx, y - ry, x + rx, y + ry);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        invalidate();
    }

    void line(int x1, int y1, int x2, int y2, const std::string& colorName, int thickness) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);

        MoveToEx(hdcMem, x1, y1, NULL);
        LineTo(hdcMem, x2, y2);

        SelectObject(hdcMem, oldPen);
        DeleteObject(pen);
        invalidate();
    }

    void text(int x, int y, const std::string& content, const std::string& colorName, int size) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        
        SetTextColor(hdcMem, color);
        SetBkMode(hdcMem, TRANSPARENT);

        HFONT font = CreateFontA(
            size, 0, 0, 0, FW_NORMAL, false, false, false,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial"
        );
        HFONT oldFont = (HFONT)SelectObject(hdcMem, font);

        TextOutA(hdcMem, x, y, content.c_str(), static_cast<int>(content.length()));

        SelectObject(hdcMem, oldFont);
        DeleteObject(font);
        invalidate();
    }

    void pixel(int x, int y, const std::string& colorName) {
        std::lock_guard<std::mutex> lock(mtx);
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        SetPixel(hdcMem, x, y, color);
        invalidate();
    }

    // Input getters
    double getMouseX() {
        std::lock_guard<std::mutex> lock(mtx);
        return mouseX;
    }

    double getMouseY() {
        std::lock_guard<std::mutex> lock(mtx);
        return mouseY;
    }

    bool getMouseLeft() {
        std::lock_guard<std::mutex> lock(mtx);
        return mouseLeft;
    }

    bool getKey(const std::string& keyName) {
        std::lock_guard<std::mutex> lock(mtx);
        int vk = mapKeyNameToVk(keyName);
        if (vk >= 0 && vk < 256) {
            return keys[vk];
        }
        return false;
    }

private:
    void invalidate() {
        if (hwnd != nullptr) {
            InvalidateRect(hwnd, NULL, false);
        }
    }

    int mapKeyNameToVk(const std::string& keyName) {
        if (keyName.empty()) return -1;
        
        if (keyName.length() == 1) {
            char c = keyName[0];
            if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
            if (c >= 'A' && c <= 'Z') return c;
            if (c >= '0' && c <= '9') return c;
            if (c == ' ') return VK_SPACE;
        }

        std::string lower = keyName;
        for (auto& c : lower) c = tolower(c);

        if (lower == "space") return VK_SPACE;
        if (lower == "left") return VK_LEFT;
        if (lower == "right") return VK_RIGHT;
        if (lower == "up") return VK_UP;
        if (lower == "down") return VK_DOWN;
        if (lower == "enter" || lower == "return") return VK_RETURN;
        if (lower == "escape" || lower == "esc") return VK_ESCAPE;
        if (lower == "shift") return VK_SHIFT;
        if (lower == "ctrl" || lower == "control") return VK_CONTROL;
        if (lower == "alt") return VK_MENU;
        if (lower == "backspace") return VK_BACK;
        if (lower == "tab") return VK_TAB;

        return -1;
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WindowInstance* self = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            self = (WindowInstance*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd = hwnd;
        } else {
            self = (WindowInstance*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        if (self != nullptr) {
            switch (msg) {
                case WM_PAINT: {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hwnd, &ps);
                    {
                        std::lock_guard<std::mutex> lock(self->mtx);
                        if (self->hdcMem != nullptr) {
                            BitBlt(hdc, 0, 0, self->width, self->height, self->hdcMem, 0, 0, SRCCOPY);
                        }
                    }
                    EndPaint(hwnd, &ps);
                    return 0;
                }
                case WM_MOUSEMOVE: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->mouseX = GET_X_LPARAM(lParam);
                    self->mouseY = GET_Y_LPARAM(lParam);
                    return 0;
                }
                case WM_LBUTTONDOWN: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->mouseLeft = true;
                    return 0;
                }
                case WM_LBUTTONUP: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->mouseLeft = false;
                    return 0;
                }
                case WM_KEYDOWN: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    if (wParam >= 0 && wParam < 256) {
                        self->keys[wParam] = true;
                    }
                    return 0;
                }
                case WM_KEYUP: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    if (wParam >= 0 && wParam < 256) {
                        self->keys[wParam] = false;
                    }
                    return 0;
                }
                case WM_CLOSE: {
                    DestroyWindow(hwnd);
                    return 0;
                }
                case WM_DESTROY: {
                    std::lock_guard<std::mutex> lock(self->mtx);
                    self->isWindowOpen = false;
                    PostQuitMessage(0);
                    return 0;
                }
            }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void run() {
        HINSTANCE hInst = GetModuleHandle(NULL);
        
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &WindowInstance::wndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"EpilespyLangWindowClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);

        RegisterClassW(&wc);

        RECT r = {0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, false);
        int w = r.right - r.left;
        int h = r.bottom - r.top;

        std::wstring wtitle(title.begin(), title.end());

        HWND tempHwnd = CreateWindowExW(
            0,
            L"EpilespyLangWindowClass",
            wtitle.c_str(),
            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT,
            w, h,
            NULL, NULL, hInst, this
        );

        if (!tempHwnd) {
            std::lock_guard<std::mutex> lock(mtx);
            isWindowOpen = false;
            return;
        }

        HDC hdcScreen = GetDC(tempHwnd);
        {
            std::lock_guard<std::mutex> lock(mtx);
            hdcMem = CreateCompatibleDC(hdcScreen);
            hbmMem = CreateCompatibleBitmap(hdcScreen, width, height);
            hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
            RECT clientRect = {0, 0, width, height};
            FillRect(hdcMem, &clientRect, brush);
            DeleteObject(brush);
        }
        ReleaseDC(tempHwnd, hdcScreen);

        ShowWindow(tempHwnd, SW_SHOW);
        UpdateWindow(tempHwnd);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            if (hdcMem != nullptr) {
                SelectObject(hdcMem, hbmOld);
                DeleteObject(hbmMem);
                DeleteDC(hdcMem);
                hdcMem = nullptr;
            }
            hwnd = nullptr;
            isWindowOpen = false;
        }
    }
};

#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif
