#include "probe_system.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_mmu.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <unistd.h>

namespace {

void Address(au13xxprobe::ProbeNand& nand, uint32_t row, uint8_t column = 0u) {
    nand.Address(column);
    nand.Address(static_cast<uint8_t>(row));
    nand.Address(static_cast<uint8_t>(row >> 8u));
    nand.Address(static_cast<uint8_t>(row >> 16u));
}

void Program(au13xxprobe::ProbeNand& nand, uint8_t area, uint32_t row, uint8_t data) {
    nand.Command(area);
    nand.Command(0x80u);
    Address(nand, row);
    nand.Write(data);
    nand.Command(0x10u);
    nand.Command(0x70u);
    TEST_CHECK(nand.Read() == 0xC0u);
}

void Nand() {
    au13xxprobe::ProbeNand nand;
    nand.Command(0x90u);
    nand.Address(0u);
    for (uint8_t id : {0xECu, 0x76u, 0x5Au, 0x3Fu}) TEST_CHECK(nand.Read() == id);
    Program(nand, 0u, 32u, 0xAAu);
    Program(nand, 0x50u, 32u, 0x12u);
    Program(nand, 0u, 33u, 0xBBu);
    Program(nand, 0x50u, 33u, 0x34u);
    nand.Command(0u);
    Address(nand, 32u);
    TEST_CHECK(nand.Read() == 0xAAu);
    Address(nand, 33u);
    TEST_CHECK(nand.Read() == 0xBBu);
    nand.Command(0x50u);
    Address(nand, 32u);
    TEST_CHECK(nand.Read() == 0x12u);
    Address(nand, 33u);
    TEST_CHECK(nand.Read() == 0x34u);
    nand.Command(0x50u);
    Address(nand, 31u);
    TEST_CHECK(nand.Read() == 0xFFu);
    nand.Command(0x50u);
    Address(nand, 32u);
    TEST_CHECK(nand.Read() == 0x12u);
    nand.Command(0u);
    Address(nand, 32u);
    nand.Command(0x70u);
    TEST_CHECK(nand.Read() == 0xC0u);
    nand.Command(0u);
    TEST_CHECK(nand.Read() == 0xAAu);
    nand.Command(0x50u);
    Address(nand, 32u);
    nand.Command(0x70u);
    nand.Command(0x70u);
    TEST_CHECK(nand.Read() == 0xC0u);
    nand.Command(0x50u);
    TEST_CHECK(nand.Read() == 0x12u);
    nand.Command(0x70u);
    nand.Command(0u);
    Address(nand, 31u);
    TEST_CHECK(nand.Read() == 0xFFu);
    nand.Command(0x01u);
    nand.Command(0xFFu);
    nand.Command(0x80u);
    Address(nand, 64u);
    nand.Write(0x5Au);
    nand.Command(0x10u);
    nand.Command(0u);
    Address(nand, 64u);
    TEST_CHECK(nand.Read() == 0x5Au);
    const auto path = std::filesystem::temp_directory_path() /
                      ("cerf-au13xx-probe-" + std::to_string(getpid()) + ".bin");
    TEST_CHECK(!std::filesystem::exists(path));
    {
        std::ofstream file(path, std::ios::binary);
        const std::array<uint8_t, 1024u> bytes = [] {
            std::array<uint8_t, 1024u> result{};
            std::fill_n(result.begin(), 512u, 0x23u);
            std::fill_n(result.begin() + 512u, 512u, 0x45u);
            return result;
        }();
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        TEST_CHECK(file.good());
    }
    nand.Import(path.string(), 0x11A0u);
    TEST_CHECK(std::filesystem::remove(path));
    for (uint32_t i = 0u; i < 3u; ++i) {
        nand.Command(0u);
        Address(nand, 0x11A0u + i);
        TEST_CHECK(nand.Read() == (i == 0u ? 0x23u : (i == 1u ? 0x45u : 0xFFu)));
        nand.Command(0x50u);
        Address(nand, 0x11A0u + i);
        TEST_CHECK(nand.Read() == 0xFFu);
    }
    nand.Command(0x01u);
    Address(nand, 64u);
    TEST_CHECK(nand.Read() == 0xFFu);
    Program(nand, 0u, 0x10001u, 0x67u);
    nand.Command(0u);
    Address(nand, 1u);
    TEST_CHECK(nand.Read() == 0xFFu);
    nand.Command(0u);
    Address(nand, 0x10001u);
    TEST_CHECK(nand.Read() == 0x67u);
    nand.Command(0x60u);
    nand.Address(32u);
    nand.Address(0u);
    nand.Address(0u);
    nand.Command(0xD0u);
    nand.Command(0x50u);
    Address(nand, 32u);
    TEST_CHECK(nand.Read() == 0xFFu);
    nand.Command(0u);
    Address(nand, 32u);
    TEST_CHECK(nand.Read() == 0xFFu);
    nand.Command(0u);
    Address(nand, 64u);
    TEST_CHECK(nand.Read() == 0x5Au);
    {
        std::vector<uint8_t> raw(131072u * 528u, 0xFFu);
        raw[0u] = 0x23u;
        raw[512u] = 0x99u;
        raw[131071u * 528u] = 0x9Au;
        raw.back() = 0x76u;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(raw.data()), raw.size());
        TEST_CHECK(file.good());
    }
    nand.ImportRaw(path.string());
    TEST_CHECK(std::filesystem::remove(path));
    nand.Command(0u);
    Address(nand, 0u);
    TEST_CHECK(nand.Read() == 0x23u);
    nand.Command(0x50u);
    Address(nand, 0u);
    TEST_CHECK(nand.Read() == 0x99u);
    nand.Command(0u);
    Address(nand, 131071u);
    TEST_CHECK(nand.Read() == 0x9Au);
    nand.Command(0x50u);
    Address(nand, 131071u, 15u);
    TEST_CHECK(nand.Read() == 0x76u);
}

