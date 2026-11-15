#include "probe_system.h"

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
    cpu.ResetState();
    auto& state = *cpu.State();
    host.Bind(state);
    host.BindEngine(engine);
    auto& tlb = engine.Get<cerfengine::MipsMmu>().TlbContext();
    system.Bind(tlb);
    if (argc == 2 && std::string_view(argv[1]) == "--reverse-endian") {
        const auto status = system.MipsCp0(12u);
        status.write(status.write_context, 0x02000010u);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--deferred-watch") {
        const auto cause = system.MipsCp0(13u);
        cause.write(cause.write_context, 0x00400000u);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--instruction-watch") {
        const auto watch = system.MipsCp0(18u);
        watch.write(watch.write_context, 4u);
        return 0;
    }
    const bool high = argc == 2 && std::string_view(argv[1]) == "--high-physical";
    TEST_CHECK(argc == 1 || high);
    const uint64_t physical = high ? 0x100000000ull : 0x20000000ull;
    for (uint32_t mask : {0u, 0x1E000u, 0x1FFE000u}) {
        state.cp0_index = 0u;
        state.cp0_entryhi = 0x00400001u;
        state.cp0_pagemask = mask;
        state.cp0_entrylo0 = static_cast<uint32_t>((physical >> 12u) << 6u) | 6u;
        state.cp0_entrylo1 = 0u;
        const uint32_t original_lo = state.cp0_entrylo0;
        setup.mips_tlb.write_indexed(setup.mips_tlb.context, tlb);
        std::printf("TLB_PFN expected=%llX actual=%llX mask=%X\n",
                    static_cast<unsigned long long>(physical),
                    static_cast<unsigned long long>(state.tlb[0].pfn[0]), mask);
        TEST_CHECK(state.tlb[0].pfn[0] == physical);
        state.cp0_entrylo0 = 0u;
        setup.mips_tlb.read(setup.mips_tlb.context, tlb);
        TEST_CHECK(state.cp0_entrylo0 == original_lo);
        uint32_t pa = 0u;
        const uint32_t va = 0x00400234u;
        TEST_CHECK(setup.mips_tlb.map_address(setup.mips_tlb.context, &state, va,
                   cerfengine::MipsAccess::kRead, &pa) == cerfengine::MipsTlbResult::kMatch);
        const uint64_t expected = physical | (va & ((mask | 0x1FFFu) >> 1u));
        std::printf("TLB_TRANSLATE expected=%llX actual=%X\n",
                    static_cast<unsigned long long>(expected), pa);
        TEST_CHECK(pa == expected);
    }
    const auto cause = system.MipsCp0(13u);
    state.cp0_cause = 0x80008024u;
    cause.write(cause.write_context, 0x00800300u);
    std::printf("CAUSE_WRITE expected=80808324 actual=%08X\n", state.cp0_cause);
    TEST_CHECK(cause.read(cause.read_context) == 0x80808324u);
    for (uint32_t bev : {0u, 0x00400000u}) {
        const uint32_t base = bev == 0u ? 0x80000000u : 0xBFC00200u;
        for (uint32_t iv : {0u, 0x00800000u}) {
            cause.write(cause.write_context, iv);
            for (uint32_t exception : {0u, 8u}) {
                state.cp0_status = bev;
                state.pc = 0x80004000u;
                state.branch_state = cerfengine::MipsBranch::kNone;
                setup.mips_exception.enter(setup.mips_exception.context, &state, exception, false);
                const uint32_t expected = base + (exception == 0u && iv != 0u ? 0x200u : 0x180u);
                TEST_CHECK(state.pc == expected);
                TEST_CHECK(state.cp0_epc == 0x80004000u);
            }
            state.cp0_status = bev;
            setup.mips_exception.enter(setup.mips_exception.context, &state, 2u, true);
            TEST_CHECK(state.pc == base);
            state.cp0_status = bev | 2u;
            setup.mips_exception.enter(setup.mips_exception.context, &state, 2u, true);
            TEST_CHECK(state.pc == base + 0x180u);
        }
    }
    const auto status = system.MipsCp0(12u);
    status.write(status.write_context, 0x05800000u);
    std::printf("STATUS_RESERVED expected=00000000 actual=%08X\n", state.cp0_status);
    TEST_CHECK(status.read(status.read_context) == 0u);
    status.write(status.write_context, 0xFDFFFFFFu);
    TEST_CHECK(status.read(status.read_context) == 0x1858FF17u);
    const auto watchhi = system.MipsCp0(19u);
    watchhi.write(watchhi.write_context, 0xFFFFFFFFu);
    TEST_CHECK(watchhi.read(watchhi.read_context) == 0x40FF0FF8u);
    TEST_CHECK(!system.MipsCp0(17u).writable);
    TEST_CHECK(!system.MipsCp0(19u, 1u).present);
    std::puts("au13xx_system_test passed");
}
