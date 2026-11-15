#define main Au1374OriginalBootHostMain
#include "au1374_core_host.cpp"
#undef main

#include <cerfcore/state/state_stream.h>
#include <cerfcore/platform/host_file.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>

#include <array>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class SdTest {
public:
    explicit SdTest(cerfcore::CerfEmulator& emu) : emu_(emu), bus_(emu.Get<cerfcore::PeripheralDispatcher>()) {}
    void Run() {
        Write(0xCu, 1u);
        Write(0xCu, 3u);
        Write(0x10u, 0x401u);
        Write(8u, 0xFFFF8000u);
        Command(0u, 0u, 0u, 0u);
        Check((Command(55u, 0u, 1u, 0u) & 0x20u) != 0u, "SD APP_CMD status");
        Check(Command(41u, 0xFF8000u, 3u, 0u) == 0x80FF8000u, "SD operating conditions");
        Command(2u, 0u, 2u, 0u);
        Check((Read(0x28u) & 0xFFFFFFu) == 0xFF4345u, "SD CID and136-bit response alignment");
        Check((Command(3u, 0u, 6u, 0u) >> 16u) == 1u, "SD RCA assignment");
        Command(9u, 0x10000u, 2u, 0u);
        std::array<uint32_t, 4u> raw{Read(0x28u), Read(0x2Cu), Read(0x30u), Read(0x34u)};
        std::array<uint32_t, 4u> csd{};
        for (uint32_t i = 0u; i < 4u; ++i) {
            csd[i] = raw[i] << 8u;
            if (i + 1u < 4u) csd[i] |= raw[i + 1u] >> 24u;
        }
        const auto bits = [&csd](uint32_t low, uint32_t count) {
            uint32_t value = 0u;
            for (uint32_t i = 0u; i < count; ++i) value |= ((csd[3u - (low + i) / 32u] >> ((low + i) % 32u)) & 1u) << i;
            return value;
        };
        const uint64_t size = static_cast<uint64_t>(bits(62u, 12u) + 1u) <<
                              (bits(47u, 3u) + 2u + bits(80u, 4u));
        Check(size == 64u * 1024u * 1024u, "SD CSD decoded capacity");
        Command(7u, 0x10000u, 0x81u, 0u);
        Command(55u, 0x10000u, 1u, 0u);
        Write(0x14u, 7u);
        Command(51u, 0u, 1u, 2u);
        Check(Read(4u) == 0x500u && Read(4u) == 0u, "SD SCR packed32-bit FIFO words");
        Check((Read(0x18u) & 0x400u) == 0u && (Read(0x18u) & 0x40000u) != 0u,
              "SD SCR FIFO drain and data completion");
        std::puts("PASS Core SD identification, RCA, CSD capacity and packed SCR transfer");
        ReadSector();
        SaveRestore();
    }

private:
    uint32_t Read(uint32_t offset) { return bus_.ReadWord(0x10601000u + offset); }
    void Write(uint32_t offset, uint32_t value) { bus_.WriteWord(0x10601000u + offset, value); }
    uint32_t Command(uint32_t index, uint32_t argument, uint32_t response, uint32_t type) {
        Write(0x18u, 0xFFFFFFFFu);
        Write(0x24u, argument);
        Write(0x20u, (response << 16u) | (index << 8u) | (type << 4u) | 1u);
        Check((Read(0x18u) & 0x20000u) == 0u, "SD unexpected response timeout");
        return Read(0x34u);
    }
    void ReadSector() {
        std::array<uint8_t, 512u> expected{};
        cerfcore::host_platform::HostFile file;
        const auto& config = emu_.Get<cerfcore::DeviceConfig>();
        Check(config.bundled_sd_cards.size() == 1u, "SD fixture configuration");
        Check(file.Open(cerfcore::ResolveDeviceFile(config.device_name, config.bundled_sd_cards[0u].file),
                        cerfcore::host_platform::FileMode::kReadExisting), "SD fixture open");
        uint32_t done = 0u;
        Check(file.Read(expected.data(), expected.size(), done) && done == expected.size(), "SD fixture read");
        Write(0x14u, 511u);
        Command(17u, 0u, 1u, 2u);
        for (uint32_t i = 0u; i < 128u; ++i) {
            Check(Read(4u) == cerfcore::cerf::le::U32(expected.data(), i * 4u), "SD MBR byte stream");
        }
        Check((Read(0x18u) & 0x420u) == 0u, "SD FIFO and busy remain after final word");
        std::puts("PASS Core SD512-byte MBR read matches immutable FAT16 input");
    }
    void SaveRestore() {
        constexpr uint32_t address = 0x03FFFE00u;
        Command(17u, address, 1u, 2u);
        const auto first = Read(4u);
        cerfcore::Peripheral* sd = nullptr;
        for (auto* part : bus_.RegisteredPeripherals()) if (part->MmioBase() == 0x10601000u) sd = part;
        Check(sd != nullptr, "SD peripheral missing");
        constexpr const char* path = "au1374-core-sd-test.state";
        {
            cerfcore::StateWriter writer(emu_, path);
            sd->SaveState(writer);
            Check(writer.Commit(), "SD snapshot write");
        }
        const auto second = Read(4u);
        Command(12u, 0u, 0x81u, 7u);
        Command(24u, address, 1u, 1u);
        for (uint32_t i = 0u; i < 128u; ++i) Write(0u, 0x73590000u | i);
        Command(17u, address, 1u, 2u);
        Check(Read(4u) == 0x73590000u, "SD volatile write/read");
        {
            cerfcore::StateReader reader(emu_, path);
            sd->RestoreState(reader);
        }
        sd->PostRestore();
        Check(Read(4u) == second, "SD mid-block cursor restore");
        Command(12u, 0u, 0x81u, 7u);
        Command(17u, address, 1u, 2u);
        Check(Read(4u) == first, "SD media cells restore");
        std::puts("PASS Core SD volatile write, mid-block snapshot and media restoration");
    }
    cerfcore::CerfEmulator& emu_;
    cerfcore::PeripheralDispatcher& bus_;
};

}

int main(int argc, char** argv) {
    using namespace cerfcore;
    Log::SetFile("au1374-core-sd-test.log");
    Log::SetFlush(true);
    Log::SetEnabled(Log::MASK_ALL);
    CerfConfig config;
    config.device_override = "au1374";
    CerfEmulator emulator(config, argc, argv);
    emulator.CreateAllServices();
    const bool passed = emulator.Get<Fatal>().Guard([&] {
        emulator.ResolveAllSlots();
        emulator.ReadyAllServices();
        SdTest(emulator).Run();
    });
    Check(passed, "Core SD tests halted");
    return 0;
}
