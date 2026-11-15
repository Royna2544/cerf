#pragma once

#include "probe_host.h"

#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mips/mips_tlb_context.h>

#include <array>

namespace au13xxprobe {

class ProbeSystem : public cerfengine::MipsSystemHost {
public:
    ProbeSystem(ProbeHost& host, uint32_t config0, uint32_t config1);
    ~ProbeSystem() override;
    void Attach(cerfengine::Setup& setup);
    void Bind(cerfengine::MipsTlbContext& tlb);
    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override;
    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd, uint32_t sel) override;
    void MipsReset(cerfengine::MipsCpuState* state) override;

private:
    struct Register {
        ProbeSystem* system;
        uint32_t rd;
    };
    uint32_t* Storage(uint32_t rd);
    uint32_t Read(uint32_t rd);
    void Write(uint32_t rd, uint32_t value);
    uint32_t RandomIndex();
    void ArmCompare(uint64_t now);
    void ClockTick();
    void FillTlb(cerfengine::MipsTlbContext& tlb, uint32_t index);
    static uint32_t CERFENGINE_FASTCALL ReadRegister(void* context);
    static void CERFENGINE_FASTCALL WriteRegister(void* context, uint32_t value);
    static uint32_t CERFENGINE_FASTCALL ReadConfig1(void* context);
    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void* context, cerfengine::MipsCpuState* state, uint32_t va,
        cerfengine::MipsAccess access, uint32_t* pa);
    static bool CERFENGINE_FASTCALL PageGlobal(void* context, cerfengine::MipsCpuState* state,
                                               uint32_t va);
    static void CERFENGINE_FASTCALL WriteIndexed(void* context, cerfengine::MipsTlbContext& tlb);
    static void CERFENGINE_FASTCALL WriteRandom(void* context, cerfengine::MipsTlbContext& tlb);
    static void CERFENGINE_FASTCALL Probe(void* context, cerfengine::MipsTlbContext& tlb);
    static void CERFENGINE_FASTCALL ReadTlb(void* context, cerfengine::MipsTlbContext& tlb);
    static void CERFENGINE_FASTCALL EnterException(void* context, cerfengine::MipsCpuState* state,
                                                   uint32_t cause, bool refill);
    static void CERFENGINE_FASTCALL FaultRegisters(void* context, cerfengine::MipsCpuState* state,
                                                   uint32_t va);
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void* context,
                                                       const cerfengine::MipsCpuState& state);
    static void CERFENGINE_FASTCALL Eret(void* context, cerfengine::MipsCpuState* state);
    static void CERFENGINE_FASTCALL Rfe(void* context, cerfengine::MipsCpuState* state);
    ProbeHost& host_;
    cerfengine::MipsCpuState* state_ = nullptr;
    cerfengine::MipsTlbContext* tlb_ = nullptr;
    std::array<Register, 32u> registers_;
    uint32_t config0_;
    uint32_t config1_;
    uint32_t count_base_ = 0u;
    uint64_t count_anchor_ = 0u;
    uint64_t random_anchor_ = 0u;
    uint64_t next_compare_ = UINT64_MAX;
};

}
