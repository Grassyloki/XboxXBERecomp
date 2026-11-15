// What both windows (app/main.cpp's Win32 one, app/main_sdl.cpp's SDL one) share: the frame pacer, and F3's thread
// stacks and guest memory. The window files own Paused() and the keys that call these.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "core/netplay.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace app {

static void (*g_mid_frame)() = nullptr;
void SetMidFrame(void (*fn)()) { g_mid_frame = fn; }

void Pace(uint32_t vblanks) {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    if (Paused()) {
        LOGR("paused: the window is not in front");
        while (Paused()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        LOGR("resumed");
        next = clock::now();
    }
    // LAN play: every copy at 60 a second whatever its cap, as the frames are the title's clocks (netplay::Frame)
    uint32_t cap = PaceRate(netplay::Active() ? 60 : Get().fps_cap, vblanks);
    auto now = clock::now();
    if (!cap) {  // NF_UNCAPPED=1 (diagnostic): the frame's work time, averaged over 10 s
        static clock::time_point last_u = now, report_u = now + std::chrono::seconds(10);
        static double work_u = 0;
        static uint32_t n_u = 0;
        work_u += std::chrono::duration<double, std::milli>(now - last_u).count(), n_u++;
        last_u = now;
        if (now >= report_u && n_u) {
            LOGR("frames uncapped: %.2f ms a frame on average (%.0f a second)", work_u / n_u, 1000.0 * n_u / work_u);
            work_u = 0, n_u = 0, report_u = now + std::chrono::seconds(10);
        }
        next = now;
        netplay::Frame(60);
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    static clock::time_point last = now, report = now + std::chrono::seconds(10);
    static uint32_t frames = 0, slow = 0;
    static double longest = 0;
    double took = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    frames++;
    if (took > 1.5 * std::chrono::duration<double, std::milli>(period).count()) slow++, longest = std::max(longest, took);
    if (now >= report) {
        uint32_t compiled = gpu::g_shaders_compiled.exchange(0);
        if (slow) LOGR("frames: %u of %u in the last 10 s over %.1f ms (%u a second paced), the longest %.1f ms; "
                       "%u shaders compiled", slow, frames, 1.5 * std::chrono::duration<double, std::milli>(period).count(),
                       cap, longest, compiled);
        if (slow) logx::Flush();
        report = now + std::chrono::seconds(10);
        frames = slow = 0;
        longest = 0;
    }
    next += period;
    if (next < now - period) next = now;
    if (g_mid_frame) g_mid_frame();
    // NF_PERF_WORK (the capped run): the frame's work, the period less the time this frame's pacing waits
    static double waited_last = 0, work_sum = 0;
    static uint32_t work_n = 0;
    static clock::time_point work_report = now + std::chrono::seconds(10);
    work_sum += took - waited_last, work_n++;
    if (now >= work_report && work_n) {
        LOGR("frame work: %.2f ms a frame on average (paced at %u a second)", work_sum / work_n, cap);
        work_sum = 0, work_n = 0, work_report = now + std::chrono::seconds(10);
    }
    const auto wait_from = clock::now();
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
    waited_last = std::chrono::duration<double, std::milli>(clock::now() - wait_from).count();
    netplay::Frame(cap);  // LAN play: the next frame's controllers of every player
}

// F3 with XBE_CAPTURE_DIR: the guest's committed memory too (as app/main.cpp's, sdk/scripts/guestmem.py reads it)
void DumpGuestMemory() {
    if (diag::CaptureDir().empty()) return;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::weakly_canonical(ExeDir() / diag::CaptureDir(), ec);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path file = dir / ("guest_mem_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".bin");
    FILE* f = std::fopen(file.string().c_str(), "wb");
    if (!f) return;
    std::fwrite("XBEMEM1", 1, 8, f);
    uint64_t total = 0;
    for (uint64_t va = 0x1000; va < 0x100000000ull;) {
        if (!mem::IsCommitted(uint32_t(va))) {
            va += 0x1000;
            continue;
        }
        uint64_t end = va;
        while (end < 0x100000000ull && end - va < 0x1000000 && mem::IsCommitted(uint32_t(end))) end += 0x1000;
        uint32_t head[2] = {uint32_t(va), uint32_t(end - va)};
        std::fwrite(head, 4, 2, f);
        std::fwrite(kernel::HostPtr(uint32_t(va)), 1, size_t(end - va), f);
        total += end - va;
        va = end;
    }
    std::fclose(f);
    LOGR("F3: guest memory, %llu MB, written to %s", (unsigned long long)(total >> 20), file.string().c_str());
}

void DumpThreads() {
    LOGR("F3: thread stacks%s", diag::CaptureDir().empty() ? " (no guest memory: XBE_CAPTURE_DIR is not set)" : ", guest memory too");
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGR("F3: thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
    DumpGuestMemory();
}

}  // namespace app
