#include "probe_host.h"
#include <string_view>

namespace {

class DmaTest {
public:
    DmaTest();
    void Run();
    void InvalidStride(bool two_dimensions);
    void NarrowStride(uint32_t width);
    void InvalidGpioSelection() { host_.Write(0x10200060u, 0x80u, 4u); }

private:
    void Arm(const std::array<uint32_t, 8u>& descriptor, uint32_t config = 0u);
    void CopyAndInterrupt();
    void Strides();
    void LiteralAndBranch();
    void CompareWidths();
    void BurstPressure();
    void Throttle();
    void FetchArbitration();
    void Requests();
    void Request(uint32_t source, bool level);
    void GpioSelection();
    au13xxprobe::ProbeHost host_;
    cerfengine::MipsCpuState state_{};
};

DmaTest::DmaTest() : host_(0xFFFFu) {
    host_.Bind(state_);
    host_.Write(0x10200060u, 0x4A49u, 4u);
}

void DmaTest::Arm(const std::array<uint32_t, 8u>& descriptor, uint32_t config) {
    host_.Write(0x14002000u, 0u, 4u);
    host_.Write(0x14002014u, 6u, 4u);
    host_.Write(0x14002010u, 0u, 4u);
    host_.LoadWords(0x10000u, descriptor.data(), descriptor.size());
    host_.Write(0x14002004u, 0x10000u, 4u);
    host_.Write(0x14002000u, config | 1u, 4u);
    host_.Write(0x1400200Cu, 0u, 4u);
}

void DmaTest::CopyAndInterrupt() {
    host_.Write(0x10201000u + 4u * 75u, 0x21u, 4u);
    host_.Write(0x10200048u, 1u << (75u % 32u), 4u);
    TEST_CHECK(host_.Read(0x1400300Cu, 4u) == 0xFFFFu);
    const std::array<uint32_t, 4u> source = {0x12345678u, 0xAABBCCDDu, 0x87654321u, 0xDEADBEEFu};
    host_.LoadWords(0x20000u, source.data(), source.size());
    const uint32_t command = 0xC0000104u | (31u << 25u) | (31u << 20u) | (2u << 18u);
    Arm({command, 16u, 0x20000u, 0u, 0x30000u, 0u, 0u, 0u});
    for (uint32_t i = 0u; i < source.size(); ++i) {
        TEST_CHECK(host_.ReadPhysical(0x30000u + 4u * i, 4u) == source[i]);
    }
    TEST_CHECK(host_.Read(0x14002004u, 4u) == 0u);
    TEST_CHECK((host_.ReadPhysical(0x10000u, 4u) & 0x80000000u) == 0u);
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 0u);
    TEST_CHECK(host_.Read(0x14003004u, 4u) == 1u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) != 0u);
    host_.Write(0x10200028u, 1u << (75u % 32u), 4u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) != 0u);
    host_.Write(0x1400300Cu, 0u, 4u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) == 0u);
    TEST_CHECK(host_.Read(0x14003004u, 4u) == 1u);
    host_.Write(0x1400300Cu, 0xFFFFu, 4u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) != 0u);
    host_.Write(0x14002010u, 0u, 4u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) == 0u);
    Arm({command & ~0x100u, 4u, 0x20000u, 0u, 0x30010u, 0u, 0u, 0u}, 4u);
    TEST_CHECK(host_.ReadPhysical(0x30010u, 4u) == 0x78563412u);
    Arm({command & ~0x100u, 4u, 0x20003u, 1u << 28u, 0x30014u, 0u, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x30014u, 4u) == 0x12345678u);
}

void DmaTest::Strides() {
    const std::array<uint32_t, 8u> source = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    host_.LoadWords(0x21000u, source.data(), source.size());
    const uint32_t command = 0xC0000004u | (31u << 25u) | (31u << 20u) |
                             (2u << 18u) | (2u << 16u);
    Arm({command, 32u, 0x21000u, 0u, 0x31000u, (8u << 14u) | 16u, 0u, 0u});
    for (uint32_t i = 0u; i < source.size(); ++i) {
        const uint32_t address = 0x31000u + (i / 2u) * 16u + (i % 2u) * 4u;
        TEST_CHECK(host_.ReadPhysical(address, 4u) == source[i]);
    }
    TEST_CHECK(host_.ReadPhysical(0x31008u, 4u) == 0u);
    Arm({command | 0x400u, 32u, 0x21000u, 0u, 0x32000u,
         (4u << 21u) | (8u << 11u) | 4u, 0u, 0u});
    const std::array<uint32_t, 8u> expected = {1u, 5u, 2u, 6u, 3u, 7u, 4u, 8u};
    for (uint32_t i = 0u; i < expected.size(); ++i) {
        TEST_CHECK(host_.ReadPhysical(0x32000u + 4u * i, 4u) == expected[i]);
    }
}

