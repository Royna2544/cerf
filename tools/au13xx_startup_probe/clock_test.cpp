#include "probe_host.h"
#include <string_view>

int main(int argc, char** argv) {
    au13xxprobe::ProbeHost host(0xFFFFu);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    if (argc == 2) {
        TEST_CHECK(std::string_view(argv[1]) == "--external-counter");
        host.Gpic().SetLevel(1u, false);
        host.Write(0x10900044u, 31u, 4u);
        host.Write(0x10900014u, 0x6120u, 4u);
        return 0;
    }
    TEST_CHECK(argc == 1);
    host.Write(0x10900044u, 31u, 4u);
    host.Write(0x10900014u, 0x2120u, 4u);
    host.Write(0x10900060u, 33u, 4u);
    state.guest_cycle_counter = 386718u;
    host.Tick();
    TEST_CHECK(host.Read(0x10900058u, 4u) == 0u);
    ++state.guest_cycle_counter;
    host.Tick();
    TEST_CHECK(host.Read(0x10900058u, 4u) == 1u);
    host.Write(0x10900060u, 66u, 4u);
    state.guest_cycle_counter += 773437u;
    host.Tick();
    TEST_CHECK(host.Read(0x10900058u, 4u) == 2u);
    std::puts("RTC_CLOCK rate changes preserve oscillator phase");
    const auto setup_uart = [&] {
        host.Write(0x10100100u, 0u, 4u);
        host.Write(0x10100100u, 3u, 4u);
        host.Write(0x10100014u, 3u, 4u);
        host.Write(0x10100010u, 0x41u, 4u);
        host.Write(0x10100018u, 0x10u, 4u);
    };
    const auto step = [&](uint32_t cycles) {
        state.guest_cycle_counter += cycles;
        host.Tick();
    };
    setup_uart();
    host.Write(0x10100004u, 0x5Au, 4u);
    step(100u);
    host.Write(0x1090003Cu, 0u, 4u);
    step(589u);
    TEST_CHECK((host.Read(0x1010001Cu, 4u) & 1u) == 0u);
    step(1u);
    TEST_CHECK(host.Read(0x10100000u, 4u) == 0x5Au);
    host.Write(0x1090003Cu, 2u, 4u);
    setup_uart();
    host.Write(0x10100008u, 1u, 4u);
    host.Write(0x10100004u, 0x5Au, 4u);
    step(1280u);
    step(3072u);
    host.Write(0x10100028u, 2u, 4u);
    step(6143u);
    TEST_CHECK(host.Read(0x1010000Cu, 4u) == 1u);
    step(1u);
    TEST_CHECK(host.Read(0x1010000Cu, 4u) == 12u);
    std::puts("UART_CLOCK shifter and timeout retain elapsed phase");
}
