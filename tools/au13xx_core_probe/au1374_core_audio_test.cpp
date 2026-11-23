#include "au1374_core_test_fixture.h"

#include <cerfcore/state/state_stream.h>

namespace {

using au1374_core_test::Check;

class AudioTest : au1374_core_test::CoreTestFixture {
public:
    explicit AudioTest(cerfcore::CerfEmulator& emu) : CoreTestFixture(emu) {}
    void Run() {
        Write(0u, 0x24u);
        Write(0x24u, 2u);
        Write(0x24u, 0u);
        Write(4u, 3u);
        Write(8u, 0xF4E01806u);
        Write(0xCu, 0xFFFFFFFFu);
        for (uint32_t i = 0u; i < 16u; ++i) bus_.WriteHalf(0x10A0101Cu, static_cast<uint16_t>(i * 100u));
        Check((Read(0x14u) & 0x700u) == 0x400u, "AC97 halfword FIFO fill/full/request state");
        Write(0x1Cu, 0x1234u);
        Check((Read(0x18u) & 0x200u) != 0u, "AC97 FIFO overflow latch");
        Write(0x18u, 0x200u);
        Write(0x10u, 1u);
        Check((Read(0x14u) & 0x10u) != 0u, "AC97 transmit busy after start");
        Tick(16499u);
        Check((Read(0x14u) & 0x400u) != 0u, "AC97 FIFO consumed before first48kHz frame");
        Tick(1u);
        Check((Read(0x14u) & 0x600u) == 0u, "AC97 first stereo frame consumption");
        Tick(3u * 16500u);
        Check((Read(0x14u) & 0x100u) != 0u, "AC97 DMA demand at eight empty entries");
        std::puts("PASS Core AC97 halfword FIFO, overflow, stereo frame timing and DMA threshold");
        Write(0x10u, 2u);
        Tick(3u * 16500u);
        Check((Read(0x14u) & 0x10u) != 0u, "AC97 transmit stop discarded queued samples");
        Tick(16500u);
        Check((Read(0x14u) & 0x210u) == 0x200u && (Read(0x18u) & 0x10u) != 0u,
              "AC97 transmit stop completion at FIFO drain");
        Write(0x18u, 0x10u);
        Check((Read(0x18u) & 0x10u) == 0u, "AC97 transmit-done W1C");
        std::puts("PASS Core AC97 graceful transmit stop and done interrupt acknowledgement");
        Snapshot();
    }

private:
    void Snapshot() {
        const auto parts = bus_.RegisteredPeripherals();
        Write(0x10u, 4u);
        for (uint32_t i = 0u; i < 16u; ++i) Write(0x1Cu, i * 300u);
        Write(0x10u, 1u);
        Tick(8250u);
        constexpr const char* path = "au1374-core-audio-test.state";
        {
            cerfcore::StateWriter writer(emu_, path);
            for (auto* part : parts) {
                writer.BeginFrame(part->MmioBase());
                part->SaveState(writer);
                writer.EndFrame();
            }
            Check(writer.Commit(), "AC97 snapshot commit");
        }
        const auto saved_cycles = state_.guest_cycle_counter;
        const auto saved_folded = state_.guest_cycle_folded;
        Tick(33000u);
        bus_.WriteWord(0x10900060u, 0x3Fu);
        Tick(1000000u);
        state_.guest_cycle_counter = saved_cycles;
        state_.guest_cycle_folded = saved_folded;
        {
            cerfcore::StateReader reader(emu_, path);
            for (auto* part : parts) {
                Check(reader.EnterFrame() == part->MmioBase(), "AC97 snapshot frame identity");
                part->RestoreState(reader);
                reader.LeaveFrame();
            }
        }
        clock_.OnCyclesRestored();
        for (auto* part : parts) part->PostRestore();
        Tick(8249u);
        Check((Read(0x14u) & 0x400u) != 0u, "restored AC97 consumed before saved frame phase");
        Tick(1u);
        Check((Read(0x14u) & 0x600u) == 0u, "restored AC97 first stereo frame");
        Tick(3u * 16500u);
        Check((Read(0x14u) & 0x100u) != 0u, "restored AC97 DMA threshold");
        Write(0x10u, 2u);
        Tick(4u * 16500u);
        Check((Read(0x14u) & 0x10u) == 0u, "restored AC97 stop completion");
        std::puts("PASS Core active AC97 FIFO and frame phase restore across CPU-rate changes");
    }
    void Write(uint32_t offset, uint32_t value) { bus_.WriteWord(0x10A01000u + offset, value); }
    uint32_t Read(uint32_t offset) { return bus_.ReadWord(0x10A01000u + offset); }
};

}

int main(int argc, char** argv) {
    return au1374_core_test::RunCoreTest(argc, argv, "audio",
                                         [](cerfcore::CerfEmulator& emu) { AudioTest(emu).Run(); });
}
