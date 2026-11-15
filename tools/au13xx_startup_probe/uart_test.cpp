#include "probe_host.h"
#include <string_view>

namespace {

class UartTest {
public:
    UartTest();
    void Run();
    void UnsupportedBidir(uint32_t value);

private:
    void Setup(uint32_t port);
    void Step(uint32_t cycles);
    void Interrupts();
    void ReceiveAndTimeout();
    void BreakAcknowledgement();
    void ModemAndStandby();
    void DmaLoopback();
    au13xxprobe::ProbeHost host_;
    cerfengine::MipsCpuState state_{};
};

UartTest::UartTest() : host_(0xFFFFu) {
    host_.Bind(state_);
}

void UartTest::Setup(uint32_t port) {
    const uint32_t base = 0x10100000u + port * 0x1000u;
    host_.Write(base + 0x100u, 0u, 4u);
    host_.Write(base + 0x100u, 1u, 4u);
    host_.Write(base + 0x100u, 3u, 4u);
    host_.Write(base + 0x14u, 3u, 4u);
    host_.Write(base + 0x10u, 1u, 4u);
    host_.Write(base + 0x18u, 0x10u, 4u);
}

void UartTest::Step(uint32_t cycles) {
    state_.guest_cycle_counter += cycles;
    host_.Tick();
}

void UartTest::Interrupts() {
    host_.Write(0x10100014u, 3u, 4u);
    TEST_CHECK(host_.Read(0x10100014u, 4u) == 0u);
    Setup(0u);
    host_.Write(0x10201000u + 4u * 88u, 0x21u, 4u);
    host_.Write(0x10200048u, 1u << (88u % 32u), 4u);
    host_.Write(0x10100008u, 2u, 4u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 2u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) != 0u);
    host_.Write(0x10100004u, 0x41u, 4u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x60u) == 0x20u);
    host_.Write(0x10100004u, 0x42u, 4u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 1u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) == 0u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 2u);
    host_.Write(0x10100008u, 0u, 4u);
    TEST_CHECK((state_.cp0_cause & (1u << 10u)) == 0u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x41u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x42u);
    TEST_CHECK(host_.Read(0x10101014u, 4u) == 0u);
    TEST_CHECK(host_.Read(0x10102014u, 4u) == 0u);
    TEST_CHECK(host_.Read(0x10103014u, 4u) == 0u);
}

void UartTest::ReceiveAndTimeout() {
    Setup(0u);
    host_.Write(0x10100010u, 0x41u, 4u);
    host_.Write(0x10100008u, 1u, 4u);
    host_.Write(0x10100004u, 0x5Au, 4u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 1u);
    Step(0x300u * 8u - 1u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 1u);
    Step(1u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 12u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x5Au);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 1u);
    Setup(0u);
    host_.Write(0x10100008u, 5u, 4u);
    for (uint32_t i = 0u; i < 17u; ++i) host_.Write(0x10100004u, i, 4u);
    Step(17u * 16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 6u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 2u) != 0u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 2u) == 0u);
    for (uint32_t i = 0u; i < 16u; ++i) TEST_CHECK(host_.Read(0x10100000u, 4u) == i);
    Setup(0u);
    host_.Write(0x10100008u, 4u, 4u);
    host_.Write(0x10100014u, 0x43u, 4u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 6u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x99u) == 0x99u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0u);
}

void UartTest::ModemAndStandby() {
    Setup(1u);
    host_.Write(0x10101018u, 0x1Fu, 4u);
    TEST_CHECK(host_.Read(0x10101020u, 4u) == 0xFBu);
    TEST_CHECK(host_.Read(0x10101020u, 4u) == 0xF0u);
    host_.Write(0x10101018u, 0x10u, 4u);
    TEST_CHECK(host_.Read(0x10101020u, 4u) == 0x0Fu);
    host_.Write(0x10101024u, 1u, 4u);
    host_.Write(0x10101004u, 0x66u, 4u);
    Step(10000u);
    TEST_CHECK((host_.Read(0x1010101Cu, 4u) & 1u) == 0u);
    host_.Uart(1u).SetModemInputs(0x10u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x10101000u, 4u) == 0x66u);
    host_.Write(0x10101104u, 1u, 4u);
    host_.Write(0x10101004u, 0x55u, 4u);
    Step(10000u);
    TEST_CHECK((host_.Read(0x1010101Cu, 4u) & 1u) == 0u);
    host_.Write(0x10101104u, 0u, 4u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x10101000u, 4u) == 0x55u);
    Setup(3u);
    host_.Write(0x10103004u, 0x77u, 4u);
    Step(100u);
    host_.Write(0x10103100u, 2u, 4u);
    Step(5000u);
    host_.Write(0x10103100u, 3u, 4u);
    Step(16u * 8u * 10u - 101u);
    TEST_CHECK((host_.Read(0x1010301Cu, 4u) & 1u) == 0u);
    Step(1u);
    TEST_CHECK(host_.Read(0x10103000u, 4u) == 0x77u);
}