void Host() {
    au13xxprobe::ProbeHost host(0x1234u);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    host.Write(0x10201114u, 3u, 4u);
    TEST_CHECK((host.Read(0x10200008u, 4u) & (1u << 5u)) != 0u);
    host.Write(0x10200018u, 1u << 5u, 4u);
    TEST_CHECK((host.Read(0x10201114u, 4u) & 3u) == 2u);
    TEST_CHECK((host.Read(0x10200008u, 4u) & (1u << 5u)) == 0u);
    host.Write(0x10200008u, 1u << 5u, 4u);
    TEST_CHECK((host.Read(0x10201114u, 4u) & 3u) == 3u);
    TEST_CHECK((host.Read(0x10200008u, 4u) & (1u << 5u)) != 0u);
    host.Gpic().SetLevel(69u, false);
    TEST_CHECK((host.Read(0x10200008u, 4u) & (1u << 5u)) != 0u);
    host.Write(0x10200098u, 1u << 5u, 4u);
    TEST_CHECK((host.Read(0x10201114u, 4u) & 3u) == 0u);
    TEST_CHECK((host.Read(0x10200008u, 4u) & (1u << 5u)) == 0u);
    TEST_CHECK(host.Read(0x1FE00000u, 1u) == 0x34u);
    TEST_CHECK(host.Read(0x1FE00001u, 1u) == 0x12u);
    TEST_CHECK(host.Read(0x1FE00002u, 1u) == 0x34u);
    TEST_CHECK(host.Read(0x1FE00000u, 4u) == 0x12341234u);
    TEST_CHECK(host.Read(0x1FE00001u, 4u) == 0x34123412u);
    host.Write(0x10102100u, 3u, 4u);
    host.Write(0x10102014u, 3u, 4u);
    host.Write(0x10102028u, 1u, 4u);
    host.Write(0x10102010u, 1u, 4u);
    host.Write(0x10102004u, 'A', 4u);
    host.Write(0x10102004u, '\n', 4u);
    state.guest_cycle_counter += 100000u;
    host.Tick();
    TEST_CHECK(host.Read(0x1010201Cu, 4u) == 0x60u);
    for (uint32_t i = 0u; i < 16u; ++i) host.Write(0x10102004u, 'B', 4u);
    TEST_CHECK(host.Read(0x1010201Cu, 4u) == 0u);
    state.guest_cycle_counter += 100000u;
    host.Tick();
    TEST_CHECK(host.Read(0x1010201Cu, 4u) == 0x60u);
    state.guest_cycle_counter = 0xFFFFFFF0u;
    TEST_CHECK(host.Cycles() == 0xFFFFFFF0u);
    state.guest_cycle_counter = 0x20u;
    TEST_CHECK(host.Cycles() == 0x100000020ull);
    host.Write(0x10900044u, 31u, 4u);
    host.Write(0x10900014u, 0x2920u, 4u);
    host.Write(0x10900048u, 77u, 4u);
    TEST_CHECK(host.Read(0x10900058u, 4u) == 77u);
    TEST_CHECK((host.Read(0x10900014u, 4u) & 0x10FF009Fu) == 0u);
    state.guest_cycle_counter += 773438u;
    TEST_CHECK(host.Read(0x10900058u, 4u) == 78u);
    host.Write(0x10900014u, 0x2820u, 4u);
    state.guest_cycle_counter += 773438u;
    TEST_CHECK(host.Read(0x10900058u, 4u) == 78u);
}

