#include "probe_sys.h"

#include "probe_host.h"

namespace au13xxprobe {

ProbeSys::ProbeSys(ProbeHost& host) : host_(host) {
    for (uint32_t offset : {0u, 4u, 8u, 12u, 16u, 24u, 28u, 44u, 64u, 68u,
                            72u, 76u, 80u, 84u, 88u}) {
        regs_[offset] = 0u;
    }
    regs_[0x14u] = 0x120u;
    regs_[0x20u] = 0u;
    regs_[0x24u] = 0x40903409u;
    regs_[0x28u] = 0x0A0E6287u;
    regs_[0x3Cu] = 2u;
    regs_[0x60u] = 0x42u;
    regs_[0x64u] = 0x14u;
    regs_[0x68u] = 0x3Fu;
}

uint32_t ProbeSys::CounterNow(const Counter& counter, uint32_t enable) {
    const uint32_t required = enable | (1u << 8u);
    if ((regs_.at(0x14u) & required) != required) return counter.value;
    const uint64_t delta = host_.Cycles() - counter.anchor;
    const uint64_t ticks = (delta * 32768u + counter.phase) /
                           (CpuHz() * (counter.trim + 1ull));
    return counter.value + static_cast<uint32_t>(ticks);
}

void ProbeSys::AnchorCounter(Counter& counter, uint32_t enable) {
    const uint64_t now = host_.Cycles();
    const uint32_t required = enable | (1u << 8u);
    if ((regs_.at(0x14u) & required) == required) {
        const uint64_t units = (now - counter.anchor) * 32768u + counter.phase;
        const uint64_t period = CpuHz() * (counter.trim + 1ull);
        counter.value += static_cast<uint32_t>(units / period);
        counter.ticks += units / period;
        counter.phase = units % period;
    }
    counter.anchor = now;
}

void ProbeSys::SetTrim(Counter& counter, uint32_t enable, uint32_t value) {
    AnchorCounter(counter, enable);
    const uint32_t trim = value & 0xFFFFu;
    counter.phase = counter.phase * (trim + 1ull) / (counter.trim + 1ull);
    counter.trim = trim;
}

uint64_t ProbeSys::NextTick(const Counter& counter, uint32_t enable) {
    const uint32_t required = enable | (1u << 8u);
    if ((regs_.at(0x14u) & required) != required) return UINT64_MAX;
    const uint64_t now = host_.Cycles();
    const uint64_t period = CpuHz() * (counter.trim + 1ull);
    const uint64_t units = (now - counter.anchor) * 32768u + counter.phase;
    return now + (period - units % period + 32767u) / 32768u;
}

void ProbeSys::Arm() {
    next_event_ = std::min(NextTick(toy_, 1u << 11u), NextTick(rtc_, 1u << 13u));
    host_.SetSysDeadline(next_event_);
}

void ProbeSys::TickCounter(Counter& counter, uint32_t enable, uint32_t source, uint32_t match_offset) {
    AnchorCounter(counter, enable);
    const uint64_t advanced = counter.ticks - counter.delivered;
    if (advanced == 0u) return;
    const uint32_t previous = counter.value - static_cast<uint32_t>(advanced);
    counter.delivered = counter.ticks;
    host_.Gpic().Pulse(source);
    for (uint32_t i = 0u; i < 3u; ++i) {
        const uint32_t distance = regs_.at(match_offset + 4u * i) - previous;
        const uint64_t ticks = distance != 0u ? distance : 0x100000000ull;
        if (advanced >= ticks) host_.Gpic().Pulse(source + 1u + i);
    }
}

void ProbeSys::Tick() {
    if (host_.Cycles() < next_event_) return;
    TickCounter(toy_, 1u << 11u, 80u, 8u);
    TickCounter(rtc_, 1u << 13u, 84u, 0x4Cu);
    Arm();
}

uint32_t ProbeSys::Read(uint32_t offset) {
    if (offset == 0x40u) return CounterNow(toy_, 1u << 11u);
    if (offset == 0x58u) return CounterNow(rtc_, 1u << 13u);
    const auto it = regs_.find(offset);
    if (it == regs_.end()) TestFail("SYS read offset=%X pc=%08X", offset, host_.Pc());
    return it->second;
}

void ProbeSys::Write(uint32_t offset, uint32_t value) {
    Tick();
    const uint32_t previous_divider = PeripheralDivider();
    if (offset == 0x3Cu) {
        for (uint32_t index = 0u; index < 4u; ++index) host_.Uart(index).Advance();
        host_.Dma().Tick();
    }
    if (!regs_.contains(offset) || offset == 0x40u || offset == 0x58u) {
        TestFail("SYS write offset=%X value=%08X pc=%08X", offset, value, host_.Pc());
    }
    if (offset == 0x14u && (value & (1u << 14u)) != 0u) {
        TestFail("external TOY/RTC counter clock bypass is unmodeled");
    }
    if (host_.Trace().Hit("SYS_WRITE", offset)) {
        std::printf("SYS_WRITE offset=%02X value=%08X pc=%08X\n", offset, value, host_.Pc());
    }
    if (offset == 0u) SetTrim(toy_, 1u << 11u, value);
    if (offset == 0x44u) SetTrim(rtc_, 1u << 13u, value);
    if (offset == 4u) {
        AnchorCounter(toy_, 1u << 11u);
        toy_.value = value;
    }
    if (offset == 0x48u) {
        AnchorCounter(rtc_, 1u << 13u);
        rtc_.value = value;
    }
    if (offset == 0x60u) {
        const uint64_t previous = regs_.at(offset) & 0x7Fu;
        value &= 0x7Fu;
        if (value == 0u) TestFail("zero CPUPLL is unmodeled");
        AnchorCounter(toy_, 1u << 11u);
        AnchorCounter(rtc_, 1u << 13u);
        toy_.phase = toy_.phase * value / previous;
        rtc_.phase = rtc_.phase * value / previous;
    }
    if (offset == 0x14u) {
        AnchorCounter(toy_, 1u << 11u);
        AnchorCounter(rtc_, 1u << 13u);
        constexpr uint32_t writable = (1u << 14u) | (1u << 13u) | (1u << 12u) |
                                      (1u << 11u) | (1u << 10u) | (1u << 8u);
        regs_[offset] = (value & writable) | 0x20u;
    } else {
        regs_[offset] = value;
    }
    if (offset == 0x3Cu) {
        host_.Dma().ClockChanged(previous_divider / 2u, BusDivider());
        for (uint32_t index = 0u; index < 4u; ++index) {
            host_.Uart(index).ClockChanged(previous_divider, PeripheralDivider());
        }
    }
    Arm();
}

uint32_t ProbeSys::PeripheralDivider() const {
    return 2u * BusDivider();
}

uint32_t ProbeSys::BusDivider() const {
    return (regs_.at(0x3Cu) & 3u) + 2u;
}

uint64_t ProbeSys::CpuHz() const {
    return 12000000ull * (regs_.at(0x60u) & 0x7Fu);
}

}
