#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/device_selection.h>
#include <cerfcore/host/user_alert.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/touch_input.h>
#include <cerfcore/execution/jit_runner.h>
#include "../../libCerfCore/src/socs/au1300/au1300_lcd.h"
#include "../../libCerfCore/src/execution/mips_engine_bridge.h"
#include "../../libCerfCore/src/peripherals/peripheral_dispatcher.h"
#include <cerfengine/mips/mips_cpu.h>
#include "au1374_probe_control.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

namespace {

class CoreProbeDeviceSelection : public DeviceSelection {
public:
    using DeviceSelection::DeviceSelection;
    std::string SelectedDevice() override { return emu_.Config().device_override; }
};

class CoreProbeUserAlert : public UserAlert {
public:
    using UserAlert::UserAlert;
    void Show(AlertLevel, AlertPlacement, const std::string& title,
              const std::string& text) override {
        LOG(Caution, "%s: %s\n", title.c_str(), text.c_str());
    }
    void Post(AlertLevel level, const std::string& title, const std::string& text,
              std::function<void()> after) override {
        Show(level, AlertPlacement::kStandalone, title, text);
        if (after) after();
    }
};

class CoreProbePresentation : public HostPresentation {
public:
    using HostPresentation::HostPresentation;
    void PanelEnabled() override { emu_.Get<PanelFrameRenderer>().PresentedSize(width_, height_); }
    void GuestRemoded(uint32_t width, uint32_t height) override { width_ = width; height_ = height; }
    void GuestTurned(uint32_t quarters) override {
        if (((quarters ^ quarters_) & 1u) != 0u) std::swap(width_, height_);
        quarters_ = quarters;
    }
    uint32_t SurfaceWidth() const override { return width_; }
    uint32_t SurfaceHeight() const override { return height_; }
    void RestoreSurfaceSize(uint32_t width, uint32_t height) override { GuestRemoded(width, height); }
    void SaveScreenshot(const std::string& path) override { emu_.Get<Fatal>().Die("Headless screenshot request path=%s", path.c_str()); }
    void ShowCustomizationsRestart() override { LOG(Board, "Headless customization restart notification\n"); }

private:
    uint32_t width_ = 800u;
    uint32_t height_ = 480u;
    uint32_t quarters_ = 0u;
};

}

REGISTER_SERVICE_AS(CoreProbeDeviceSelection, DeviceSelection);
REGISTER_SERVICE_AS(CoreProbeUserAlert, UserAlert);
REGISTER_SERVICE_AS(CoreProbePresentation, HostPresentation);

}