void RtcGpic() {
    au13xxprobe::ProbeHost host(0xFFFFu);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    host.Write(0x10201150u, 0x6Du, 4u);
    host.Write(0x10200048u, 1u << 20u, 4u);
    host.Write(0x10900044u, 31u, 4u);
    host.Write(0x10900014u, 0x2120u, 4u);
    state.guest_cycle_counter = 773437u;
    host.Tick();
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0u);
    ++state.guest_cycle_counter;
    host.Tick();
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x2000u);
    TEST_CHECK(host.Read(0x10200030u, 4u) == 84u);
    host.Write(0x10200028u, 1u << 20u, 4u);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0u);
    host.Write(0x10200058u, 1u << 20u, 4u);
    state.guest_cycle_counter += 773438u;
    host.Tick();
    TEST_CHECK(host.Read(0x10200028u, 4u) == (1u << 20u));
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0u);
    host.Write(0x10200048u, 1u << 20u, 4u);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x2000u);
    host.Write(0x10201154u, 0x61u, 4u);
    host.Write(0x10200048u, 1u << 21u, 4u);
    host.Write(0x1090004Cu, host.Read(0x10900058u, 4u) + 1u, 4u);
    state.guest_cycle_counter += 773438u;
    host.Tick();
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x2400u);
    TEST_CHECK(host.Read(0x10200030u, 4u) == 85u);
    host.Write(0x10200028u, 1u << 21u, 4u);
    TEST_CHECK(host.Read(0x10200030u, 4u) == 84u);
    host.Write(0x10200028u, 1u << 20u, 4u);
    TEST_CHECK(host.Read(0x10200030u, 4u) == 0x7Fu);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0u);
}

void GpicLow() {
    au13xxprobe::ProbeHost host(0xFFFFu);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    host.Write(0x10201014u, 0x10u, 4u);
    host.Write(0x10200040u, 1u << 5u, 4u);
    TEST_CHECK((host.Read(0x10200000u, 4u) & (1u << 5u)) == 0u);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x400u);
    host.Gpic().SetLevel(5u, false);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x400u);
    host.Write(0x10200020u, 1u << 5u, 4u);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x400u);
    host.Gpic().SetLevel(5u, true);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0u);
    host.Gpic().SetLevel(5u, false);
    TEST_CHECK((state.cp0_cause & 0x3C00u) == 0x400u);
}

void CounterPhase() {
    au13xxprobe::ProbeHost host(0xFFFFu);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    host.Write(0x10900000u, 31u, 4u);
    host.Write(0x10900044u, 31u, 4u);
    host.Write(0x10900014u, 0x2920u, 4u);
    state.guest_cycle_counter = 400000u;
    host.Write(0x10900000u, 63u, 4u);
    state.guest_cycle_counter = 800000u;
    TEST_CHECK(host.Read(0x10900058u, 4u) == 1u);
    for (uint32_t i = 0u; i < 20u; ++i) {
        state.guest_cycle_counter += 40000u;
        host.Write(0x10900000u, i, 4u);
        host.Write(0x10900014u, 0x2920u, 4u);
    }
    TEST_CHECK(host.Read(0x10900058u, 4u) == 2u);
}

void ReverseCounterPhase() {
    au13xxprobe::ProbeHost host(0xFFFFu);
    cerfengine::MipsCpuState state{};
    host.Bind(state);
    host.Write(0x10900000u, 31u, 4u);
    host.Write(0x10900044u, 31u, 4u);
    host.Write(0x10900014u, 0x2920u, 4u);
    state.guest_cycle_counter = 400000u;
    host.Write(0x10900044u, 63u, 4u);
    state.guest_cycle_counter = 800000u;
    TEST_CHECK(host.Read(0x10900040u, 4u) == 1u);
    const uint32_t rtc = host.Read(0x10900058u, 4u);
    host.Write(0x10900014u, 0x920u, 4u);
    state.guest_cycle_counter += 800000u;
    TEST_CHECK(host.Read(0x10900058u, 4u) == rtc);
    host.Write(0x10900014u, 0x2920u, 4u);
    state.guest_cycle_counter += 800000u;
    TEST_CHECK(host.Read(0x10900058u, 4u) == rtc + 1u);
}

