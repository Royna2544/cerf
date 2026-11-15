#include "probe_system.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_mmu.h>
#include <string_view>

int main(int argc, char** argv) {
    au13xxprobe::ProbeHost host(0xFFFFu);
    au13xxprobe::ProbeSystem system(host, 0x80000083u, 0x3E63318Au);
    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    setup.mips.isa_level = cerfengine::MipsIsaLevel::kMips32R1;
    setup.mips.phys_addr_mask = 0xFFFFFFFFu;
    host.Attach(setup);
    system.Attach(setup);
    cerfengine::Engine engine(setup);
    std::string error;
    TEST_CHECK(engine.Start(error));
    engine.BindClock(host);
    auto& cpu = engine.Get<cerfengine::MipsCpu>();
    auto& guest = engine.Get<cerfengine::GuestEngine>();
    cpu.ResetState();
    auto& state = *cpu.State();
    host.Bind(state);
    host.BindEngine(engine);
    system.Bind(engine.Get<cerfengine::MipsMmu>().TlbContext());
    guest.SetHostChainExit(true);
    state.cp0_status = 0u;
    if (argc == 2 && std::string_view(argv[1]) == "--armed-watch") {
        const auto watch = system.MipsCp0(18u);
        watch.write(watch.write_context, 0x80020002u);
        return 0;
    }
    TEST_CHECK(argc == 1);
    const std::array<uint32_t, 3u> code = {0x2402000Bu, 0x08000C00u, 0u};
    host.LoadWords(0x2000u, code.data(), code.size());
    const auto execute = [&](uint32_t segment) {
        state.pc = segment | 0x2000u;
        state.branch_state = cerfengine::MipsBranch::kNone;
        state.gpr[2] = 0u;
        for (uint32_t i = 0u; i < 8u && state.pc != (segment | 0x3000u); ++i) {
            TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
            host.Tick();
        }
        TEST_CHECK(state.pc == (segment | 0x3000u));
        return static_cast<uint32_t>(state.gpr[2]);
    };
    for (uint32_t segment : {0x80000000u, 0xA0000000u}) TEST_CHECK(execute(segment) == 11u);
    const uint32_t command = 0x80002004u | (31u << 20u) | (2u << 18u) | (2u << 16u);
    const std::array<uint32_t, 8u> descriptor = {
        command, 4u, 0x24020016u, 0u, 0x2000u, 0u, 0u, 0u};
    host.LoadWords(0x10000u, descriptor.data(), descriptor.size());
    host.Write(0x14002004u, 0x10000u, 4u);
    host.Write(0x14002000u, 1u, 4u);
    host.Write(0x1400200Cu, 0u, 4u);
    TEST_CHECK(host.ReadPhysical(0x2000u, 4u) == 0x24020016u);
    for (uint32_t segment : {0x80000000u, 0xA0000000u}) TEST_CHECK(execute(segment) == 22u);
    std::puts("DMA_CODE cached=22 uncached=22");
}
