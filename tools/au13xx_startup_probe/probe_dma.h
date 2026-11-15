#pragma once

#include <array>
#include <cstdint>
#include <deque>

namespace au13xxprobe {

class ProbeHost;

class ProbeDma {
public:
    explicit ProbeDma(ProbeHost& host);
    uint32_t Read(uint32_t offset);
    void Write(uint32_t offset, uint32_t value);
    void SetRequest(uint32_t source, bool level);
    void Tick();
    void ClockChanged(uint32_t previous, uint32_t current);

private:
    struct Cursor {
        uint32_t pointer = 0u;
        uint32_t block_start = 0u;
        uint32_t group_start = 0u;
        uint32_t movement = 0u;
        uint32_t block = 0u;
        uint32_t stride = 0u;
        uint32_t columns = 0u;
        uint32_t column = 0u;
        uint32_t in_block = 0u;
        uint32_t burst = 1u;
        void Reset(uint32_t address, uint32_t config, bool two_dimensions);
        uint32_t Address(uint32_t width) const;
        void Advance(uint32_t bytes);
    };
    struct Channel {
        std::array<uint32_t, 7u> regs{};
        std::array<uint32_t, 8u> descriptor{};
        std::deque<uint8_t> fifo;
        std::array<uint64_t, 2u> edge_seen{};
        std::array<std::array<uint64_t, 2u>, 32u> edge_baseline{};
        Cursor source;
        Cursor destination;
        uint32_t source_remaining = 0u;
        uint32_t literal_offset = 0u;
        uint32_t compare_value = 0u;
        uint32_t source_grant = 0u;
        uint32_t destination_grant = 0u;
    };
    bool Fetch(Channel& channel);
    bool FetchNext();
    bool Step(Channel& channel);
    bool Requested(Channel& channel, bool destination);
    void Complete(Channel& channel, uint32_t next);
    void Publish();
    void AdvanceThrottle();
    void ArmThrottle();
    ProbeHost& host_;
    std::array<Channel, 16u> channels_;
    std::array<bool, 32u> requests_{};
    std::array<std::array<uint64_t, 2u>, 32u> edges_{};
    std::array<uint32_t, 2u> round_robin_{};
    uint32_t config_ = 0u;
    uint32_t throttle_ = 0u;
    uint32_t interrupt_enable_ = 0xFFFFu;
    uint32_t high_grants_ = 0u;
    uint32_t fetch_round_robin_ = 0u;
    uint64_t throttle_epoch_ = 0u;
    uint64_t throttle_phase_ = 0u;
    bool pumping_ = false;
};

}
