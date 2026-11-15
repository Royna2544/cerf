#define main Au1374OriginalBootHostMain
#include "au1374_core_host.cpp"
#undef main

#include "../../libCerfCore/src/execution/mips_engine_bridge.h"
#include "../../libCerfCore/src/execution/guest_cycle_clock.h"
#include "../../libCerfCore/src/peripherals/peripheral_dispatcher.h"
#include "../../libCerfCore/src/socs/au1300/au1300_clock_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_dma_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_gpic_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_uart0_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_uart1_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_uart2_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_uart3_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_nand_control.h"
#include "../../libCerfCore/src/socs/au1300/au1300_nand_part.h"
#include "../../libCerfCore/src/socs/au1300/au1300_usb_control.h"
#include "../../libCerfCore/src/socs/au1300/au1300_ohci0_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_ohci1_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_ehci_peripheral.h"
#include "../../libCerfCore/src/socs/au1300/au1300_vss_peripheral.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/platform/host_file.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include <cerfengine/byte_order.h>
#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

void Check(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

class CoreIntegrationTest {
public:
    explicit CoreIntegrationTest(cerfcore::CerfEmulator& emulator)
        : emu_(emulator), engine_(emulator.Get<cerfcore::MipsEngineBridge>().Engine()),
          state_(*engine_.Get<cerfengine::MipsCpu>().State()),
          clock_(emulator.Get<cerfcore::GuestCycleClock>()),
          bus_(emulator.Get<cerfcore::PeripheralDispatcher>()),
          ram_(emulator.Get<cerfcore::EmulatedMemory>()) {}

    void Run() {
        LargeTransfer();
        RequestWakeup();
        Nand();
        NandUid();
        CodeCoherence();
        Ohci();
        Ehci();
        Vss();
        Ac97();
        Dwc2();
        Snapshot();
        ResetDomains();
    }

private:
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
    void Arm(const std::array<uint32_t, 8u>& descriptor) {
        Write(0x14002000u, 0u);
        Write(0x14002014u, 6u);
        Write(0x14002010u, 0u);
        for (uint32_t i = 0u; i < descriptor.size(); ++i) Put(0x10000u + i * 4u, descriptor[i]);
        Write(0x14002004u, 0x10000u);
        Write(0x14002000u, 1u);
        Write(0x1400200Cu, 0u);
    }
    void LargeTransfer() {
        constexpr uint32_t bytes = 0x40000u;
        auto* source = ram_.TryTranslateRange(0x200000u, bytes, true);
        auto* destination = ram_.TryTranslateRange(0x300000u, bytes, true);
        Check(source && destination, "large transfer RAM");
        for (uint32_t i = 0u; i < bytes; ++i) source[i] = static_cast<uint8_t>(i ^ (i >> 8u));
        std::memset(destination, 0xFF, bytes);
        constexpr uint32_t command = 0xC0000104u | (31u << 25u) | (31u << 20u) |
                                     (2u << 18u) | (2u << 16u);
        Arm({command, bytes, 0x200000u, 0u, 0x300000u, 0u, 0u, 0u});
        Check(Read(0x14002018u) != 0u, "large transfer must yield with work pending");
        uint32_t dispatches = 0u;
        while (Read(0x14003004u) == 0u && dispatches < 100u) {
            Tick(1u);
            ++dispatches;
        }
        Check(Read(0x14002018u) == 0u, "large transfer did not resume from clock event");
        Check(std::memcmp(source, destination, bytes) == 0, "large transfer data mismatch");
        Check(Read(0x14003004u) == 1u, "large transfer IRQ pending");
        std::printf("PASS Core DMA bounded pump resumes: %u dispatches\n", dispatches);
    }
    void RequestWakeup() {
        Put(0x40000u, 0x1234ABCDu);
        Write(0x10200060u, 5u | (6u << 8u));
        Write(0x10201014u, 2u);
        constexpr uint32_t command = 0x80000004u | (28u << 25u) | (31u << 20u);
        Arm({command, 4u, 0x40000u, 0u, 0x41000u, 0u, 0u, 0u});
        Check(Read(0x14002018u) == 4u, "DMA ignored inactive GPIO request");
        Write(0x10200000u, 1u << 5u);
        Tick(1u);
        Check(Get(0x41000u) == 0x1234ABCDu, "GPIO request failed to wake DMA event");
        Check(Read(0x14002018u) == 0u, "GPIO DMA remaining count");
        std::puts("PASS Core GPIO request wakes DMA without a DMA-register access");
    }
    uint32_t Execute(uint32_t segment) {
        auto& guest = engine_.Get<cerfengine::GuestEngine>();
        guest.SetHostChainExit(true);
        state_.cp0_status = 0u;
        state_.pc = segment | 0x2000u;
        state_.branch_state = cerfengine::MipsBranch::kNone;
        state_.gpr[2u] = 0u;
        for (uint32_t i = 0u; i < 8u && state_.pc != (segment | 0x3000u); ++i) {
            Check(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning, "execute halted");
            clock_.OnDispatch();
        }
        Check(state_.pc == (segment | 0x3000u), "test program did not reach branch target");
        return static_cast<uint32_t>(state_.gpr[2u]);
    }
    void Nand() {
        constexpr uint32_t base = 0x1C000000u;
        const auto command = [&](uint8_t value) { bus_.WriteByte(base, value); };
        const auto address = [&] {
            for (uint8_t byte : {0u, 0xFFu, 0xFFu, 1u}) bus_.WriteByte(base + 4u, byte);
        };
        Write(0x14001100u, 0x10u);
        command(0x90u);
        bus_.WriteByte(base + 4u, 0u);
        Check(Read(base + 0x20u) == 0x3F5A76ECu, "NAND ID word lane order");
        command(0u);
        command(0x80u);
        address();
        Write(base + 0x20u, 0x12345678u);
        command(0x10u);
        command(0x70u);
        Check(bus_.ReadByte(base + 0x20u) == 0xC0u, "NAND program status");
        command(0u);
        address();
        Check(Read(base + 0x20u) == 0x12345678u, "NAND volatile program");
        command(0x70u);
        Check(bus_.ReadByte(base + 0x20u) == 0xC0u, "NAND read status");
        command(0u);
        Check(Read(base + 0x20u) == 0xFFFFFFFFu, "NAND read cursor after status");
        command(0x60u);
        for (uint8_t byte : {0xFFu, 0xFFu, 1u}) bus_.WriteByte(base + 4u, byte);
        command(0xD0u);
        command(0u);
        address();
        Check(Read(base + 0x20u) == 0xFFFFFFFFu, "NAND erase");
        Write(0x14001100u, 0u);
        std::puts("PASS Core NAND ID, volatile program, read-status resumption and erase");
    }
    void NandUid() {
        constexpr uint32_t base = 0x1C000000u;
        std::array<uint8_t, 16u> expected{};
        auto& config = emu_.Get<cerfcore::DeviceConfig>();
        cerfcore::host_platform::HostFile file;
        uint32_t done = 0u;
        Check(file.Open(cerfcore::ResolveDeviceFile(config.device_name, config.storage_nand) + ".uid",
                        cerfcore::host_platform::FileMode::kReadExisting) &&
              file.Read(expected.data(), 16u, done) && done == 16u, "UID fixture read");
        Write(0x14001100u, 0x10u);
        for (uint8_t command : {0xFFu, 0x30u, 0x65u, 0u}) bus_.WriteByte(base, command);
        for (uint32_t i = 0u; i < 4u; ++i) bus_.WriteByte(base + 4u, 0u);
        Check(Read(base + 0x20u) == cerfengine::le::U32(expected.data()), "UID first word and lane order");
        constexpr const char* state_path = "au1374-core-uid-test.state";
        auto& part = emu_.Get<cerfcore::Au1300NandPart>();
        {
            cerfcore::StateWriter writer(emu_, state_path);
            writer.BeginFrame(base);
            part.SaveState(writer);
            writer.EndFrame();
            Check(writer.Commit(), "UID state write");
        }
        for (uint32_t i = 4u; i < 16u; ++i) Check(bus_.ReadByte(base + 0x20u) == expected[i], "UID byte stream");
        bus_.WriteByte(base, 0xFFu);
        {
            cerfcore::StateReader reader(emu_, state_path);
            Check(reader.EnterFrame() == base, "UID state identity");
            part.RestoreState(reader);
            reader.LeaveFrame();
        }
        for (uint32_t i = 4u; i < 16u; i += 4u) {
            Check(Read(base + 0x20u) == cerfengine::le::U32(expected.data() + i), "UID restored stream cursor");
        }
        bus_.WriteByte(base, 0xFFu);
        bus_.WriteByte(base, 0x90u);
        bus_.WriteByte(base + 4u, 0u);
        Check(Read(base + 0x20u) == 0x3F5A76ECu, "UID exit did not restore normal NAND ID mode");
        Write(0x14001100u, 0u);
        std::puts("PASS Core NAND UID sequence, byte/word lanes, mid-read restore and normal-mode exit");
    }
    void CodeCoherence() {
        Put(0x2000u, 0x2402000Bu);
        Put(0x2004u, 0x08000C00u);
        Put(0x2008u, 0u);
        for (uint32_t segment : {0x80000000u, 0xA0000000u}) Check(Execute(segment) == 11u, "initial code");
        constexpr uint32_t command = 0x80002004u | (31u << 20u) | (2u << 18u) | (2u << 16u);
        Arm({command, 4u, 0x24020016u, 0u, 0x2000u, 0u, 0u, 0u});
        for (uint32_t segment : {0x80000000u, 0xA0000000u}) Check(Execute(segment) == 22u, "stale DMA code");
        std::puts("PASS Core DMA executable writes invalidate cached and uncached aliases");
    }
    void Ohci() {
        constexpr uint32_t base = 0x14020800u;
        Write(0x1402101Cu, 0xC0000u);
        Write(0x14021004u, 7u);
        Write(0x140210C8u, 3u);
        Write(base + 8u, 1u);
        Check(Read(base + 4u) == 0xC0u, "OHCI reset state");
        Check(Read(base + 0x34u) == 11999u, "OHCI reset frame interval");
        Write(base + 0x18u, 0x50000u);
        Put(0x50080u, 0xFFFFFFFFu);
        Write(base + 0x40u, 10799u);
        Write(base + 0x10u, 0x80000004u);
        Write(base + 4u, 0x84u);
        Check(Read(base + 0x3Cu) == 0u, "OHCI SOF occurred before the first frame interval");
        Tick(792000u);
        Check(Read(base + 0x3Cu) == 1u && Get(0x50080u) == 1u, "OHCI first SOF/HCCA write");
        Check((Read(0x140210C4u) & 2u) != 0u, "OHCI SOF interrupt did not reach USB router");
        Tick(79200u);
        Check(Read(base + 0x3Cu) == 1u, "OHCI periodic boundary changed the frame");
        Write(base + 0xCu, 4u);
        Check((Read(0x140210C4u) & 2u) == 0u, "OHCI W1C interrupt did not deassert");
        Write(0x1402101Cu, 0u);
        const auto stopped = Read(base + 0x38u);
        Tick(1000000u);
        Check(Read(base + 0x38u) == stopped, "OHCI clock gating did not freeze the frame");
        Write(0x1402101Cu, 0xC0000u);
        Write(base + 4u, 0xC0u);
        Write(base + 4u, 0x3C0u);
        Write(base + 8u, 1u);
        Check(Read(base + 4u) == 0x3C0u, "OHCI software reset lost routing/wakeup configuration");
        std::puts("PASS Core OHCI first SOF, HCCA, exact periodic boundary, interrupt and clock gate");
    }
    void Ehci() {
        constexpr uint32_t base = 0x14020220u;
        Write(0x14021000u, 2u);
        Write(0x1402101Cu, 0xE0000u);
        Write(0x140210C8u, 7u);
        Check(bus_.ReadByte(base - 0x20u) == 0x20u && bus_.ReadHalf(base - 0x1Eu) == 0x100u,
              "EHCI capability access widths");
        Write(base, 2u);
        Check(Read(base + 4u) == 0x1000u, "EHCI reset/halt state");
        for (uint32_t i = 0u; i < 1024u; ++i) Put(0x60000u + i * 4u, 1u);
        Put(0x61000u, 0x61002u);
        Put(0x61010u, 1u);
        Put(0x61018u, 0x40u);
        Write(base + 0x14u, 0x60000u);
        Write(base + 0x18u, 0x61000u);
        Write(base + 8u, 0x28u);
        Write(base, 0x80071u);
        Tick(99000u);
        Check(Read(base + 0xCu) == 1u && (Read(base + 4u) & 0xC020u) == 0xC020u,
              "EHCI microframe or empty schedule processing");
        Check((Read(0x140210C4u) & 4u) == 0u, "EHCI async interrupt preceded its threshold");
        Tick(7u * 99000u);
        Check((Read(0x140210C4u) & 4u) != 0u, "EHCI async advance interrupt threshold");
        Write(base + 4u, 0x20u);
        Check((Read(0x140210C4u) & 4u) == 0u, "EHCI async W1C IRQ clear");
        Write(base, 0x80000u);
        Write(base + 0xCu, 0x1FFFu);
        Write(base, 0x80001u);
        Tick(99000u);
        Check(Read(base + 0xCu) == 0x2000u && (Read(base + 4u) & 8u) != 0u,
              "EHCI frame list rollover");
        Write(base, 0x80000u);
        Tick(792000u);
        Check(Read(base + 0xCu) == 0x2000u && (Read(base + 4u) & 0x1000u) != 0u,
              "EHCI halted frame counter advanced");
        Write(base, 2u);
        std::puts("PASS Core EHCI capabilities, schedules, async IRQ threshold, rollover and halt");
    }
    void Vss() {
        constexpr uint32_t base = 0x1100300Cu;
        auto& vss = emu_.Get<cerfcore::Au1300VssPeripheral>();
        using Block = cerfcore::Au1300VssPeripheral::Block;
        Write(base + 8u, 15u);
        Write(base, 0u);
        Write(base + 4u, 2u);
        Write(base + 4u, 1u);
        Write(base + 8u, 0u);
        Check(Read(base + 4u) == 1u && !vss.Enabled(Block::Bsa), "VSS off sequence");
        Write(base + 4u, 3u);
        Write(base, 0x1FFFFFEu);
        for (uint32_t footer : {1u, 3u, 7u, 15u}) Write(base + 8u, footer);
        Write(base, 0x1FFFFFFu);
        Write(base + 4u, 2u);
        Check(!vss.Enabled(Block::Bsa), "VSS enabled before isolation release");
        Write(base + 8u, 31u);
        Check(vss.Enabled(Block::Bsa), "VSS enable sequence");
        Write(base + 4u, 3u);
        Check(!vss.Enabled(Block::Bsa), "VSS reset did not disable access");
        std::puts("PASS Core VSS footer isolation, clock/reset sequencing and register readback");
    }
    void Ac97() {
        constexpr uint32_t base = 0x10A01000u;
        Check(Read(base + 0x14u) == 0u, "AC97 disabled status");
        Check(Read(base + 0xCu) == 0u, "AC97 reset interrupt mask differs from datasheet");
        Write(base, 0x24u);
        Write(base + 0x24u, 2u);
        Write(base + 0x24u, 0u);
        Write(base + 4u, 3u);
        Check(Read(base + 0x14u) == 1u, "AC97 status before device enable");
        Write(base + 8u, 0xF4E01806u);
        Write(base + 0xCu, 0xFFFFFFFFu);
        Check((Read(base + 0x14u) & 0x1203u) == 0x1203u, "AC97 enabled empty FIFO status");
        Write(base + 0x20u, 0x00260000u);
        Check((Read(base + 0x18u) & 0x01000000u) != 0u, "AC97 codec command completion");
        Write(base + 0x18u, 0x01000000u);
        Write(base + 0x20u, 0x02260000u);
        Check((Read(base + 0x20u) & 0xFFFFu) == 15u, "AC97 powerdown write changed readonly readiness bits");
        Write(base + 0x18u, 0x01000000u);
        Write(base + 0x20u, 0x0078C00Fu);
        Write(base + 0x20u, 0x0076E000u);
        Tick(100000u);
        Write(base + 0x20u, 0x02760000u);
        Check((Read(base + 0x20u) & 0x8000u) == 0u, "AC97 battery ADC conversion did not finish");
        Write(base + 0x20u, 0x027A0000u);
        Check((Read(base + 0x20u) & 0xFFFu) == 1738u, "AC97 provisional battery ADC input");
        Write(base + 4u, 2u);
        Check(Read(base + 0x14u) == 0u && Read(base + 8u) == 0xF4E01806u,
              "AC97 suspend did not preserve configuration");
        Write(base + 4u, 0u);
        Check(Read(base) == 0x24u && Read(base + 8u) == 0u, "AC97 disable/reset selection retention");
        std::puts("PASS Core AC97 protocol, codec power status, battery ADC, suspend and reset");
    }
    void Dwc2() {
        constexpr uint32_t base = 0x14022000u;
        Write(base + 0x804u, 2u);
        Check(Read(base + 0x804u) == 2u, "DWC2 soft disconnect");
        Write(base + 0xCu, 0x4000140Du);
        Write(base + 0x10u, 0x3Fu);
        Check(Read(base + 0x10u) == 0x80000000u, "DWC2 reset/flush completion and AHB idle");
        Check(Read(base + 0xCu) == 0x4000140Du, "DWC2 core reset changed mode configuration");
        Write(base + 0x24u, 256u);
        Write(base + 0x28u, 0x01000100u);
        Check(Read(base + 0x24u) == 256u && Read(base + 0x28u) == 0x01000100u,
              "DWC2 FIFO allocation");
        Write(0x14021000u, 3u);
        Write(0x14021004u, 7u);
        Write(0x1402101Cu, 0xF0000u);
        Write(0x140210C8u, 15u);
        Write(base + 8u, 1u);
        Write(base + 0x18u, 0x40u);
        Write(base + 0x804u, 0x80u);
        Check(Read(base + 0x804u) == 4u && (Read(0x140210C4u) & 8u) != 0u,
              "DWC2 global IN NAK status and shared interrupt");
        Write(0x1402101Cu, 0xE0000u);
        Check((Read(0x140210C4u) & 8u) == 0u, "DWC2 interrupt escaped clock gate");
        Write(0x1402101Cu, 0xF0000u);
        Write(base + 0x804u, 0x100u);
        Check(Read(base + 0x804u) == 0u && (Read(0x140210C4u) & 8u) == 0u,
              "DWC2 clear global IN NAK");
        Write(base + 0x18u, 0u);
        std::puts("PASS Core DWC2 reset, FIFO allocation, NAK commands, interrupt and clock gate");
    }
    void ResetDomains() {
        Write(0x10900018u, 0x7359u);
        Write(0x10100014u, 3u);
        Write(0x10200040u, 0xFFFFFFFFu);
        emu_.Get<cerfcore::GuestCpuReset>().WarmReset();
        Check(engine_.Get<cerfengine::GuestEngine>().Run() == cerfengine::GuestEngine::RunStatus::kRunning,
              "warm reset delivery halted");
        Check(Read(0x10900018u) == 0x7359u, "SYS scratch did not survive runtime reset");
        Check(Read(0x10100014u) == 0u, "UART LCR survived CPU reset");
        Check(Read(0x10200040u) == 0u, "GPIC mask survived CPU reset");
        emu_.Get<cerfcore::GuestCpuReset>().ColdReset();
        Check(engine_.Get<cerfengine::GuestEngine>().Run() == cerfengine::GuestEngine::RunStatus::kRunning,
              "cold reset delivery halted");
        Check(Read(0x10900018u) == 0u, "SYS scratch survived cold reset");
        std::puts("PASS Core warm/cold reset domains and reset delivery through Engine");
    }
    void Snapshot() {
        const auto parts = bus_.RegisteredPeripherals();
        Write(0x10900060u, 0x3Fu);
        Write(0x14020220u, 0x80001u);
        constexpr const char* path = "au1374-core-model-test.state";
        Write(0x10201014u, 2u);
        Put(0x42000u, 0xDEAD9876u);
        constexpr uint32_t command = 0x80000004u | (28u << 25u) | (31u << 20u);
        Arm({command, 4u, 0x42000u, 0u, 0x43000u, 0u, 0u, 0u});
        Write(0x10100100u, 3u);
        Write(0x10100014u, 3u);
        Write(0x10100018u, 0x10u);
        Write(0x10100004u, 0x51u);
        {
            cerfcore::StateWriter writer(emu_, path);
            for (auto* part : parts) {
                writer.BeginFrame(part->MmioBase());
                part->SaveState(writer);
                writer.EndFrame();
            }
            Check(writer.Commit(), "snapshot write failed");
        }
        const auto advance = [&] {
            Write(0x10200000u, 1u << 5u);
            Tick(10u * 16u * emu_.Get<cerfcore::Au1300ClockPeripheral>().Registers().PeripheralDivider());
            Check(Get(0x43000u) == 0xDEAD9876u, "restored DMA did not complete");
            Check(Read(0x10100000u) == 0x51u, "restored UART shift did not loop back");
        };
        const uint32_t saved_cycles = state_.guest_cycle_counter;
        advance();
        Tick(1000000u);
        Write(0x10900060u, 0x42u);
        Put(0x43000u, 0u);
        state_.guest_cycle_counter = saved_cycles;
        state_.guest_cycle_folded = saved_cycles;
        {
            cerfcore::StateReader reader(emu_, path);
            for (auto* part : parts) {
                Check(reader.EnterFrame() == part->MmioBase(), "snapshot frame identity");
                part->RestoreState(reader);
                reader.LeaveFrame();
            }
        }
        clock_.OnCyclesRestored();
        for (auto* part : parts) part->PostRestore();
        advance();
        Write(0x14020220u, 0x80000u);
        std::puts("PASS Core state round trip preserves DMA/UART and active USB across a CPU-rate change");
    }

    cerfcore::CerfEmulator& emu_;
    cerfengine::Engine& engine_;
    cerfengine::MipsCpuState& state_;
    cerfcore::GuestCycleClock& clock_;
    cerfcore::PeripheralDispatcher& bus_;
    cerfcore::EmulatedMemory& ram_;
};

}

int main(int argc, char** argv) {
    using namespace cerfcore;
    Log::SetFile("au1374-core-integration-test.log");
    Log::SetFlush(true);
    Log::SetEnabled(Log::MASK_ALL);
    CerfConfig config;
    config.device_override = "au1374";
    config.crash_log_file = "au1374-core-integration-test-crash.log";
    CerfEmulator emulator(config, argc, argv);
    emulator.CreateAllServices();
    const bool passed = emulator.Get<Fatal>().Guard([&] {
        emulator.ResolveAllSlots();
        emulator.ReadyAllServices();
        CoreIntegrationTest(emulator).Run();
    });
    Check(passed, "Core integration tests halted");
        std::puts("PASS twelve Core integration cases");
    return 0;
}