void DmaTest::LiteralAndBranch() {
    const uint32_t literal = 0x80002006u | (31u << 20u) | (2u << 18u) | (2u << 16u);
    host_.Write(0x14002008u, 0x33020u, 4u);
    Arm({literal, 8u, 0x01234567u, 0x89ABCDEFu, 0x33000u, 0u, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x33000u, 4u) == 0x01234567u);
    TEST_CHECK(host_.ReadPhysical(0x33004u, 4u) == 0x89ABCDEFu);
    TEST_CHECK(host_.ReadPhysical(0x33020u, 4u) == (literal & 0x7FFFFFFFu));
    Arm({literal, 8u, 0x01234567u, 0xF1234567u, 0x33030u, 0u, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x33030u, 4u) == 0x01234567u);
    TEST_CHECK(host_.ReadPhysical(0x33034u, 4u) == 0xF1234567u);
    const std::array<uint32_t, 8u> branch_target = {
        literal, 4u, 0x2468ABCDu, 0u, 0x34000u, 0u, 0u, 0u};
    host_.LoadWords(0x10040u, branch_target.data(), branch_target.size());
    host_.WritePhysical(0x34010u, 0xABu, 1u);
    const uint32_t branch = 0x80004004u | (31u << 25u) | (2u << 16u);
    Arm({branch, 0x10040u >> 5u, 0x34010u, 0u, 0xABu, 0xFFu, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x34000u, 4u) == 0x2468ABCDu);
    host_.WritePhysical(0x34000u, 0u, 4u);
    host_.LoadWords(0x10040u, branch_target.data(), branch_target.size());
    Arm({branch, 0x10040u >> 5u, 0x34010u, 0u, 0xCDu, 0xFFu, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x34000u, 4u) == 0u);
}

void DmaTest::Request(uint32_t source, bool level) {
    TEST_CHECK(source == 28u || source == 29u);
    host_.Gpic().SetLevel(73u + source - 28u, level);
}

void DmaTest::GpioSelection() {
    host_.WritePhysical(0x3C000u, 0xA1B2C3D4u, 4u);
    const uint32_t source = 0x80000004u | (28u << 25u) | (31u << 20u);
    host_.Gpic().SetLevel(5u, true);
    Arm({source, 4u, 0x3C000u, 0u, 0x3D000u, 0u, 0u, 0u});
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 4u);
    host_.Write(0x10200060u, 5u | (6u << 8u), 4u);
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x3D000u, 4u) == 0xA1B2C3D4u);
    host_.Gpic().SetLevel(5u, false);
    Arm({source, 4u, 0x3C000u, 0u, 0x3D004u, 0u, 0u, 0u});
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 4u);
    host_.Write(0x10200000u, 1u << 5u, 4u);
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x3D004u, 4u) == 0xA1B2C3D4u);
    const uint32_t destination = 0x80000004u | (31u << 25u) | (29u << 20u);
    Arm({destination, 4u, 0x3C000u, 0u, 0x3D008u, 0u, 0u, 0u});
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 4u);
    host_.Gpic().SetLevel(6u, true);
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x3D008u, 4u) == 0xA1B2C3D4u);
    host_.Write(0x10200090u, 1u << 5u, 4u);
    host_.Gpic().SetLevel(6u, false);
    host_.Write(0x10200060u, 0x4A49u, 4u);
}

void DmaTest::Requests() {
    host_.WritePhysical(0x35000u, 0x11223344u, 4u);
    const uint32_t command = 0x80000004u | (28u << 25u) | (31u << 20u);
    const std::array<uint32_t, 8u> descriptor = {command, 4u, 0x35000u, 0u, 0x36000u, 0u, 0u, 0u};
    Arm(descriptor);
    TEST_CHECK(host_.ReadPhysical(0x36000u, 4u) == 0u);
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 4u);
    Request(28u, true);
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x36000u, 4u) == 0x11223344u);
    Request(28u, false);
    host_.WritePhysical(0x36000u, 0u, 4u);
    Arm(descriptor, 1u << 9u);
    host_.Tick();
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 4u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        Request(28u, true);
        host_.Tick();
        Request(28u, false);
        host_.Tick();
    }
    TEST_CHECK(host_.ReadPhysical(0x36000u, 4u) == 0x11223344u);
    TEST_CHECK((host_.ReadPhysical(0x10000u, 4u) & 0x80000000u) == 0u);
}

