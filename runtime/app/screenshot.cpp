// F12: the frame as a PNG in the screenshots folder beside the exe. The renderer writes the frame as shown
// (gpu::RequestScreenshot, frame_info.cpp).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#include "app/app.h"
#include "app/image.h"
#include "core/log.h"
#include "gpu/style.h"

namespace app {

namespace {

std::atomic<int64_t> g_saved_ms{0};  // when the last one was written
uint32_t g_vulkan_shots = 0;         // gpu::ScreenshotResult's count when the last F12 asked the Vulkan renderer

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// screenshots/<title> <date> <time>.png
std::filesystem::path NextPath() {
    std::time_t now = std::time(nullptr);
    std::tm t{};
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &t);
    std::wstring name;
    for (const wchar_t* c = kGame.short_title; c && *c; c++) name += *c;
    name += L' ';
    for (const char* c = stamp; *c; c++) name += wchar_t(*c);
    std::filesystem::path dir = ExeDir() / "screenshots";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / (name + L".png");
}

}  // namespace

void RequestScreenshot() {
    // the renderer writes it at its next present (the picture as shown)
    std::wstring path;
    bool ok = false;
    g_vulkan_shots = gpu::ScreenshotResult(path, ok);
    gpu::RequestScreenshot(NextPath().wstring());
}
bool ScreenshotJustSaved() {
    std::wstring path;
    bool ok = false;
    if (uint32_t n = gpu::ScreenshotResult(path, ok); n != g_vulkan_shots) {
        g_vulkan_shots = n;
        if (ok) g_saved_ms = NowMs();
    }
    return NowMs() - g_saved_ms.load() < 3000 && g_saved_ms.load();
}

}  // namespace app