void UartTest::BreakAcknowledgement() {
    for (bool drain_first : {false, true}) {
        Setup(0u);
        host_.Write(0x10100008u, 4u, 4u);
        host_.Write(0x10100014u, 0x43u, 4u);
        Step(16u * 8u * 10u);
        if (drain_first) TEST_CHECK(host_.Read(0x10100000u, 4u) == 0u);
        host_.Write(0x10100014u, 3u, 4u);
        host_.Write(0x10100004u, 0x5Au, 4u);
        Step(16u * 8u * 10u);
        if (!drain_first) TEST_CHECK(host_.Read(0x10100000u, 4u) == 0u);
        const uint32_t before_ack = host_.Read(0x10100000u, 4u);
        std::printf("BREAK_RX before_ack=%02X\n", before_ack);
        TEST_CHECK(before_ack == 0u);
        TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 6u);
        TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x10u) != 0u);
        TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x10u) == 0u);
        host_.Write(0x10100004u, 0x5Au, 4u);
        Step(16u * 8u * 10u);
        TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x5Au);
    }
    Setup(0u);
    host_.Write(0x10100008u, 4u, 4u);
    host_.Write(0x10100004u, 0x33u, 4u);
    Step(16u * 8u * 10u);
    host_.Write(0x10100014u, 0x43u, 4u);
    Step(16u * 8u * 10u);
    host_.Write(0x10100014u, 3u, 4u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x10u) == 0u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x33u);
    host_.Write(0x10100004u, 0x5Au, 4u);
    Step(16u * 8u * 10u);
    TEST_CHECK((host_.Read(0x1010001Cu, 4u) & 0x10u) != 0u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0u);
    TEST_CHECK(host_.Read(0x1010000Cu, 4u) == 1u);
}

void UartTest::DmaLoopback() {
    Setup(0u);
    host_.Write(0x10100010u, 9u, 4u);
    const uint32_t source = 0x44332211u;
    host_.LoadWords(0x20000u, &source, 1u);
    const std::array<uint32_t, 8u> receive = {
        0x80000004u | (1u << 25u) | (31u << 20u), 4u, 0x10100000u, 2u << 28u,
        0x30000u, 0u, 0u, 0u};
    const std::array<uint32_t, 8u> transmit = {
        0x80000004u | (31u << 25u), 4u, 0x20000u, 0u, 0x10100004u, 2u << 28u, 0u, 0u};
    host_.LoadWords(0x10000u, receive.data(), receive.size());
    host_.LoadWords(0x10040u, transmit.data(), transmit.size());
    host_.Write(0x14002004u, 0x10000u, 4u);
    host_.Write(0x14002000u, 1u, 4u);
    host_.Write(0x1400200Cu, 0u, 4u);
    host_.Write(0x14002104u, 0x10040u, 4u);
    host_.Write(0x14002100u, 1u, 4u);
    host_.Write(0x1400210Cu, 0u, 4u);
    for (uint32_t i = 0u; i < 4u; ++i) Step(16u * 8u * 10u);
    TEST_CHECK(host_.ReadPhysical(0x30000u, 4u) == source);
    TEST_CHECK((host_.ReadPhysical(0x10000u, 4u) & 0x80000000u) == 0u);
    TEST_CHECK((host_.ReadPhysical(0x10040u, 4u) & 0x80000000u) == 0u);
}

void UartTest::Run() {
    Interrupts();
    ReceiveAndTimeout();
    BreakAcknowledgement();
    ModemAndStandby();
    DmaLoopback();
    Setup(0u);
    host_.Write(0x10100108u, 4u, 4u);
    TEST_CHECK(host_.Read(0x10100108u, 4u) == 4u);
    host_.Write(0x10100004u, 0x55u, 4u);
    Step(16u * 8u * 10u);
    TEST_CHECK(host_.Read(0x10100000u, 4u) == 0x55u);
    std::puts("au13xx_uart_test passed");
}

void UartTest::UnsupportedBidir(uint32_t value) {
    Setup(0u);
    host_.Write(0x10100108u, value, 4u);
}

}

int main(int argc, char** argv) {
    UartTest test;
    if (argc == 2) {
        const std::string_view mode(argv[1]);
        TEST_CHECK(mode == "--bidir-gate" || mode == "--open-drain");
        test.UnsupportedBidir(mode == "--bidir-gate" ? 5u : 2u);
        return 0;
    }
    TEST_CHECK(argc == 1);
    test.Run();
}