void DmaTest::CompareWidths() {
    const uint32_t literal = 0x80002004u | (31u << 20u) | (2u << 18u) | (2u << 16u);
    const auto target = std::array<uint32_t, 8u>{literal, 4u, 0xAABBCCDDu, 0u, 0x37000u, 0u, 0u, 0u};
    host_.LoadWords(0x10040u, target.data(), target.size());
    const uint32_t word_branch = 0x80004004u | (31u << 25u) | (2u << 18u) | (2u << 16u);
    Arm({word_branch, 0x10040u >> 5u, 0x10900060u, 0u, 0x42u, 0xFFu, 0u, 0u});
    TEST_CHECK(host_.ReadPhysical(0x37000u, 4u) == 0xAABBCCDDu);
    host_.WritePhysical(0x37000u, 0u, 4u);
    host_.LoadWords(0x10040u, target.data(), target.size());
    host_.Write(0x10100100u, 3u, 4u);
    host_.Write(0x10100014u, 3u, 4u);
    host_.Write(0x10100010u, 1u, 4u);
    host_.Write(0x10100018u, 0x10u, 4u);
    for (uint32_t value : {0x11u, 0x22u, 0x33u, 0x44u}) host_.Write(0x10100004u, value, 4u);
    state_.guest_cycle_counter += 5120u;
    host_.Tick();
    const uint32_t byte_branch = 0x80004004u | (1u << 25u) | (2u << 16u);
    Arm({byte_branch, 0x10040u >> 5u, 0x10100000u, 0u, 0x44332211u, 0xFFFFFFFFu, 0u, 3u << 28u});
    TEST_CHECK(host_.ReadPhysical(0x37000u, 4u) == 0xAABBCCDDu);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 1u) == 0u);
}

void DmaTest::BurstPressure() {
    Request(28u, false);
    Request(29u, false);
    const std::array<uint32_t, 10u> data = {0x03020100u, 0x07060504u, 0x0B0A0908u,
        0x0F0E0D0Cu, 0x13121110u, 0x17161514u, 0x1B1A1918u, 0x1F1E1D1Cu,
        0x23222120u, 0x27262524u};
    host_.LoadWords(0x38000u, data.data(), data.size());
    const uint32_t command = 0x80000004u | (28u << 25u) | (29u << 20u);
    Arm({command, 40u, 0x38000u, 3u << 30u, 0x39000u, 0u, 0u, 0u}, 1u << 9u);
    for (uint32_t i = 0u; i < 5u; ++i) {
        Request(28u, true);
        host_.Tick();
        Request(28u, false);
        host_.Tick();
    }
    Request(29u, true);
    host_.Tick();
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 0u);
    for (uint32_t i = 0u; i < data.size(); ++i) {
        TEST_CHECK(host_.ReadPhysical(0x39000u + i * 4u, 4u) == data[i]);
    }
    Request(28u, false);
    Request(29u, false);
    Arm({command, 8u, 0x38000u, 0u, 0x39040u, 3u << 30u, 0u, 0u}, (1u << 9u) | (1u << 7u));
    Request(28u, true);
    host_.Tick();
    Request(28u, false);
    Request(29u, true);
    host_.Tick();
    Request(29u, false);
    for (uint32_t i = 0u; i < 7u; ++i) {
        Request(28u, true);
        host_.Tick();
        Request(28u, false);
        host_.Tick();
    }
    TEST_CHECK(host_.Read(0x14002018u, 4u) == 0u);
    TEST_CHECK(host_.ReadPhysical(0x39040u, 4u) == data[0]);
    TEST_CHECK(host_.ReadPhysical(0x39044u, 4u) == data[1]);
}

void DmaTest::Throttle() {
    host_.WritePhysical(0x3A000u, 0x44332211u, 4u);
    const uint32_t command = 0xC0000004u | (30u << 25u) | (31u << 20u);
    const std::array<uint32_t, 8u> descriptor = {command, 4u, 0x3A000u, 0u, 0x3B000u, 0u, 0u, 0u};
    for (bool fine : {false, true}) {
        host_.WritePhysical(0x3B000u, 0u, 4u);
        host_.Write(0x14003008u, 0x80000003u, 4u);
        Arm(descriptor);
        TEST_CHECK(state_.guest_cycle_deadline - state_.guest_cycle_counter == 16u);
        for (uint32_t i = 0u; i < (fine ? 4u : 1u); ++i) {
            state_.guest_cycle_counter += fine ? 16u : 64u;
            host_.Tick();
        }
        TEST_CHECK(host_.ReadPhysical(0x3B000u, 4u) == 0x44332211u);
        TEST_CHECK(host_.Read(0x14002018u, 4u) == 0u);
    }
    host_.Write(0x14003008u, 0x80000003u, 4u);
    host_.WritePhysical(0x3B000u, 0u, 4u);
    Arm(descriptor);
    state_.guest_cycle_counter += 8u;
    host_.Tick();
    host_.Write(0x1090003Cu, 0u, 4u);
    state_.guest_cycle_counter += 3u;
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x3B000u, 1u) == 0u);
    ++state_.guest_cycle_counter;
    host_.Tick();
    TEST_CHECK(host_.ReadPhysical(0x3B000u, 1u) == 0x11u);
    host_.Write(0x14003008u, 0u, 4u);
}

