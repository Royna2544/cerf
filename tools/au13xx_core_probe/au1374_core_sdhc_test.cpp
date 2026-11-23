#include "au1374_core_test_fixture.h"

#include "../../libCerfCore/src/peripherals/mmc/mmc_card.h"
#include <cerfcore/core/device_config.h>
#include <cerfcore/state/state_stream.h>

#include <array>

namespace {
using au1374_core_test::Check;

void Test(cerfcore::CerfEmulator& emu) {
    using namespace cerfcore;
    for (const auto& entry : emu.Get<DeviceConfig>().bundled_sd_cards) {
        Check(!entry.writable, "SDHC test writes and erases the card; it refuses a writable sd_cards entry");
    }
    auto& card = emu.Get<MmcCard>();
    uint32_t response[4]{};
    const auto cmd = [&](uint8_t index, uint32_t argument) { return card.Command(index, argument, response); };
    const auto app = [&](uint8_t index, uint32_t argument) {
        cmd(55u, index == 41u ? 0u : 0x10000u);
        return cmd(index, argument);
    };
    cmd(0u, 0u);
    app(41u, 0x40FF8000u);
    Check(response[0u] == 0x00FF8000u, "SDHC requires interface-condition negotiation");
    Check(cmd(8u, 0x2AAu) == MmcCommandResult::NoResponse, "SDHC unsupported voltage");
    Check(cmd(8u, 0x1AAu) == MmcCommandResult::Short && response[0u] == 0x1AAu, "SDHC R7 echo");
    app(41u, 0x00FF8000u);
    Check(response[0u] == 0x00FF8000u, "SDHC requires HCS");
    app(41u, 0x40FF8000u);
    Check(response[0u] == 0xC0FF8000u, "SDHC ready and CCS");
    cmd(2u, 0u);
    cmd(3u, 0u);
    cmd(9u, 0x10000u);
    Check((response[0u] >> 30u) == 1u, "SDHC CSD structure2");
    const uint32_t capacity = ((response[1u] & 63u) << 16u) | (response[2u] >> 16u);
    Check((static_cast<uint64_t>(capacity) + 1u) * 512u * 1024u == (8ull << 30u), "SDHC8GiB CSD capacity");
    cmd(7u, 0x10000u);
    app(51u, 0u);
    Check(card.ReadData()[0u] == 2u && card.ReadData()[1u] == 5u, "SDHC SD2.0 SCR");
    card.EndDataPhase();
    cmd(6u, 0x00FFFFF1u);
    Check(card.ReadData()[13u] == 1u && (card.ReadData()[16u] & 15u) == 15u, "SDHC default speed only");
    card.EndDataPhase();
    std::puts("PASS SDHC voltage, HCS/CCS negotiation, CSD2, SCR and default-speed switch status");

    constexpr uint32_t sector = 0x00FFFFFEu;
    cmd(17u, sector);
    const auto original = card.ReadData();
    card.EndDataPhase();
    std::array<uint8_t, 512u> pattern{};
    pattern.fill(0xA7u);
    cmd(24u, sector);
    card.ReceiveBlock(pattern.data(), pattern.size());
    {
        StateWriter writer(emu, "au1374-core-sdhc-test.state");
        card.SaveState(writer);
        Check(writer.Commit(), "SDHC overlay snapshot");
    }
    cmd(32u, 0u);
    cmd(33u, 0x00FFFFFFu);
    cmd(38u, 0u);
    cmd(17u, sector);
    Check(card.ReadData()[0u] == 0u, "SDHC compact whole-card erase");
    card.EndDataPhase();
    {
        StateReader reader(emu, "au1374-core-sdhc-test.state");
        card.RestoreState(reader);
    }
    cmd(17u, sector);
    Check(card.ReadData()[0u] == 0xA7u, "SDHC high-address overlay restore");
    card.EndDataPhase();
    cmd(17u, 0x01000000u);
    Check((response[0u] & 0x80000000u) != 0u, "SDHC end-of-card bounds");
    Check(original.size() == 512u, "SDHC high-address read");
    std::puts("PASS SDHC >4GiB addressing, compact8GiB erase and overlay snapshot restoration");
}
}

int main(int argc, char** argv) {
    return au1374_core_test::RunCoreTest(argc, argv, "sdhc", Test);
}
