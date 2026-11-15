#pragma once

#include <array>
#include <cstdint>
#include <map>

namespace au13xxprobe {

class ProbeHost;

class ProbeGpic {
public:
    explicit ProbeGpic(ProbeHost& host);
    uint32_t Read(uint32_t offset);
    void Write(uint32_t offset, uint32_t value);
    void Pulse(uint32_t source);
    void SetLevel(uint32_t source, bool level);

private:
    void Publish();
    ProbeHost& host_;
    std::map<uint32_t, uint32_t> regs_;
    std::array<bool, 128u> levels_{};
};

}
