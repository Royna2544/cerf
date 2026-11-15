#pragma once

#include <cstdint>
#include <map>

namespace au13xxprobe {

class ProbeHost;

class ProbeSys {
public:
    explicit ProbeSys(ProbeHost& host);
    uint32_t Read(uint32_t offset);
    void Write(uint32_t offset, uint32_t value);
    void Tick();
    uint32_t PeripheralDivider() const;
    uint32_t BusDivider() const;
    uint64_t CpuHz() const;

private:
    struct Counter {
        uint32_t value = 0u;
        uint32_t trim = 0u;
        uint64_t anchor = 0u;
        uint64_t phase = 0u;
        uint64_t ticks = 0u;
        uint64_t delivered = 0u;
    };
    uint32_t CounterNow(const Counter& counter, uint32_t enable);
    void AnchorCounter(Counter& counter, uint32_t enable);
    void SetTrim(Counter& counter, uint32_t enable, uint32_t value);
    uint64_t NextTick(const Counter& counter, uint32_t enable);
    void TickCounter(Counter& counter, uint32_t enable, uint32_t source, uint32_t match_offset);
    void Arm();
    ProbeHost& host_;
    std::map<uint32_t, uint32_t> regs_;
    Counter toy_;
    Counter rtc_;
    uint64_t next_event_ = UINT64_MAX;
};

}
