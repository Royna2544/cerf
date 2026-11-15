#pragma once
#include "au1374_probe_debug.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/host/touch_input.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

inline void Au1374ProbeFrame(cerfcore::CerfEmulator& emu, const std::string& path) {
    Au1374ProbeCpu(emu);
    auto& renderer = emu.Get<cerfcore::PanelFrameRenderer>();
    uint32_t width = 0u, height = 0u;
    renderer.PresentedSize(width, height);
    if (width == 0u || height == 0u || width > 2048u || height > 2048u) return;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    renderer.RenderInto(pixels.data(), width, height);
    auto* file = std::fopen(path.c_str(), "wb");
    if (!file) emu.Get<cerfcore::Fatal>().UserError("Probe frame open: %s", path.c_str());
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (const auto pixel : pixels) {
        const unsigned char rgb[]{static_cast<unsigned char>(pixel >> 16u),
                                  static_cast<unsigned char>(pixel >> 8u),
                                  static_cast<unsigned char>(pixel)};
        std::fwrite(rgb, 1u, 3u, file);
    }
    std::fclose(file);
    std::printf("Probe frame: %s\n", path.c_str());
    std::fflush(stdout);
}

// Append complete lines: down X Y; up X Y; frame PATH; stop. Actions run while
// the guest is paused. The deadline still terminates an unattended functional run.
inline void Au1374ProbeControl(cerfcore::CerfEmulator& emu, const char* path,
                              std::unique_lock<std::mutex>& lock,
                              std::condition_variable& wake, const bool& finished) {
    using namespace std::chrono;
    auto& runner = emu.Get<cerfcore::JitRunner>();
    const auto* budget = std::getenv("CERF_AU1374_PROBE_SECONDS");
    const auto end = steady_clock::now() + seconds(budget ? std::max(1, std::atoi(budget)) : 900);
    size_t consumed = 0u;
    while (!finished && steady_clock::now() < end) {
        std::ifstream file(path);
        std::string line;
        size_t index = 0u;
        while (std::getline(file, line)) {
            if (++index <= consumed) continue;
            consumed = index;
            if (line.empty() || line[0u] == '#') continue;
            std::istringstream command(line);
            std::string action;
            command >> action;
            lock.unlock();
            runner.Pause();
            const bool ok = emu.Get<cerfcore::Fatal>().Guard([&] {
                int x = 0, y = 0;
                if ((action == "down" || action == "up") && (command >> x >> y)) {
                    auto& touch = emu.Get<cerfcore::TouchInput>();
                    if (action == "down") touch.OnPenDown(x, y);
                    else touch.OnPenUp(x, y);
                } else if (action == "cpu") Au1374ProbeCpu(emu);
                else if (action == "dump") {
                    uint32_t address = 0u, bytes = 0u;
                    std::string target;
                    command >> std::hex >> address >> bytes >> target;
                    Au1374ProbeDump(emu, address, bytes, target);
                } else if (action == "frame") {
                    std::string target;
                    std::getline(command >> std::ws, target);
                    Au1374ProbeFrame(emu, target);
                } else if (action == "stop") runner.RequestStop();
                else emu.Get<cerfcore::Fatal>().UserError("Unknown probe control: %s", line.c_str());
            });
            runner.Resume();
            lock.lock();
            if (!ok || action == "stop" || runner.Stopped()) return;
        }
        wake.wait_for(lock, milliseconds(100), [&] { return finished; });
    }
    if (!finished) runner.RequestStop();
}
