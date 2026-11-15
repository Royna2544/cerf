#include "probe_host.h"

namespace au13xxprobe {

uint32_t ProbeHost::ReadPhysical(uint32_t pa, uint32_t width) {
    if (width == 0u || width > 4u) TestFail("DMA bus read width=%u", width);
    if (pa >= 0x10000000u) return Read(pa, width);
    if (static_cast<uint64_t>(pa) + width > 0x10000000u) TestFail("DMA read crosses RAM boundary");
    cerfengine::PhysicalPage page;
    const uint8_t* bytes = TestHost::PhysicalTranslate(this, pa, false, page);
    return static_cast<uint32_t>(cerfengine::le::UN(bytes, width));
}

void ProbeHost::WritePhysical(uint32_t pa, uint32_t value, uint32_t width) {
    if (width == 0u || width > 4u) TestFail("DMA bus write width=%u", width);
    if (pa >= 0x10000000u) {
        Write(pa, value, width);
        return;
    }
    if (static_cast<uint64_t>(pa) + width > 0x10000000u) TestFail("DMA write crosses RAM boundary");
    cerfengine::PhysicalPage page;
    uint8_t* bytes = TestHost::PhysicalTranslate(this, pa, true, page);
    if (!bytes) TestFail("DMA write to read-only memory");
    cerfengine::le::PutN(bytes, value, width);
    if (coherence_) coherence_->AfterWrite(pa, width);
}

}
