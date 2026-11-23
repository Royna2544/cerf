#include "au1374_core_test_fixture.h"

#include <array>
#include <cstdio>
#include <vector>

namespace {

using au1374_core_test::Check;

class SensorTest : au1374_core_test::CoreTestFixture {
public:
    explicit SensorTest(cerfcore::CerfEmulator& emulator) : CoreTestFixture(emulator) {}

    void Run() { I2cSensors(); }

private:
    void I2cSensors() {
        const auto pin = [&](uint32_t gpio, uint32_t mode) {
            Write(0x10201000u + gpio * 4u, mode);
        };
        const auto level = [&](uint32_t gpio) {
            return (Read(0x10200000u + (gpio / 32u) * 4u) & (1u << (gpio % 32u))) != 0u;
        };
        const auto start = [&](uint32_t sda, uint32_t scl) {
            pin(scl, 3u);
            pin(sda, 0u);
            pin(sda, 2u);
        };
        const auto stop = [&](uint32_t sda, uint32_t scl) {
            pin(sda, 2u);
            pin(scl, 3u);
            pin(sda, 0u);
        };
        const auto send = [&](uint32_t sda, uint32_t scl, uint8_t byte) {
            for (int bit = 7; bit >= 0; --bit) {
                pin(scl, 2u);
                pin(sda, (byte & (1u << bit)) != 0u ? 0u : 2u);
                pin(scl, 3u);
            }
            pin(scl, 2u);
            pin(sda, 0u);
            pin(scl, 3u);
            const bool ack = !level(sda);
            pin(scl, 2u);
            return ack;
        };
        const auto recv = [&](uint32_t sda, uint32_t scl, bool acknowledge = false) {
            uint8_t value = 0u;
            for (uint32_t bit = 0u; bit < 8u; ++bit) {
                pin(scl, 3u);
                value = static_cast<uint8_t>((value << 1u) | (level(sda) ? 1u : 0u));
                pin(scl, 2u);
            }
            pin(sda, acknowledge ? 2u : 0u);
            pin(scl, 3u);
            pin(scl, 2u);
            pin(sda, 0u);
            return value;
        };
        constexpr uint32_t sda = 70u;
        constexpr uint32_t scl = 63u;
        const auto command = [&](uint8_t address, uint8_t value) {
            start(sda, scl);
            Check(send(sda, scl, address << 1u), "YAS526B command address NACK");
            Check(send(sda, scl, value), "YAS526B command NACK");
            stop(sda, scl);
        };
        const auto read = [&](uint8_t address, uint32_t count) {
            std::vector<uint8_t> bytes;
            start(sda, scl);
            Check(send(sda, scl, (address << 1u) | 1u), "YAS526B data address NACK");
            for (uint32_t i = 0u; i < count; ++i) bytes.push_back(recv(sda, scl, i + 1u < count));
            stop(sda, scl);
            return bytes;
        };
        for (uint8_t addr : {0x2Eu, 0x2Fu}) command(addr, 0xC0u);
        for (uint8_t addr : {0x2Eu, 0x2Fu}) {
            command(addr, 0xA0u);
            (void)read(addr, 4u);
            const auto calibration = read(addr, 4u);
            if (addr == 0x2Fu) Check((calibration[3] & 1u) == 0u, "YAS526B legacy chip selection");
        }
        constexpr std::array<uint8_t, 16u> xy_commands{
            0x88u, 0x81u, 0x89u, 0x82u, 0x8Au, 0x83u, 0x8Bu, 0x84u,
            0x8Cu, 0x85u, 0x8Du, 0x86u, 0x8Eu, 0x87u, 0x8Fu, 0x80u};
        constexpr std::array<uint8_t, 8u> z_commands{
            0x88u, 0x81u, 0x89u, 0x82u, 0x8Au, 0x83u, 0x8Bu, 0x80u};
        for (uint8_t byte : xy_commands) command(0x2Eu, byte);
        for (uint8_t byte : z_commands) command(0x2Fu, byte);
        command(0x2Eu, 0x01u);
        command(0x2Fu, 0x01u);
        const auto xy_rough = read(0x2Eu, 4u);
        const auto z_rough = read(0x2Fu, 2u);
        Check(xy_rough[0] == 1u && xy_rough[2] == 1u && z_rough[0] == 1u,
              "YAS526B rough-offset measurement bounds");
        for (uint8_t byte : {0x4Bu, 0x6Bu}) command(0x2Eu, byte);
        command(0x2Fu, 0x4Bu);
        command(0x2Eu, 0x00u);
        command(0x2Fu, 0x00u);
        const auto xy_sample = read(0x2Eu, 4u);
        const auto z_sample = read(0x2Fu, 2u);
        Check(xy_sample[0] == 0x28u && xy_sample[2] == 0x2Fu &&
              z_sample[0] == 0x1Fu && (xy_sample[0] & 0x80u) == 0u,
              "YAS526B legacy measurement and status");
        std::puts("PASS Core YAS526B guest calibration, configuration and measurement sequence");

        start(sda, scl);
        Check(send(sda, scl, 0x38u << 1u), "SMB380 address write NACK");
        Check(send(sda, scl, 0u), "SMB380 register pointer NACK");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, (0x38u << 1u) | 1u), "SMB380 address read NACK");
        Check(recv(sda, scl) == 0x02u, "SMB380 chip ID over its GPIO bus");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, 0x2Eu << 1u), "YAS526B XY address write NACK");
        Check(send(sda, scl, 0u), "YAS526B XY measurement NACK");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, (0x2Eu << 1u) | 1u), "YAS526B XY address read NACK");
        Check(recv(sda, scl) == 0x28u, "YAS526B north-facing measurement over sensor bus");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, 0x2Eu << 1u), "YAS526B XY control address NACK");
        Check(send(sda, scl, 0xC0u), "YAS526B XY control command NACK");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, 0x2Eu << 1u), "YAS526B XY configuration address NACK");
        Check(send(sda, scl, 0xA0u), "YAS526B XY configuration command NACK");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, 0x2Fu << 1u), "YAS526B Z measurement address NACK");
        Check(send(sda, scl, 0u), "YAS526B Z measurement command NACK");
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, (0x2Fu << 1u) | 1u), "YAS526B Z address read NACK");
        for (uint32_t i = 0u; i < 4u; ++i) (void)recv(sda, scl, i != 3u);
        stop(sda, scl);
        start(sda, scl);
        Check(send(sda, scl, 0x2Eu << 1u), "YAS526B XY normal-mode address NACK");
        Check(send(sda, scl, 0x80u), "YAS526B XY normal-mode command NACK");
        stop(sda, scl);
        std::puts("PASS Core SMB380 and YAS526B respond on the sensor GPIO I2C bus");

        constexpr uint32_t light_sda = 56u;
        constexpr uint32_t light_scl = 73u;
        start(light_sda, light_scl);
        Check(send(light_sda, light_scl, 0x23u << 1u), "BH1750 power-on address NACK");
        Check(send(light_sda, light_scl, 0x01u), "BH1750 power-on command NACK");
        stop(light_sda, light_scl);
        start(light_sda, light_scl);
        Check(send(light_sda, light_scl, 0x23u << 1u), "BH1750 mode address NACK");
        Check(send(light_sda, light_scl, 0x10u), "BH1750 continuous H-resolution command NACK");
        stop(light_sda, light_scl);
        start(light_sda, light_scl);
        Check(send(light_sda, light_scl, (0x23u << 1u) | 1u), "BH1750 early read NACK");
        Check(recv(light_sda, light_scl, true) == 0u, "BH1750 sample ready too early (high)");
        Check(recv(light_sda, light_scl) == 0u, "BH1750 sample ready too early (low)");
        stop(light_sda, light_scl);
        Tick(static_cast<uint32_t>(clock_.NsToCycles(200000000)));
        start(light_sda, light_scl);
        Check(send(light_sda, light_scl, (0x23u << 1u) | 1u), "BH1750 address read NACK");
        Check(recv(light_sda, light_scl, true) == 0u, "BH1750 100 lx sample high byte");
        Check(recv(light_sda, light_scl) == 120u, "BH1750 100 lx sample low byte");
        stop(light_sda, light_scl);
        std::puts("PASS Core BH1750 light sample through GPIO I2C bus");

        constexpr uint32_t video_sda = 60u;
        constexpr uint32_t video_scl = 74u;
        start(video_sda, video_scl);
        Check(send(video_sda, video_scl, 0x44u << 1u), "TW9900 address write NACK");
        Check(send(video_sda, video_scl, 1u), "TW9900 ID pointer NACK");
        stop(video_sda, video_scl);
        start(video_sda, video_scl);
        Check(send(video_sda, video_scl, (0x44u << 1u) | 1u), "TW9900 address read NACK");
        Check(recv(video_sda, video_scl) == 0x83u, "TW9900 revision on video GPIO bus");
        stop(video_sda, video_scl);
        start(video_sda, video_scl);
        Check(send(video_sda, video_scl, 0x2Au << 1u), "PIC command address NACK");
        Check(send(video_sda, video_scl, 1u), "PIC command byte NACK");
        stop(video_sda, video_scl);
        std::puts("PASS Core TW9900 and PIC respond on video GPIO I2C bus");
    }
};

}

int main(int argc, char** argv) {
    return au1374_core_test::RunCoreTest(argc, argv, "sensor",
                                         [](cerfcore::CerfEmulator& emu) { SensorTest(emu).Run(); });
}