void Asid() {
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
    auto& tlb = engine.Get<cerfengine::MipsMmu>().TlbContext();
    system.Bind(tlb);
    const auto wired = system.MipsCp0(6u);
    const auto random = system.MipsCp0(1u);
    state.guest_cycle_counter += 13u;
    wired.write(wired.write_context, 2u);
    TEST_CHECK(random.read(random.read_context) == 31u);
    ++state.guest_cycle_counter;
    TEST_CHECK(random.read(random.read_context) == 30u);
    guest.SetHostChainExit(true);
    state.cp0_status = 0u;
    for (uint32_t index = 0u; index < 2u; ++index) {
        state.cp0_index = index;
        state.cp0_entryhi = 0x00400001u + index;
        state.cp0_entrylo0 = ((0x10000u + 0x2000u * index) >> 12u) << 6u | 6u;
        state.cp0_entrylo1 = 0u;
        setup.mips_tlb.write_indexed(setup.mips_tlb.context, tlb);
        const std::array<uint32_t, 4u> program = {0x2402000Bu + 11u * index, 0x08000800u, 0u, 0u};
        host.LoadWords(0x10000u + 0x2000u * index, program.data(), program.size());
    }
    const auto entryhi = system.MipsCp0(10u);
    for (uint32_t asid : {1u, 2u, 1u}) {
        entryhi.write(entryhi.write_context, 0x00400000u | asid);
        state.pc = 0x00400000u;
        state.gpr[2] = 0u;
        state.branch_state = cerfengine::MipsBranch::kNone;
        for (uint32_t n = 0u; n < 16u && state.pc != 0x2000u; ++n) {
            TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        }
        TEST_CHECK(state.pc == 0x2000u);
        TEST_CHECK(state.gpr[2] == (asid == 1u ? 11u : 22u));
    }
    const auto count = system.MipsCp0(9u);
    const auto compare = system.MipsCp0(11u);
    count.write(count.write_context, 0xFFFFFFF0u);
    compare.write(compare.write_context, 0x10u);
    state.guest_cycle_counter += 31u;
    host.Tick();
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) == 0u);
    ++state.guest_cycle_counter;
    host.Tick();
    std::printf("COMPARE_REPRO count=%08X compare=%08X cause=%08X\n",
                count.read(count.read_context), state.cp0_compare, state.cp0_cause);
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) != 0u);
    count.write(count.write_context, 7u);
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) != 0u);
    compare.write(compare.write_context, 0x20u);
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) == 0u);
    count.write(count.write_context, 0x1Fu);
    ++state.guest_cycle_counter;
    host.Tick();
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) != 0u);
    compare.write(compare.write_context, 0x20u);
    state.guest_cycle_counter += 0xFFFFFFFFu;
    host.Tick();
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) == 0u);
    ++state.guest_cycle_counter;
    host.Tick();
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) != 0u);
    const std::array<uint32_t, 3u> loop = {0x24420001u, 0x08001000u, 0u};
    const std::array<uint32_t, 3u> handler = {0x24140021u, 0x40805800u, 0x42000018u};
    host.LoadWords(0x4000u, loop.data(), loop.size());
    host.LoadWords(0x180u, handler.data(), handler.size());
    count.write(count.write_context, 0u);
    compare.write(compare.write_context, 100u);
    state.cp0_status = 0x8001u;
    state.pc = 0x80004000u;
    state.gpr[2] = 0u;
    state.gpr[20] = 0u;
    for (uint32_t n = 0u; n < 256u && (state.gpr[20] != 33u || (state.cp0_status & 2u) != 0u); ++n) {
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        host.Tick();
    }
    TEST_CHECK(state.gpr[20] == 33u);
    TEST_CHECK(state.gpr[2] != 0u);
    TEST_CHECK((state.cp0_status & 2u) == 0u);
    TEST_CHECK((state.cp0_cause & cerfengine::kMipsCauseTimerIp) == 0u);
}

}

int main(int argc, char** argv) {
    if (argc == 2) {
        au13xxprobe::ProbeNand nand;
        nand.Command(0u);
        Address(nand, 31u);
        if (std::string_view(argv[1]) == "--sequential-cross-block") {
            for (uint32_t i = 0u; i < 529u; ++i) nand.Read();
        } else if (std::string_view(argv[1]) == "--random-cross-block") {
            nand.Read();
            Address(nand, 32u);
        } else {
            TestFail("unknown regression mode");
        }
        return 0;
    }
    Nand();
    Host();
    RtcGpic();
    GpicLow();
    CounterPhase();
    ReverseCounterPhase();
    Asid();
    std::puts("au13xx_probe_test passed");
}
