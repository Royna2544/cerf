#include "probe_uart.h"

#include "probe_host.h"

namespace au13xxprobe {

ProbeUart::ProbeUart(ProbeHost& host, uint32_t index) : host_(host), index_(index) {
    Reset();
}

void ProbeUart::Reset() {
    for (uint32_t offset : {8u, 16u, 20u, 24u, 36u, 40u, 256u, 260u, 264u}) regs_[offset] = 0u;
    regs_[0x28u] = 1u;
    fifo_.clear();
    receive_fifo_.clear();
    shift_.reset();
    overrun_ = false;
    break_received_ = false;
    break_pending_ = false;
    break_deadline_ = UINT64_MAX;
    receive_deadline_ = UINT64_MAX;
    modem_status_ = external_modem_;
    next_event_ = 0u;
}

uint64_t ProbeUart::FrameCycles() const {
    const uint32_t lcr = regs_.at(0x14u);
    const uint32_t data_bits = 5u + (lcr & 3u);
    const uint32_t parity = (lcr & 8u) != 0u ? 1u : 0u;
    const uint32_t stop_halves = (lcr & 4u) == 0u ? 2u : (data_bits == 5u ? 3u : 4u);
    const uint32_t frame_halves = 2u * (1u + data_bits + parity) + stop_halves;
    const uint32_t divisor = regs_.at(0x28u) & 0xFFFFu;
    if (divisor == 0u) TestFail("UART%u zero clock divider during transfer", index_);
    return static_cast<uint64_t>(frame_halves) * 8u * divisor * host_.Sys().PeripheralDivider();
}

uint64_t ProbeUart::TimeoutCycle() const {
    return receive_deadline_;
}

void ProbeUart::ClockChanged(uint64_t previous, uint64_t current) {
    const uint64_t now = regs_.at(0x100u) == 2u ? pause_anchor_ : host_.Cycles();
    const auto rescale = [=](uint64_t deadline) {
        if (deadline == UINT64_MAX || deadline <= now) return deadline;
        return now + ((deadline - now) * current + previous - 1u) / previous;
    };
    if (shift_) deadline_ = rescale(deadline_);
    receive_deadline_ = rescale(receive_deadline_);
    break_deadline_ = rescale(break_deadline_);
    Publish();
}

uint32_t ProbeUart::ReceiveThreshold() const {
    constexpr uint32_t thresholds[] = {1u, 4u, 8u, 14u};
    return (regs_.at(0x10u) & 1u) != 0u ? thresholds[(regs_.at(0x10u) >> 6u) & 3u] : 1u;
}

uint32_t ProbeUart::TransmitThreshold() const {
    return (regs_.at(0x10u) & 1u) != 0u ? ((regs_.at(0x10u) >> 4u) & 3u) * 4u : 0u;
}

bool ProbeUart::BreakVisible() const {
    return break_pending_ && (receive_fifo_.empty() ||
           (receive_fifo_.front().errors & 0x10u) != 0u);
}

uint32_t ProbeUart::InterruptCause() const {
    const uint32_t enable = regs_.at(8u);
    if ((enable & 4u) != 0u && (overrun_ || BreakVisible() ||
        (!receive_fifo_.empty() && receive_fifo_.front().errors != 0u))) return 6u;
    if ((enable & 1u) != 0u && !receive_fifo_.empty()) {
        const uint64_t now = regs_.at(0x100u) == 2u ? pause_anchor_ : host_.Cycles();
        if (now >= TimeoutCycle()) return 12u;
        if (receive_fifo_.size() >= ReceiveThreshold()) return 4u;
    }
    if ((enable & 2u) != 0u && fifo_.size() <= TransmitThreshold()) return 2u;
    if ((enable & 8u) != 0u && (modem_status_ & 15u) != 0u) return 0u;
    return 1u;
}

void ProbeUart::Publish() {
    UpdateModem();
    constexpr uint32_t interrupts[] = {88u, 17u, 25u, 27u};
    const bool working = regs_.at(0x100u) == 3u;
    host_.Gpic().SetLevel(interrupts[index_], working && InterruptCause() != 1u);
    const bool threshold_mode = (regs_.at(0x10u) & 8u) != 0u;
    host_.Dma().SetRequest(index_ * 2u, working && (threshold_mode ?
        fifo_.size() <= TransmitThreshold() : !shift_ && fifo_.empty()));
    host_.Dma().SetRequest(index_ * 2u + 1u, working && (threshold_mode ?
        receive_fifo_.size() >= ReceiveThreshold() : !receive_fifo_.empty()));
    uint64_t next = working && shift_ ? deadline_ : UINT64_MAX;
    if (working && !receive_fifo_.empty() && (regs_.at(8u) & 1u) != 0u &&
        TimeoutCycle() > host_.Cycles()) next = std::min(next, TimeoutCycle());
    if (working) next = std::min(next, break_deadline_);
    next_event_ = next;
    host_.SetUartDeadline(index_, next);
}

void ProbeUart::UpdateModem() {
    const uint32_t control = regs_.at(0x18u);
    uint32_t current = external_modem_;
    if ((control & 0x10u) != 0u) {
        const uint32_t rts = (regs_.at(0x24u) & 1u) != 0u ?
            (receive_fifo_.size() < ReceiveThreshold() ? 2u : 0u) : control & 2u;
        current = (rts << 3u) | ((control & 1u) << 5u) |
                  ((control & 4u) << 4u) | ((control & 8u) << 4u);
    }
    const uint32_t disabled = regs_.at(0x104u);
    const uint32_t disabled_status = ((disabled & 1u) << 4u) | ((disabled & 2u) << 6u) |
                                     ((disabled & 4u) << 3u) | ((disabled & 8u) << 3u);
    current &= ~disabled_status;
    const uint32_t changed = (modem_status_ ^ current) & 0xF0u;
    const uint32_t delta = ((changed & 0x10u) >> 4u) | ((changed & 0x20u) >> 4u) |
                           ((changed & 0x80u) >> 4u) | ((changed & modem_status_ & 0x40u) >> 4u);
    modem_status_ = current | (modem_status_ & 15u) | delta;
}

void ProbeUart::SetModemInputs(uint32_t value) {
    Advance();
    external_modem_ = value & 0xF0u;
    UpdateModem();
    Start(host_.Cycles());
    Publish();
}

void ProbeUart::Start(uint64_t cycle) {
    if (shift_ || fifo_.empty() || regs_.at(0x100u) != 3u) return;
    if ((regs_.at(0x24u) & 1u) != 0u &&
        ((external_modem_ & 0x10u) == 0u || (regs_.at(0x104u) & 1u) != 0u)) return;
    shift_ = fifo_.front();
    fifo_.pop_front();
    deadline_ = cycle + FrameCycles();
}

void ProbeUart::Emit(uint8_t value) {
    if (value == '\r') return;
    if (value == '\n') {
        if (index_ == 2u) std::printf("UART %s\n", line_.c_str());
        else std::printf("UART%u %s\n", index_, line_.c_str());
        line_.clear();
    } else {
        line_.push_back(static_cast<char>(value));
        if (line_.size() == 4096u) FlushLine();
    }
}

void ProbeUart::Receive(uint8_t value, uint8_t errors, uint64_t cycle) {
    if (break_pending_) return;
    const uint32_t capacity = (regs_.at(0x10u) & 1u) != 0u ? 16u : 1u;
    if (receive_fifo_.size() == capacity) overrun_ = true;
    else receive_fifo_.push_back({value, errors});
    if ((errors & 0x10u) != 0u) break_pending_ = true;
    receive_deadline_ = cycle + 0x300ull * regs_.at(0x28u) * host_.Sys().PeripheralDivider();
}

void ProbeUart::Advance() {
    const uint64_t now = host_.Cycles();
    if (now < next_event_) return;
    if (regs_.at(0x100u) == 3u) {
        while (shift_ && deadline_ <= now) {
            if ((regs_.at(0x14u) & 0x40u) == 0u) {
                if ((regs_.at(0x18u) & 0x10u) != 0u) Receive(*shift_, 0u, deadline_);
                else Emit(*shift_);
            }
            shift_.reset();
            Start(deadline_);
        }
        if (break_deadline_ <= now) {
            Receive(0u, 0x18u, break_deadline_);
            break_received_ = true;
            break_deadline_ = UINT64_MAX;
        }
    }
    Publish();
}

void ProbeUart::FlushLine() {
    if (!line_.empty()) std::printf("UART%u_PARTIAL %s\n", index_, line_.c_str());
    line_.clear();
}

uint32_t ProbeUart::Read(uint32_t offset) {
    Advance();
    if (offset == 0u) {
        const uint32_t value = receive_fifo_.empty() ? 0u : receive_fifo_.front().value;
        if (!receive_fifo_.empty()) receive_fifo_.pop_front();
        receive_deadline_ = host_.Cycles() + 0x300ull * regs_.at(0x28u) * host_.Sys().PeripheralDivider();
        Publish();
        return value;
    }
    if (offset == 0x1Cu) {
        uint32_t status = (fifo_.size() <= TransmitThreshold() ? 0x20u : 0u) |
                           (!shift_ && fifo_.empty() ? 0x40u : 0u) | (overrun_ ? 2u : 0u);
        if (!receive_fifo_.empty()) status |= 1u | receive_fifo_.front().errors;
        if (BreakVisible()) {
            status |= 0x10u;
            break_pending_ = false;
        }
        for (const auto& received : receive_fifo_) if (received.errors != 0u) status |= 0x80u;
        overrun_ = false;
        if (!receive_fifo_.empty()) receive_fifo_.front().errors = 0u;
        Publish();
        return status;
    }
    if (offset == 0xCu) return InterruptCause();
    if (offset == 0x20u) {
        const uint32_t status = modem_status_;
        modem_status_ &= 0xF0u;
        Publish();
        return status;
    }
    const auto it = regs_.find(offset);
    if (it == regs_.end()) TestFail("UART%u read offset=%X pc=%08X", index_, offset, host_.Pc());
    return it->second;
}

void ProbeUart::Write(uint32_t offset, uint32_t value) {
    Advance();
    if (offset == 0x108u && (value & 3u) != 0u) {
        TestFail("UART%u bidirectional gating and open-drain output are unmodeled", index_);
    }
    const uint32_t previous_divisor = regs_.at(0x28u);
    if (offset != 4u && !regs_.contains(offset)) TestFail("UART%u write offset=%X pc=%08X", index_, offset, host_.Pc());
    if (offset < 0x100u && (regs_.at(0x100u) & 2u) == 0u) return;
    if (offset == 4u) {
        if (regs_.at(0x100u) != 3u) TestFail("UART%u TX while disabled", index_);
        const size_t capacity = (regs_.at(0x10u) & 1u) != 0u ? 16u : 1u;
        if (fifo_.size() >= capacity) TestFail("UART%u TX FIFO overflow", index_);
        const uint32_t mask = (1u << (5u + (regs_.at(0x14u) & 3u))) - 1u;
        fifo_.push_back(static_cast<uint8_t>(value & mask));
        Start(host_.Cycles());
        Publish();
        return;
    }
    if (host_.Trace().Hit("UART_CONTROL", (index_ << 12u) | offset)) {
        std::printf("UART_CONTROL port=%u offset=%03X value=%08X\n", index_, offset, value);
    }
    if (offset == 0x100u) {
        value &= 3u;
        const uint32_t previous = regs_.at(offset);
        if ((value & 2u) == 0u) Reset();
        else if (previous == 3u && value == 2u) pause_anchor_ = host_.Cycles();
        else if (previous == 2u && value == 3u) {
            const uint64_t elapsed = host_.Cycles() - pause_anchor_;
            if (shift_) deadline_ += elapsed;
            if (receive_deadline_ != UINT64_MAX) receive_deadline_ += elapsed;
            if (break_deadline_ != UINT64_MAX) break_deadline_ += elapsed;
        }
    }
    if (offset == 0x10u) {
        if ((value & 4u) != 0u || ((value ^ regs_.at(offset)) & 1u) != 0u) fifo_.clear();
        if ((value & 2u) != 0u || ((value ^ regs_.at(offset)) & 1u) != 0u) {
            receive_fifo_.clear();
            overrun_ = false;
        }
        value &= 0xF9u;
    }
    if (offset == 8u || offset == 0x104u) value &= 15u;
    if (offset == 0x14u) value &= 0x7Fu;
    if (offset == 0x18u) value &= 0x1Fu;
    if (offset == 0x24u) value &= 1u;
    if (offset == 0x108u) value &= 7u;
    if (offset == 0x28u) {
        value &= 0xFFFFu;
        if (value == 0u) TestFail("UART%u zero clock divider is unmodeled", index_);
    }
    regs_[offset] = value;
    if (offset == 0x28u) ClockChanged(previous_divisor, value);
    if (offset == 0x18u || offset == 0x104u) UpdateModem();
    const bool loop_break = (regs_.at(0x18u) & 0x10u) != 0u && (regs_.at(0x14u) & 0x40u) != 0u;
    if (!loop_break) {
        break_received_ = false;
        break_deadline_ = UINT64_MAX;
    } else if (!break_received_ && break_deadline_ == UINT64_MAX && regs_.at(0x100u) == 3u) {
        break_deadline_ = host_.Cycles() + FrameCycles();
    }
    Start(host_.Cycles());
    Publish();
}

}
