#pragma once

#include "probe_nand.h"
#include "probe_dma.h"
#include "probe_gpic.h"
#include "probe_sys.h"
#include "probe_trace.h"
#include "probe_uart.h"
#include "test_host.h"
#include <cerfengine/mips/mips_bus_master_coherence.h>

#include <map>

namespace au13xxprobe {

class ProbeHost : public TestHost {
public:
    explicit ProbeHost(uint16_t external);
    void Bind(cerfengine::MipsCpuState& state);
    void BindEngine(cerfengine::Engine& engine);
    void Attach(cerfengine::Setup& setup);
    uint64_t Cycles() override;
    void Tick();
    void SetCpuDeadline(uint64_t cycle);
    void SetSysDeadline(uint64_t cycle);
    void SetDmaDeadline(uint64_t cycle);
    void SetUartDeadline(uint32_t index, uint64_t cycle);
    void SetGpicLines(uint32_t lines);
    void Summary();
    uint32_t Read(uint32_t pa, uint32_t width);
    void Write(uint32_t pa, uint32_t value, uint32_t width);
    uint32_t ReadPhysical(uint32_t pa, uint32_t width);
    void WritePhysical(uint32_t pa, uint32_t value, uint32_t width);
    uint32_t Pc() const;
    ProbeTrace& Trace();
    ProbeSys& Sys();
    ProbeNand& Nand();
    ProbeGpic& Gpic();
    ProbeDma& Dma();
    ProbeUart& Uart(uint32_t index);
    static bool Route(void* context, uint32_t pa, cerfengine::MmioRoute& route);

private:
    static uint32_t CERFENGINE_CDECL RouteRead(void* context, uint32_t offset, uint32_t width);
    static void CERFENGINE_CDECL RouteWrite(void* context, uint32_t offset, uint32_t value,
                                           uint32_t width);
    void PublishDeadline();
    cerfengine::MipsCpuState* state_ = nullptr;
    cerfengine::MipsBusMasterCoherence* coherence_ = nullptr;
    uint64_t cycles_ = 0u;
    uint32_t folded_ = 0u;
    uint64_t cpu_deadline_ = UINT64_MAX;
    uint64_t sys_deadline_ = UINT64_MAX;
    uint64_t dma_deadline_ = UINT64_MAX;
    std::array<uint64_t, 4u> uart_deadlines_ = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    uint16_t external_;
    uint32_t nand_control_ = 0u;
    ProbeGpic gpic_;
    ProbeTrace trace_;
    ProbeSys sys_;
    ProbeDma dma_;
    std::array<ProbeUart, 4u> uarts_;
    ProbeNand nand_;
};

}
