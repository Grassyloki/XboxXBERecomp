// The boot splash: GameInfo::splash .bmp or .png beside the exe, centred in a borderless window. The game boots behind
// it with its window hidden and waits at its first frame until the splash has been up kMinimum, since this boot takes
// a fraction of a second. No file, or a relaunch: no splash. Windows: a layered window; elsewhere an SDL one (Linux, macOS;
// Android has the app's own launcher in front instead).
#ifndef __ANDROID__  // Android has the app's own launcher in front instead
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <SDL3/SDL.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <thread>
#include <vector>

#include "app/app.h"
#include "app/image.h"
#include "core/log.h"

namespace app {

namespace {

constexpr auto kMinimum = std::chrono::seconds(1);

std::atomic<bool> g_waiting{false};
std::chrono::steady_clock::time_point g_until;

// the splash picture beside the exe (.bmp, else .png) as premultiplied BGRA, as UpdateLayeredWindow wants it and a
// transparent window's compositor blends it; a picture without alpha comes out opaque
bool LoadSplash(std::vector<uint32_t>& pixels, uint32_t& w, uint32_t& h, std::filesystem::path& path) {
    for (const char* ext : {".bmp", ".png"}) {
        std::error_code ec;
#ifdef _WIN32
        path = ExeDir() / (std::wstring(kGame.splash) + std::wstring(ext, ext + std::strlen(ext)));
#else
        path = ExeDir() / (Utf8(kGame.splash) + ext);
#endif
        if (!std::filesystem::exists(path, ec)) continue;
        uint32_t error = 0;
        if (DecodeImage(path, pixels, w, h, true, 0, &error)) return true;
        LOGW("splash: %s could not be read (%08X)", path.string().c_str(), unsigned(error));
        pixels.clear();
    }
    return false;
}

}  // namespace

void SplashFirstFrame() {
    if (!g_waiting.exchange(false)) return;
    std::this_thread::sleep_until(g_until);
    OpenGameWindow();
}

#ifdef _WIN32

namespace {
HWND g_splash = nullptr;
}  // namespace

bool OpenSplash() {
    if (std::wcsstr(GetCommandLineW(), L"--relaunched")) return false;
    std::vector<uint32_t> pixels;
    uint32_t pw = 0, ph = 0;
    std::filesystem::path path;
    if (!LoadSplash(pixels, pw, ph, path)) return false;
    const UINT w = pw, h = ph;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514));  // IDC_WAIT
    std::wstring cls = std::wstring(kGame.name) + L"Splash";
    wc.lpszClassName = cls.c_str();
    RegisterClassExW(&wc);
    // centred on the primary screen at one image pixel per screen pixel
    POINT at{(GetSystemMetrics(SM_CXSCREEN) - int(w)) / 2, (GetSystemMetrics(SM_CYSCREEN) - int(h)) / 2};
    g_splash = CreateWindowExW(WS_EX_LAYERED, wc.lpszClassName, kGame.title, WS_POPUP,
                               at.x, at.y, int(w), int(h), nullptr, nullptr, inst, nullptr);
    if (!g_splash) return false;

    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), LONG(w), -LONG(h), 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        std::memcpy(bits, pixels.data(), pixels.size() * 4);
        HGDIOBJ old = SelectObject(dc, bitmap);
        SIZE size{LONG(w), LONG(h)};
        POINT origin{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(g_splash, screen, &at, &size, dc, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    // to the front once, without staying on top
    SetWindowPos(g_splash, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(g_splash, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    g_until = std::chrono::steady_clock::now() + kMinimum;
    g_waiting = true;
    LOGI("splash: %s, %ux%u", path.string().c_str(), w, h);
    return true;
}

void CloseSplash() {
    if (g_splash) DestroyWindow(g_splash);
    g_splash = nullptr;
}

#else

namespace {
SDL_Window* g_splash = nullptr;
}  // namespace

bool OpenSplash() {
    if (const char* e = std::getenv("XBE_RELAUNCHED"); e && *e == '1') return false;
    std::vector<uint32_t> pixels;
    uint32_t w = 0, h = 0;
    std::filesystem::path path;
    if (!LoadSplash(pixels, w, h, path)) return false;

    // centred on the primary screen at one image pixel per screen pixel, borderless, see-through where the picture is
    g_splash = SDL_CreateWindow(Utf8(kGame.title).c_str(), int(w), int(h),
                                SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_UTILITY);
    if (!g_splash) g_splash = SDL_CreateWindow(Utf8(kGame.title).c_str(), int(w), int(h), SDL_WINDOW_BORDERLESS);
    if (!g_splash) {
        LOGW("splash: no window: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(g_splash, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_Surface* screen = SDL_GetWindowSurface(g_splash);
    // BGRA in memory is SDL's ARGB8888 on a little-endian machine
    SDL_Surface* picture = SDL_CreateSurfaceFrom(int(w), int(h), SDL_PIXELFORMAT_ARGB8888, pixels.data(), int(w) * 4);
    if (screen && picture) {
        SDL_SetSurfaceBlendMode(picture, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(picture, nullptr, screen, nullptr);
        SDL_UpdateWindowSurface(g_splash);
    }
    if (picture) SDL_DestroySurface(picture);
    SDL_ShowWindow(g_splash);
    SDL_RaiseWindow(g_splash);
    SDL_PumpEvents();
    g_until = std::chrono::steady_clock::now() + kMinimum;
    g_waiting = true;
    LOGI("splash: %s, %ux%u", path.string().c_str(), w, h);
    return true;
}

void CloseSplash() {
    if (g_splash) SDL_DestroyWindow(g_splash);
    g_splash = nullptr;
}

#endif

}  // namespace app
#else
#include "app/app.h"

namespace app {
bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
}  // namespace app
#endif
