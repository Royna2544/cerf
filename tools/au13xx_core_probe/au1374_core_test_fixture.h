#pragma once

#define main Au1374OriginalBootHostMain
#include "au1374_core_host.cpp"
#undef main

#include "../../libCerfCore/src/execution/guest_cycle_clock.h"
#include "../../libCerfCore/src/execution/mips_engine_bridge.h"
#include "../../libCerfCore/src/peripherals/peripheral_dispatcher.h"
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/platform/host_file.h>
#include <cerfengine/byte_order.h>
#include <cerfengine/mips/mips_cpu.h>

#include <stdexcept>
#include <string>

namespace au1374_core_test {

inline void Check(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

class CoreTestFixture {
protected:
    explicit CoreTestFixture(cerfcore::CerfEmulator& emu)
        : emu_(emu), bus_(emu.Get<cerfcore::PeripheralDispatcher>()), clock_(emu.Get<cerfcore::GuestCycleClock>()),
          ram_(emu.Get<cerfcore::EmulatedMemory>()),
          state_(*emu.Get<cerfcore::MipsEngineBridge>().Engine().Get<cerfengine::MipsCpu>().State()) {}

    void Tick(uint32_t cycles) {
        state_.guest_cycle_counter += cycles;
        clock_.OnDispatch();
    }
    void Write(uint32_t pa, uint32_t value) { bus_.WriteWord(pa, value); }
    uint32_t Read(uint32_t pa) { return bus_.ReadWord(pa); }
    void Put(uint32_t pa, uint32_t value) {
        auto* data = ram_.TryTranslateRange(pa, 4u, true);
        Check(data != nullptr, "test RAM write route");
        cerfengine::le::Put32(data, value);
    }
    uint32_t Get(uint32_t pa) {
        const auto* data = ram_.TryTranslateRange(pa, 4u);
        Check(data != nullptr, "test RAM read route");
        return cerfengine::le::U32(data);
    }

    cerfcore::CerfEmulator&         emu_;
    cerfcore::PeripheralDispatcher& bus_;
    cerfcore::GuestCycleClock&      clock_;
    cerfcore::EmulatedMemory&       ram_;
    cerfengine::MipsCpuState&       state_;
};

template <typename Body>
bool BootAndRun(int argc, char** argv, const std::string& crash_log, Body&& body) {
    using namespace cerfcore;
    CerfConfig config;
    config.device_override = "au1374";
    config.crash_log_file  = crash_log.c_str();
    CerfEmulator emulator(config, argc, argv);
    emulator.CreateAllServices();
    return emulator.Get<Fatal>().Guard([&] {
        emulator.ResolveAllSlots();
        emulator.ReadyAllServices();
        body(emulator);
    });
}

template <typename Body>
int RunCoreTest(int argc, char** argv, const char* name, Body&& body) {
    const std::string stem = std::string("au1374-core-") + name + "-test";
    cerfcore::Log::SetFile((stem + ".log").c_str());
    cerfcore::Log::SetFlush(true);
    cerfcore::Log::SetEnabled(cerfcore::Log::MASK_ALL);
    Check(BootAndRun(argc, argv, stem + "-crash.log", body), (stem + " halted").c_str());
    return 0;
}

inline bool FileContains(const std::string& path, const std::string& text) {
    cerfcore::host_platform::HostFile file;
    uint64_t size = 0u;
    if (!file.Open(path, cerfcore::host_platform::FileMode::kReadExisting) || !file.Size(size)) return false;
    std::string bytes(static_cast<size_t>(size), '\0');
    uint32_t done = 0u;
    return file.Read(bytes.data(), static_cast<uint32_t>(bytes.size()), done) && done == bytes.size() &&
           bytes.find(text) != std::string::npos;
}

template <typename Access>
bool HaltsAfterBoot(int argc, char** argv, const char* name, const std::string& reason, Access&& access) {
    std::string crash_log;
    const bool completed =
        BootAndRun(argc, argv, std::string("au1374-core-") + name + "-expected-halt-crash.log",
                   [&](cerfcore::CerfEmulator& emu) {
                       crash_log = emu.Get<cerfcore::Fatal>().CrashLogPath();
                       access(emu.Get<cerfcore::PeripheralDispatcher>());
                   });
    return !completed && FileContains(crash_log, reason);
}

}
