#include "probe_host.h"

#include <cerfengine/cycle_deadline.h>

namespace au13xxprobe {

ProbeHost::ProbeHost(uint16_t external)
    : TestHost(0u, 0x10000000u, 0xFFFFFFFFu), external_(external), gpic_(*this),
      sys_(*this), dma_(*this), uarts_{ProbeUart(*this, 0u), ProbeUart(*this, 1u),
                                     ProbeUart(*this, 2u), ProbeUart(*this, 3u)} {}

void ProbeHost::Bind(cerfengine::MipsCpuState& state) {
    state_ = &state;
    folded_ = state.guest_cycle_counter;
    cycles_ = folded_;
    Tick();
}

void ProbeHost::BindEngine(cerfengine::Engine& engine) {
    coherence_ = &engine.Get<cerfengine::MipsBusMasterCoherence>();
}

void ProbeHost::Attach(cerfengine::Setup& setup) {
    TestHost::Attach(setup);
    setup.mmio.context = this;
}

uint64_t ProbeHost::Cycles() {
    if (!state_) return 0u;
    const uint32_t current = state_->guest_cycle_counter;
    cycles_ += static_cast<uint32_t>(current - folded_);
    folded_ = current;
    return cycles_;
}

void ProbeHost::Tick() {
    DispatchClock();
    sys_.Tick();
    for (auto& uart : uarts_) uart.Advance();
    dma_.Tick();
    PublishDeadline();
}

void ProbeHost::SetCpuDeadline(uint64_t cycle) {
    cpu_deadline_ = cycle;
    PublishDeadline();
}

void ProbeHost::SetSysDeadline(uint64_t cycle) {
    sys_deadline_ = cycle;
    PublishDeadline();
}

void ProbeHost::SetDmaDeadline(uint64_t cycle) {
    if (dma_deadline_ == cycle) return;
    dma_deadline_ = cycle;
    PublishDeadline();
}

void ProbeHost::SetUartDeadline(uint32_t index, uint64_t cycle) {
    if (uart_deadlines_.at(index) == cycle) return;
    uart_deadlines_.at(index) = cycle;
    PublishDeadline();
}

void ProbeHost::SetGpicLines(uint32_t lines) {
    state_->cp0_cause = (state_->cp0_cause & ~0x3C00u) | lines;
}

void ProbeHost::PublishDeadline() {
    if (!state_) return;
    const uint64_t now = Cycles();
    uint64_t next = std::min(cpu_deadline_, sys_deadline_);
    next = std::min(next, dma_deadline_);
    for (uint64_t deadline : uart_deadlines_) next = std::min(next, deadline);
    const uint64_t remaining = next > now ? next - now : 0u;
    state_->guest_cycle_deadline = cerfengine::CycleDeadline(
        state_->guest_cycle_counter, std::min<uint64_t>(remaining, 100000u));
}

void ProbeHost::Summary() {
    for (auto& uart : uarts_) uart.FlushLine();
    trace_.Summary();
    nand_.Summary();
}

uint32_t ProbeHost::Pc() const {
    return state_ ? state_->pc : 0u;
}

ProbeTrace& ProbeHost::Trace() {
    return trace_;
}

ProbeSys& ProbeHost::Sys() {
    return sys_;
}

ProbeNand& ProbeHost::Nand() {
    return nand_;
}

ProbeGpic& ProbeHost::Gpic() {
    return gpic_;
}

ProbeDma& ProbeHost::Dma() {
    return dma_;
}

ProbeUart& ProbeHost::Uart(uint32_t index) {
    return uarts_.at(index);
}

uint32_t ProbeHost::Read(uint32_t pa, uint32_t width) {
    if (pa >= 0x10100000u && pa < 0x10104000u && width == 1u && (pa & 0xFFFu) == 0u) {
        return uarts_[(pa - 0x10100000u) / 0x1000u].Read(0u);
    }
    if (pa >= 0x1FE00000u && pa < 0x1FE01000u) {
        if (width != 1u && width != 2u && width != 4u) TestFail("external read width=%u", width);
        uint32_t value = 0u;
        for (uint32_t byte = 0u; byte < width; ++byte) {
            value |= ((external_ >> (((pa + byte) & 1u) * 8u)) & 0xFFu) << (byte * 8u);
        }
        if (trace_.Hit("EXTERNAL_READ", pa & 0xFFFu)) {
            std::printf("EXTERNAL_READ pa=%08X width=%u value=%08X\n", pa, width, value);
        }
        return value;
    }
    if (pa == 0x1C000020u) {
        if (width != 1u) TestFail("8-bit NAND read width=%u", width);
        return nand_.Read();
    }
    if (width != 4u || (pa & 3u) != 0u) TestFail("MMIO read pa=%08X width=%u pc=%08X", pa, width, Pc());
    if (pa >= 0x10100000u && pa < 0x10104000u) return uarts_[(pa - 0x10100000u) / 0x1000u].Read(pa & 0xFFFu);
    if (pa >= 0x10200000u && pa < 0x10202000u) return gpic_.Read(pa - 0x10200000u);
    if (pa >= 0x10900000u && pa < 0x10901000u) return sys_.Read(pa - 0x10900000u);
    if (pa >= 0x14002000u && pa < 0x14003010u) return dma_.Read(pa - 0x14002000u);
    if (pa == 0x14001100u) return nand_control_;
    if (pa == 0x14001104u) return 1u;
    TestFail("unmodeled read pa=%08X width=%u pc=%08X", pa, width, Pc());
}

void ProbeHost::Write(uint32_t pa, uint32_t value, uint32_t width) {
    if (pa >= 0x10100000u && pa < 0x10104000u && width == 1u && (pa & 0xFFFu) == 4u) {
        uarts_[(pa - 0x10100000u) / 0x1000u].Write(4u, value);
        return;
    }
    if (pa == 0x1C000000u || pa == 0x1C000004u || pa == 0x1C000020u) {
        if (width != 1u) TestFail("8-bit NAND write width=%u", width);
        if (pa == 0x1C000000u) nand_.Command(static_cast<uint8_t>(value));
        else if (pa == 0x1C000004u) nand_.Address(static_cast<uint8_t>(value));
        else nand_.Write(static_cast<uint8_t>(value));
        return;
    }
    if (width != 4u || (pa & 3u) != 0u) TestFail("MMIO write pa=%08X width=%u pc=%08X", pa, width, Pc());
    if (pa >= 0x10100000u && pa < 0x10104000u) {
        uarts_[(pa - 0x10100000u) / 0x1000u].Write(pa & 0xFFFu, value);
        return;
    }
    if (pa >= 0x10200000u && pa < 0x10202000u) {
        sys_.Tick();
        gpic_.Write(pa - 0x10200000u, value);
        return;
    }
    if (pa >= 0x10900000u && pa < 0x10901000u) {
        sys_.Write(pa - 0x10900000u, value);
        return;
    }
    if (pa >= 0x14002000u && pa < 0x14003010u) {
        dma_.Write(pa - 0x14002000u, value);
        return;
    }
    if (pa == 0x14001100u) {
        if (trace_.Hit("NAND_CONTROL", pa)) std::printf("NAND_CONTROL value=%08X\n", value);
        nand_control_ = value;
        return;
    }
    TestFail("unmodeled write pa=%08X value=%08X width=%u pc=%08X", pa, value, width, Pc());
}

bool ProbeHost::Route(void* context, uint32_t pa, cerfengine::MmioRoute& route) {
    if (pa < 0x10000000u) return false;
    route.base = 0u;
    route.last = 0xFFFFFFFFu;
    route.context = context;
    route.read = &RouteRead;
    route.write = &RouteWrite;
    return true;
}

uint32_t CERFENGINE_CDECL ProbeHost::RouteRead(void* context, uint32_t offset, uint32_t width) {
    return static_cast<ProbeHost*>(context)->Read(offset, width);
}

void CERFENGINE_CDECL ProbeHost::RouteWrite(void* context, uint32_t offset, uint32_t value,
                                            uint32_t width) {
    static_cast<ProbeHost*>(context)->Write(offset, value, width);
}

}