int main(int argc, char** argv) {
    using namespace cerfcore;
    Log::SetFile("au1374-core-startup.log");
    Log::SetFlush(true);
    Log::SetEnabled(Log::MASK_ALL);
    int status = 0;
    {
        CerfConfig config;
        config.device_override = "au1374";
        config.crash_log_file = "au1374-core-crash.log";
        config.flush_outputs = true;
        config.timeout_seconds = 0;
        CerfEmulator emulator(config, argc, argv);
        emulator.CreateAllServices();
        auto& fatal = emulator.Get<Fatal>();
        const bool ready = fatal.Guard([&] {
            emulator.ResolveAllSlots();
            emulator.ReadyAllServices();
            Au1374ProbeTraces(emulator);
            emulator.StartGuest();
        });
        if (ready) {
            std::mutex mutex;
            std::condition_variable wake;
            bool finished = false;
            std::thread deadline([&] {
                std::unique_lock<std::mutex> lock(mutex);
                if (const auto* control = std::getenv("CERF_AU1374_PROBE_CONTROL")) {
                    Au1374ProbeControl(emulator, control, lock, wake, finished);
                    return;
                }
                const bool tap = std::getenv("CERF_AU1374_PROBE_TAP") != nullptr;
                const bool acknowledge = tap && std::getenv("CERF_AU1374_PROBE_ACK_MAP_DIALOG") != nullptr;
                if (tap) {
                    if (wake.wait_for(lock, std::chrono::seconds(50), [&] { return finished; })) return;
                    lock.unlock();
                    emulator.Get<TouchInput>().OnPenDown(100, 160);
                    lock.lock();
                    if (wake.wait_for(lock, std::chrono::seconds(2), [&] { return finished; })) return;
                    lock.unlock();
                    emulator.Get<TouchInput>().OnPenUp(100, 160);
                    lock.lock();
                    if (acknowledge) {
                        if (wake.wait_for(lock, std::chrono::seconds(10), [&] { return finished; })) return;
                        lock.unlock();
                        emulator.Get<TouchInput>().OnPenDown(453, 209);
                        lock.lock();
                        if (wake.wait_for(lock, std::chrono::seconds(2), [&] { return finished; })) return;
                        lock.unlock();
                        emulator.Get<TouchInput>().OnPenUp(453, 209);
                        lock.lock();
                    }
                }
                if (!wake.wait_for(lock, std::chrono::seconds(acknowledge ? 11 : tap ? 23 : 45), [&] { return finished; })) {
                    emulator.Get<JitRunner>().RequestStop();
                }
            });
            emulator.WaitForExit();
            {
                std::lock_guard<std::mutex> lock(mutex);
                finished = true;
            }
            wake.notify_one();
            deadline.join();
        }
        fatal.GuardTeardown([&] {
            const auto* cpu = emulator.Get<MipsEngineBridge>().Engine().Get<cerfengine::MipsCpu>().State();
            LOG(Board, "Core stopped pc=%08X r31=%08X r4=%08X r16=%08X\n", cpu->pc,
                static_cast<uint32_t>(cpu->gpr[31u]), static_cast<uint32_t>(cpu->gpr[4u]),
                static_cast<uint32_t>(cpu->gpr[16u]));
            auto& bus = emulator.Get<PeripheralDispatcher>();
            LOG(Board, "SD1 stopped cfg=%08X enable=%08X cfg2=%08X status=%08X cmd=%08X arg=%08X\n",
                bus.ReadWord(0x10601008u), bus.ReadWord(0x1060100Cu), bus.ReadWord(0x10601010u),
                bus.ReadWord(0x10601018u), bus.ReadWord(0x10601020u), bus.ReadWord(0x10601024u));
            auto& renderer = emulator.Get<PanelFrameRenderer>();
            uint32_t width = 0u, height = 0u;
            renderer.PresentedSize(width, height);
            if (width > 2048u || height > 2048u) return;
            const auto registers = emulator.Get<Au1300Lcd>().Snapshot();
            if (auto* json = std::fopen("au1374-core-lcd-registers.json", "wb")) {
                std::fprintf(json, "{\"width\":%u,\"height\":%u,\"registers\":[", width, height);
                for (size_t i = 0u; i < registers.size(); ++i) {
                    std::fprintf(json, "%s%u", i == 0u ? "" : ",", registers[i]);
                }
                std::fputs("]}\n", json);
                std::fclose(json);
            }
            std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
            renderer.RenderInto(pixels.data(), width, height);
            if (auto* ppm = std::fopen("au1374-core-frame.ppm", "wb")) {
                std::fprintf(ppm, "P6\n%u %u\n255\n", width, height);
                for (auto pixel : pixels) {
                    const unsigned char rgb[]{static_cast<unsigned char>(pixel >> 16u),
                                              static_cast<unsigned char>(pixel >> 8u),
                                              static_cast<unsigned char>(pixel)};
                    std::fwrite(rgb, 1u, 3u, ppm);
                }
                std::fclose(ppm);
            }
            std::printf("Core frame %u x %u\n", width, height);
        });
        if (fatal.Halted()) status = fatal.HaltCode();
    }
    Log::Close();
    std::printf("Core host exit=%d\n", status);
    return status;
}
