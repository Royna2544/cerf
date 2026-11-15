#include "probe_system.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_mmu.h>

#include <charconv>
#include <fstream>
#include <string_view>

namespace {

uint64_t Number(const char* text, uint64_t maximum) {
    std::string_view input(text);
    int base = 10;
    if (input.starts_with("0x") || input.starts_with("0X")) {
        input.remove_prefix(2u);
        base = 16;
    }
    uint64_t value = 0u;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value, base);
    if (input.empty() || result.ec != std::errc{} || result.ptr != input.data() + input.size() ||
        value > maximum) TestFail("invalid numeric argument: %s", text);
    return value;
}

}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0u);
    if (argc < 2 || argc > 8) {
        std::printf("usage: au13xx_startup_probe Part00.bin [external16 [Config0 [Config1 "
                    "[guest_cycle_budget [linear_nand_main_or_raw:path [start_page]]]]]]\n");
        return 1;
    }
    const uint16_t external = argc > 2 ? static_cast<uint16_t>(Number(argv[2], 0xFFFFu)) : 0xFFFFu;
    const uint32_t config0 = argc > 3 ? static_cast<uint32_t>(Number(argv[3], UINT32_MAX)) : 0x80000083u;
    const uint32_t config1 = argc > 4 ? static_cast<uint32_t>(Number(argv[4], UINT32_MAX)) : 0x3E63318Au;
    const uint64_t budget = argc > 5 ? Number(argv[5], 20000000000ull) : 1500000000ull;
    const uint32_t start_page = argc > 7 ? static_cast<uint32_t>(Number(argv[7], 131071u)) : 0x11A0u;
    if (budget == 0u) TestFail("guest-cycle budget must be positive");
    au13xxprobe::ProbeHost host(external);
    au13xxprobe::ProbeSystem system(host, config0, config1);
    constexpr size_t image_size = 0x003722E4u;
    std::vector<uint8_t> bytes(image_size);
    std::ifstream image(argv[1], std::ios::binary);
    image.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!image) TestFail("could not read the complete primary XIP from %s", argv[1]);
    std::vector<uint32_t> words(image_size / 4u);
    for (size_t i = 0u; i < words.size(); ++i) words[i] = cerfengine::le::U32(bytes.data() + 4u * i);
    host.LoadWords(0x0FC7E000u, words.data(), words.size());
    if (argc > 6) {
        const std::string_view nand_path(argv[6]);
        if (nand_path.starts_with("raw:")) host.Nand().ImportRaw(std::string(nand_path.substr(4u)));
        else host.Nand().Import(argv[6], start_page);
    }
    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    setup.mips.isa_level = cerfengine::MipsIsaLevel::kMips32R1;
    setup.mips.llsc = true;
    setup.mips.cop1_cop2_unusable = true;
    setup.mips.phys_addr_mask = 0xFFFFFFFFu;
    host.Attach(setup);
    system.Attach(setup);
    cerfengine::Engine engine(setup);
    engine.SetLogMask(1u << static_cast<uint32_t>(cerfengine::LogChannel::kCaution));
    std::string error;
    if (!engine.Start(error)) TestFail("Engine start: %s", error.c_str());
    engine.BindClock(host);
    auto& cpu = engine.Get<cerfengine::MipsCpu>();
    auto& guest = engine.Get<cerfengine::GuestEngine>();
    cpu.ResetState();
    host.Bind(*cpu.State());
    host.BindEngine(engine);
    system.Bind(engine.Get<cerfengine::MipsMmu>().TlbContext());
    guest.SetHostChainExit(true);
    std::printf("START pc=%08X Config0=%08X Config1=%08X budget_cycles=%llu external=%04X\n",
                cpu.State()->pc, config0, config1, static_cast<unsigned long long>(budget), external);
    bool halted = false;
    while (host.Cycles() < budget) {
        if (guest.Run() == cerfengine::GuestEngine::RunStatus::kHalted) {
            halted = true;
            break;
        }
        host.Tick();
    }
    std::printf("%s cycles=%llu pc=%08X\n", halted ? "ENGINE_HALT" : "BUDGET",
                static_cast<unsigned long long>(host.Cycles()), cpu.State()->pc);
    const auto count = system.MipsCp0(9u);
    std::printf("TIMER_STATE count=%08X compare=%08X cause=%08X status=%08X\n",
                count.read(count.read_context), cpu.State()->cp0_compare,
                cpu.State()->cp0_cause, cpu.State()->cp0_status);
    guest.PrintFatalDump();
    host.Summary();
    return halted ? 1 : 0;
}
