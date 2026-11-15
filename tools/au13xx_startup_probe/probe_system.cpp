#include "probe_system.h"

#include <bit>

namespace au13xxprobe {

ProbeSystem::ProbeSystem(ProbeHost& host, uint32_t config0, uint32_t config1)
    : host_(host), config0_(config0), config1_(config1) {
    for (uint32_t rd = 0u; rd < registers_.size(); ++rd) registers_[rd] = Register{this, rd};
}

ProbeSystem::~ProbeSystem() {
    host_.OnClockDispatch({});
}

void ProbeSystem::Attach(cerfengine::Setup& setup) {
    setup.mips_system = this;
    setup.mips_tlb = {this, &MapAddress, &PageGlobal, &WriteIndexed, &WriteRandom, &Probe, &ReadTlb};
    setup.mips_exception = {this, &EnterException, &FaultRegisters, &InterruptsEnabled, &Eret, &Rfe};
}

void ProbeSystem::Bind(cerfengine::MipsTlbContext& tlb) {
    tlb_ = &tlb;
    host_.OnClockDispatch([this] { ClockTick(); });
    ArmCompare(host_.Cycles());
}

void ProbeSystem::MipsReset(cerfengine::MipsCpuState* state) {
    state_ = state;
    state->min_page_shift = 12u;
    state->nb_tlb = ((config1_ >> 25u) & 63u) + 1u;
    state->tlb_in_use = state->nb_tlb;
    state->cp0_random = state->nb_tlb - 1u;
    state->cp0_prid = 0x800C8001u;
    state->cp0_config = config0_;
    state->cp0_status = 0x00400004u;
    state->phys_addr_mask = 0xFFFFFFFFu;
    state->pc = 0x8FC7E004u;
    count_base_ = 0u;
    count_anchor_ = host_.Cycles();
    random_anchor_ = count_anchor_;
    ArmCompare(count_anchor_);
}

void ProbeSystem::ArmCompare(uint64_t now) {
    const uint32_t count = count_base_ + static_cast<uint32_t>(now - count_anchor_);
    const uint32_t distance = state_->cp0_compare - count;
    next_compare_ = now + (distance != 0u ? distance : 0x100000000ull);
    host_.SetCpuDeadline(next_compare_);
}

void ProbeSystem::ClockTick() {
    const uint64_t now = host_.Cycles();
    if (now < next_compare_) return;
    state_->cp0_cause |= cerfengine::kMipsCauseTimerIp;
    if (host_.Trace().Hit("COMPARE_IRQ", 0u)) {
        std::printf("COMPARE_IRQ due=%llu observed=%llu compare=%08X pc=%08X\n",
                    static_cast<unsigned long long>(next_compare_),
                    static_cast<unsigned long long>(now), state_->cp0_compare, state_->pc);
    }
    ArmCompare(now);
}

uint32_t* ProbeSystem::Storage(uint32_t rd) {
    switch (rd) {
    case 0u: return &state_->cp0_index;
    case 1u: return &state_->cp0_random;
    case 2u: return &state_->cp0_entrylo0;
    case 3u: return &state_->cp0_entrylo1;
    case 4u: return &state_->cp0_context;
    case 5u: return &state_->cp0_pagemask;
    case 6u: return &state_->cp0_wired;
    case 8u: return &state_->cp0_badvaddr;
    case 9u: return &state_->count_save;
    case 10u: return &state_->cp0_entryhi;
    case 11u: return &state_->cp0_compare;
    case 12u: return &state_->cp0_status;
    case 13u: return &state_->cp0_cause;
    case 14u: return &state_->cp0_epc;
    case 15u: return &state_->cp0_prid;
    case 16u: return &state_->cp0_config;
    case 17u: return &state_->cp0_lladdr;
    case 18u: return &state_->cp0_watchlo;
    case 19u: return &state_->cp0_watchhi;
    case 28u: return &state_->cp0_taglo;
    case 29u: return &state_->cp0_taghi;
    case 30u: return &state_->cp0_errorepc;
    default: return nullptr;
    }
}

cerfengine::MipsCp0Decision ProbeSystem::MipsCp0(uint32_t rd) {
    return MipsCp0(rd, 0u);
}

cerfengine::MipsCp0Decision ProbeSystem::MipsCp0(uint32_t rd, uint32_t sel) {
    cerfengine::MipsCp0Decision decision;
    if (rd == 16u && sel == 1u) {
        decision.present = true;
        decision.read = &ReadConfig1;
        decision.read_context = this;
        return decision;
    }
    if (sel != 0u || !Storage(rd)) return decision;
    decision.present = true;
    decision.read = &ReadRegister;
    decision.read_context = &registers_[rd];
    decision.writable = rd != 1u && rd != 8u && rd != 15u && rd != 17u;
    decision.write = &WriteRegister;
    decision.write_context = &registers_[rd];
    return decision;
}

uint32_t ProbeSystem::RandomIndex() {
    if (state_->cp0_wired >= state_->nb_tlb) TestFail("CP0 Wired leaves no random TLB slot");
    const uint32_t slots = state_->nb_tlb - state_->cp0_wired;
    return state_->cp0_wired +
           (slots - 1u - static_cast<uint32_t>((host_.Cycles() - random_anchor_) % slots));
}

uint32_t ProbeSystem::Read(uint32_t rd) {
    if (rd == 9u) return count_base_ + static_cast<uint32_t>(host_.Cycles() - count_anchor_);
    if (rd == 1u) return RandomIndex();
    return *Storage(rd);
}

void ProbeSystem::Write(uint32_t rd, uint32_t value) {
    if (rd == 9u || rd == 11u) ClockTick();
    if (host_.Trace().Hit("CP0_WRITE", rd)) {
        std::printf("CP0_WRITE rd=%u value=%08X pc=%08X\n", rd, value, host_.Pc());
    }
    switch (rd) {
    case 0u:
        value = (state_->cp0_index & 0x80000000u) |
                (value & (std::bit_ceil(state_->nb_tlb) - 1u));
        break;
    case 2u:
    case 3u: value &= 0x3FFFFFFFu; break;
    case 4u: value = (value & 0xFF800000u) | (state_->cp0_context & 0x007FFFF0u); break;
    case 5u: value &= 0x01FFE000u; break;
    case 6u:
        value &= 0x3Fu;
        if (value >= state_->nb_tlb) TestFail("CP0 Wired=%u nb_tlb=%u", value, state_->nb_tlb);
        random_anchor_ = host_.Cycles();
        break;
    case 9u:
        count_base_ = value;
        count_anchor_ = host_.Cycles();
        break;
    case 10u:
        value &= 0xFFFFE0FFu;
        if (((value ^ state_->cp0_entryhi) & 0xFFu) != 0u) tlb_->InvalidateAddressSpace();
        break;
    case 11u: state_->cp0_cause &= ~cerfengine::kMipsCauseTimerIp; break;
    case 12u:
        if ((value & 0x02000000u) != 0u) TestFail("reverse-endian user accesses are unmodeled");
        value &= 0x1A58FF17u;
        break;
    case 13u:
        if ((value & 0x00400000u) != 0u) TestFail("deferred watch exceptions are unmodeled");
        value = (state_->cp0_cause & ~0x00800300u) | (value & 0x00800300u);
        break;
    case 16u: value = (state_->cp0_config & ~7u) | (value & 7u); break;
    case 18u:
        if ((value & 7u) != 0u) TestFail("armed watchpoints are unmodeled: WatchLo=%08X", value);
        break;
    case 19u: value &= 0x40FF0FF8u; break;
    default: break;
    }
    *Storage(rd) = value;
    if (rd == 9u || rd == 11u) ArmCompare(host_.Cycles());
}

uint32_t CERFENGINE_FASTCALL ProbeSystem::ReadRegister(void* context) {
    const auto& reg = *static_cast<Register*>(context);
    return reg.system->Read(reg.rd);
}

void CERFENGINE_FASTCALL ProbeSystem::WriteRegister(void* context, uint32_t value) {
    const auto& reg = *static_cast<Register*>(context);
    reg.system->Write(reg.rd, value);
}

uint32_t CERFENGINE_FASTCALL ProbeSystem::ReadConfig1(void* context) {
    return static_cast<ProbeSystem*>(context)->config1_;
}


void ProbeSystem::FillTlb(cerfengine::MipsTlbContext& tlb, uint32_t index) {
    auto& state = *tlb.State();
    if (index >= state.nb_tlb) TestFail("TLB index=%u nb_tlb=%u", index, state.nb_tlb);
    tlb.InvalidateAddressSpace();
    auto& entry = state.tlb[index];
    entry.vpn = state.cp0_entryhi & 0xFFFFE000u;
    entry.asid = state.cp0_entryhi & 0xFFu;
    entry.page_mask = state.cp0_pagemask;
    entry.g = static_cast<uint8_t>(state.cp0_entrylo0 & state.cp0_entrylo1 & 1u);
    entry.v0 = static_cast<uint8_t>((state.cp0_entrylo0 >> 1u) & 1u);
    entry.d0 = static_cast<uint8_t>((state.cp0_entrylo0 >> 2u) & 1u);
    entry.c0 = static_cast<uint8_t>((state.cp0_entrylo0 >> 3u) & 7u);
    entry.v1 = static_cast<uint8_t>((state.cp0_entrylo1 >> 1u) & 1u);
    entry.d1 = static_cast<uint8_t>((state.cp0_entrylo1 >> 2u) & 1u);
    entry.c1 = static_cast<uint8_t>((state.cp0_entrylo1 >> 3u) & 7u);
    const uint32_t half_mask = (state.cp0_pagemask | 0x1FFFu) >> 1u;
    entry.pfn[0] = (static_cast<uint64_t>(state.cp0_entrylo0 >> 6u) << 12u) &
                   ~static_cast<uint64_t>(half_mask);
    entry.pfn[1] = (static_cast<uint64_t>(state.cp0_entrylo1 >> 6u) << 12u) &
                   ~static_cast<uint64_t>(half_mask);
    state.tlb_in_use = state.nb_tlb;
    if (host_.Trace().Hit("TLB_WRITE", 0u)) {
        std::printf("TLB_WRITE index=%u hi=%08X mask=%08X lo0=%08X lo1=%08X pc=%08X\n",
                    index, state.cp0_entryhi, state.cp0_pagemask, state.cp0_entrylo0,
                    state.cp0_entrylo1, state.pc);
    }
}

void CERFENGINE_FASTCALL ProbeSystem::WriteIndexed(void* context, cerfengine::MipsTlbContext& tlb) {
    static_cast<ProbeSystem*>(context)->FillTlb(tlb, tlb.State()->cp0_index & 0x3Fu);
}

void CERFENGINE_FASTCALL ProbeSystem::WriteRandom(void* context, cerfengine::MipsTlbContext& tlb) {
    auto& self = *static_cast<ProbeSystem*>(context);
    self.FillTlb(tlb, self.RandomIndex());
}

void CERFENGINE_FASTCALL ProbeSystem::Probe(void*, cerfengine::MipsTlbContext& tlb) {
    auto& state = *tlb.State();
    for (uint32_t i = 0u; i < state.nb_tlb; ++i) {
        const auto& entry = state.tlb[i];
        const uint32_t mask = entry.page_mask | 0x1FFFu;
        if ((entry.g || entry.asid == (state.cp0_entryhi & 0xFFu)) &&
            (entry.vpn & ~mask) == (state.cp0_entryhi & ~mask)) {
            state.cp0_index = i;
            return;
        }
    }
    state.cp0_index |= 0x80000000u;
}

void CERFENGINE_FASTCALL ProbeSystem::ReadTlb(void*, cerfengine::MipsTlbContext& tlb) {
    auto& state = *tlb.State();
    const uint32_t index = state.cp0_index & 0x3Fu;
    if (index >= state.nb_tlb) TestFail("TLBR index=%u", index);
    const auto& entry = state.tlb[index];
    if (entry.asid != (state.cp0_entryhi & 0xFFu)) tlb.InvalidateAddressSpace();
    state.cp0_entryhi = entry.vpn | entry.asid;
    state.cp0_pagemask = entry.page_mask;
    state.cp0_entrylo0 = static_cast<uint32_t>(entry.pfn[0] >> 12u) << 6u |
                         (entry.c0 << 3u) | (entry.d0 << 2u) | (entry.v0 << 1u) | entry.g;
    state.cp0_entrylo1 = static_cast<uint32_t>(entry.pfn[1] >> 12u) << 6u |
                         (entry.c1 << 3u) | (entry.d1 << 2u) | (entry.v1 << 1u) | entry.g;
}

cerfengine::MipsTlbResult CERFENGINE_FASTCALL ProbeSystem::MapAddress(
    void* context, cerfengine::MipsCpuState* state, uint32_t va,
    cerfengine::MipsAccess access, uint32_t* pa) {
    for (uint32_t i = 0u; i < state->nb_tlb; ++i) {
        const auto& entry = state->tlb[i];
        const uint32_t mask = entry.page_mask | 0x1FFFu;
        if (!entry.g && entry.asid != (state->cp0_entryhi & 0xFFu)) continue;
        if ((entry.vpn & ~mask) != (va & ~mask)) continue;
        const bool odd = (va & ((mask >> 1u) + 1u)) != 0u;
        if (!(odd ? entry.v1 : entry.v0)) return cerfengine::MipsTlbResult::kInvalid;
        if (access == cerfengine::MipsAccess::kWrite && !(odd ? entry.d1 : entry.d0)) {
            return cerfengine::MipsTlbResult::kModified;
        }
        const uint64_t physical = entry.pfn[odd ? 1u : 0u] | (va & (mask >> 1u));
        if (physical > UINT32_MAX) {
            TestFail("TLB physical address exceeds the 32-bit Engine bus: %llX",
                     static_cast<unsigned long long>(physical));
        }
        *pa = static_cast<uint32_t>(physical);
        return cerfengine::MipsTlbResult::kMatch;
    }
    auto& host = static_cast<ProbeSystem*>(context)->host_;
    if (host.Trace().Hit("TLB_MISS", 0u)) {
        std::printf("TLB_MISS va=%08X access=%u pc=%08X asid=%02X\n", va,
                    static_cast<uint32_t>(access), state->pc, state->cp0_entryhi & 0xFFu);
    }
    return cerfengine::MipsTlbResult::kNoMatch;
}

bool CERFENGINE_FASTCALL ProbeSystem::PageGlobal(void*, cerfengine::MipsCpuState* state, uint32_t va) {
    for (uint32_t i = 0u; i < state->nb_tlb; ++i) {
        const auto& entry = state->tlb[i];
        const uint32_t mask = entry.page_mask | 0x1FFFu;
        if ((entry.g || entry.asid == (state->cp0_entryhi & 0xFFu)) &&
            (entry.vpn & ~mask) == (va & ~mask)) return entry.g != 0u;
    }
    return false;
}


void CERFENGINE_FASTCALL ProbeSystem::EnterException(void* context, cerfengine::MipsCpuState* state,
                                                     uint32_t cause, bool refill) {
    auto& s = *state;
    auto& host = static_cast<ProbeSystem*>(context)->host_;
    if (host.Trace().Hit("EXCEPTION", cause)) {
        std::printf("EXCEPTION cause=%u pc=%08X badvaddr=%08X status=%08X\n",
                    cause, s.pc, s.cp0_badvaddr, s.cp0_status);
        if (cause == cerfengine::MipsExcCode::kAdEL || cause == cerfengine::MipsExcCode::kAdES) {
            for (uint32_t i = 0u; i < 32u; i += 4u) {
                std::printf("FAULT_REG r%u=%08X r%u=%08X r%u=%08X r%u=%08X\n",
                            i, static_cast<uint32_t>(s.gpr[i]), i + 1u,
                            static_cast<uint32_t>(s.gpr[i + 1u]), i + 2u,
                            static_cast<uint32_t>(s.gpr[i + 2u]), i + 3u,
                            static_cast<uint32_t>(s.gpr[i + 3u]));
            }
        }
    }
    const bool exl = (s.cp0_status & 2u) != 0u;
    if (!exl) {
        const bool delay = s.branch_state != cerfengine::MipsBranch::kNone;
        s.cp0_epc = delay ? s.pc - s.branch_len : s.pc;
        s.cp0_cause = (s.cp0_cause & 0x7FFFFFFFu) | (delay ? 0x80000000u : 0u);
        s.cp0_status |= 2u;
    }
    const uint32_t base = (s.cp0_status & 0x00400000u) != 0u ? 0xBFC00200u : 0x80000000u;
    const bool interrupt_vector = cause == cerfengine::MipsExcCode::kInt &&
                                  (s.cp0_cause & 0x00800000u) != 0u;
    s.pc = base + (interrupt_vector ? 0x200u : (refill && !exl ? 0u : 0x180u));
    s.cp0_cause = (s.cp0_cause & ~0x7Cu) | (cause << 2u);
    s.isa_mode = 0u;
    s.branch_state = cerfengine::MipsBranch::kNone;
}

void CERFENGINE_FASTCALL ProbeSystem::FaultRegisters(void*, cerfengine::MipsCpuState* state, uint32_t va) {
    state->cp0_badvaddr = va;
    state->cp0_context = (state->cp0_context & 0xFF800000u) | ((va >> 9u) & 0x007FFFF0u);
    state->cp0_entryhi = (state->cp0_entryhi & 0xFFu) | (va & 0xFFFFE000u);
}

bool CERFENGINE_FASTCALL ProbeSystem::InterruptsEnabled(void*, const cerfengine::MipsCpuState& state) {
    return (state.cp0_status & 7u) == 1u;
}

void CERFENGINE_FASTCALL ProbeSystem::Eret(void*, cerfengine::MipsCpuState* state) {
    if ((state->cp0_status & 4u) != 0u) {
        state->pc = state->cp0_errorepc;
        state->cp0_status &= ~4u;
    } else {
        state->pc = state->cp0_epc;
        state->cp0_status &= ~2u;
    }
    state->llbit = 0u;
}

void CERFENGINE_FASTCALL ProbeSystem::Rfe(void*, cerfengine::MipsCpuState*) {
    TestFail("RFE is unavailable on this MIPS32R1 probe");
}

}
