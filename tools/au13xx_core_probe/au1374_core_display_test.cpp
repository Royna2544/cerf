#include "au1374_core_test_fixture.h"

#include <array>

namespace {

using au1374_core_test::Check;

class DisplayTest : au1374_core_test::CoreTestFixture {
public:
    explicit DisplayTest(cerfcore::CerfEmulator& emu) : CoreTestFixture(emu) {}
    void Run() {
        Sd();
        Write(4u, 0u);
        Tick(20000000u);
        Write(0x48u, 0xFFFFFFFFu);
        Write(0xCu, 0u);
        Write(0x10u, 0u);
        Write(0x100u, 0x3FEu);
        Write(0x104u, (6u << 25u) | (1u << 22u) | (1u << 11u));
        Write(0x108u, 4u << 8u);
        Write(0x10Cu, 0x100000u);
        Write(0x114u, 0u);
        Write(0x24u, 1u);
        Put(0x100000u, 0x001FF800u);
        Write(4u, 0x80080000u);
        auto pixels = Render();
        Check(pixels[0u] == 0xFFFF0000u && pixels[1u] == 0xFF0000FFu,
              "LCD RGB565 little endian pixel order");
        Tick(263u);
        Check((Read(0x48u) & 3u) == 0u, "LCD raster edge fired early");
        Tick(1u);
        Check((Read(0x48u) & 3u) == 2u, "LCD start-active edge");
        Write(0x48u, 2u);
        Tick(264u);
        Check((Read(0x48u) & 3u) == 1u, "LCD vertical-sync edge");
        std::puts("PASS Core LCD RGB565 and guest-clock raster edges");
        Indexed();
        AlphaCursor();
        Write(4u, 0u);
        Tick(528u);
        Check((Read(0x48u) & 4u) != 0u, "LCD shutdown completion");
        std::puts("PASS Core LCD shutdown completes at frame boundary");
    }

private:
    void Write(uint32_t offset, uint32_t value) { bus_.WriteWord(0x15000000u + offset, value); }
    uint32_t Read(uint32_t offset) { return bus_.ReadWord(0x15000000u + offset); }
    std::array<uint32_t, 2u> Render() {
        std::array<uint32_t, 2u> pixels{};
        emu_.Get<cerfcore::PanelFrameRenderer>().RenderInto(pixels.data(), 2u, 1u);
        return pixels;
    }
    void Sd() {
        constexpr uint32_t base = 0x10601000u;
        bus_.WriteWord(base + 0xCu, 1u);
        bus_.WriteWord(base + 0xCu, 3u);
        bus_.WriteWord(base + 0x10u, 0x401u);
        bus_.WriteWord(base + 8u, 0x203u);
        bus_.WriteWord(base + 8u, 0xFFu);
        Check((bus_.ReadWord(base + 8u) & 0x1FFu) == 3u, "SD divider changed without write enable");
        bus_.WriteWord(base + 8u, 0x28000u);
        bus_.WriteWord(base + 0x20u, 0x40501u);
        Check((bus_.ReadWord(base + 0x18u) & 0x28000u) == 0x28000u,
              "SD absent card response timeout and master interrupt");
        bus_.WriteWord(base + 0x18u, 0x20000u);
        Check((bus_.ReadWord(base + 0x18u) & 0x28000u) == 0u, "SD timeout W1C");
        std::puts("PASS Core SD divider enable, absent-card timeout and interrupt acknowledgement");
    }
    void Indexed() {
        constexpr uint32_t words[4u][4u]{
            {0x40000000u, 0x40u, 0x02000000u, 0x02u},
            {0x10000000u, 0x10u, 0x04000000u, 0x04u},
            {0x01000000u, 0x01u, 0x10000000u, 0x10u},
            {0x00010000u, 0x100u, 0x00010000u, 0x100u}
        };
        Write(0x400u, 0xFF000000u);
        Write(0x404u, 0xFFFFFFFFu);
        Write(0x108u, (1u << 21u) | (4u << 8u));
        for (uint32_t format = 0u; format < 4u; ++format) {
            for (uint32_t order = 0u; order < (format == 3u ? 2u : 4u); ++order) {
                Write(0x104u, (format << 25u) | (order << 22u) | (1u << 11u));
                Put(0x100000u, words[format][order]);
                const auto pixels = Render();
                Check(pixels[0u] == 0xFF000000u && pixels[1u] == 0xFFFFFFFFu,
                      "LCD indexed palette or pixel ordering");
            }
        }
        std::puts("PASS Core LCD 1/2/4/8 bpp palettes and documented pixel orders");
    }
    void AlphaCursor() {
        Write(0x104u, (6u << 25u) | (1u << 22u) | (1u << 11u));
        Write(0x108u, 4u << 8u);
        Put(0x100000u, 0x001F001Fu);
        Write(0x120u, 0u);
        Write(0x124u, (3u << 30u) | (1u << 29u) | (13u << 25u) | (1u << 11u));
        Write(0x128u, 8u << 8u);
        Write(0x12Cu, 0x100100u);
        Put(0x100100u, 0x80FF0000u);
        Put(0x100104u, 0x80FF0000u);
        Write(0x24u, 3u);
        Check(Render()[0u] == 0xFF80007Fu, "LCD opposite-pipe alpha blend");
        Write(0x38u, 0u);
        Write(0x3Cu, 0xFF00FF00u);
        Write(0x800u, 1u);
        Write(0x34u, 0u);
        Write(0x30u, 1u);
        const auto pixels = Render();
        Check(pixels[0u] == 0xFF00FF00u && pixels[1u] == 0xFF80007Fu,
              "LCD cursor color, alpha and pattern order");
        std::puts("PASS Core LCD pipe priority, alpha blend and hardware cursor");
    }
};

}

int main(int argc, char** argv) {
    return au1374_core_test::RunCoreTest(argc, argv, "display",
                                         [](cerfcore::CerfEmulator& emu) { DisplayTest(emu).Run(); });
}