void DmaTest::FetchArbitration() {
    for (uint32_t policy : {0u, 4u}) {
        au13xxprobe::ProbeHost host(0xFFFFu);
        cerfengine::MipsCpuState state{};
        host.Bind(state);
        host.Write(0x10100100u, 3u, 4u);
        host.Write(0x10100014u, 3u, 4u);
        host.Write(0x10100010u, 1u, 4u);
        host.Write(0x10100018u, 0x10u, 4u);
        host.Write(0x10100004u, 0x11u, 4u);
        host.Write(0x10100004u, 0x22u, 4u);
        state.guest_cycle_counter += 2560u;
        host.Tick();
        host.Write(0x14003000u, policy, 4u);
        const uint32_t command = 0x80000004u | (1u << 25u) | (31u << 20u);
        for (uint32_t index : {0u, 3u}) {
            const uint32_t descriptor_pa = 0x10000u + index * 64u;
            const std::array<uint32_t, 8u> descriptor = {command, 1u, 0x10100000u,
                2u << 28u, 0x40000u + index * 4u, 0u, 0u, 0u};
            host.LoadWords(descriptor_pa, descriptor.data(), descriptor.size());
            host.Write(0x14002004u + index * 256u, descriptor_pa, 4u);
            host.Write(0x14002000u + index * 256u, 1u, 4u);
        }
        const uint32_t literal = 0x80002004u | (31u << 20u) | (2u << 18u) | (2u << 16u);
        const std::array<uint32_t, 8u> trigger = {literal, 8u, 0u, 0u, 0x1400200Cu,
            (1u << 30u) | (4u << 14u) | 0x300u, 0u, 0u};
        host.LoadWords(0x10100u, trigger.data(), trigger.size());
        host.Write(0x14002204u, 0x10100u, 4u);
        host.Write(0x14002200u, 1u, 4u);
        host.Write(0x1400220Cu, 0u, 4u);
        std::printf("FETCH_POLICY AF=%u ch0=%02X ch3=%02X\n", policy,
                    host.ReadPhysical(0x40000u, 1u), host.ReadPhysical(0x4000Cu, 1u));
        TEST_CHECK(host.ReadPhysical(0x40000u, 1u) == (policy == 0u ? 0x22u : 0x11u));
        TEST_CHECK(host.ReadPhysical(0x4000Cu, 1u) == (policy == 0u ? 0x11u : 0x22u));
    }
}

void DmaTest::Run() {
    CopyAndInterrupt();
    Strides();
    LiteralAndBranch();
    Requests();
    GpioSelection();
    CompareWidths();
    BurstPressure();
    Throttle();
    FetchArbitration();
    std::puts("au13xx_dma_test passed");
}

}

void DmaTest::InvalidStride(bool two_dimensions) {
    const uint32_t command = 0xC0000004u | (31u << 25u) | (31u << 20u) |
                             (two_dimensions ? 0x400u | (2u << 18u) | (2u << 16u) : 0u);
    Arm({command, 4u, 0x20000u, (two_dimensions ? 1u : 3u) << 28u, 0x30000u, 0u, 0u, 0u});
}

void DmaTest::NarrowStride(uint32_t width) {
    const uint32_t command = 0x80000404u | (31u << 25u) | (31u << 20u) |
                             (width << 18u) | (width << 16u);
    Arm({command, 4u, 0x20000u, 0u, 0x30000u, 0u, 0u, 0u});
    std::printf("NARROW_2D completed remaining=%u valid=%08X\n",
                host_.Read(0x14002018u, 4u), host_.ReadPhysical(0x10000u, 4u));
}

int main(int argc, char** argv) {
    DmaTest test;
    if (argc == 2) {
        const std::string_view mode(argv[1]);
        if (mode == "--unsupported-gpio-dma") {
            test.InvalidGpioSelection();
            return 0;
        }
        if (mode == "--reserved-2d-byte" || mode == "--reserved-2d-halfword") {
            test.NarrowStride(mode == "--reserved-2d-byte" ? 0u : 1u);
            return 0;
        }
        TEST_CHECK(mode == "--reserved-2d-decrement" || mode == "--unsupported-burstable-fifo");
        test.InvalidStride(mode == "--reserved-2d-decrement");
        return 0;
    }
    TEST_CHECK(argc == 1);
    test.Run();
}
