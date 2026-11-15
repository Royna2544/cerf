#pragma once

#include "../../libCerfCore/src/execution/mips_engine_bridge.h"
#include "../../libCerfCore/src/tracing/trace_manager.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfengine/mips/mips_cpu.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

inline void Au1374ProbeCpu(cerfcore::CerfEmulator& emu) {
    const auto* s = emu.Get<cerfcore::MipsEngineBridge>().Engine().Get<cerfengine::MipsCpu>().State();
    LOG(Board, "PROBE pc=%08X ra=%08X sp=%08X a0=%08X a1=%08X a2=%08X a3=%08X s0=%08X s1=%08X\n",
        s->pc, static_cast<uint32_t>(s->gpr[31u]), static_cast<uint32_t>(s->gpr[29u]),
        static_cast<uint32_t>(s->gpr[4u]), static_cast<uint32_t>(s->gpr[5u]),
        static_cast<uint32_t>(s->gpr[6u]), static_cast<uint32_t>(s->gpr[7u]),
        static_cast<uint32_t>(s->gpr[16u]), static_cast<uint32_t>(s->gpr[17u]));
}

inline void Au1374ProbeDump(cerfcore::CerfEmulator& emu, uint32_t address,
                           uint32_t bytes, const std::string& path) {
    const auto* data = emu.Get<cerfcore::EmulatedMemory>().TryTranslateRange(address, bytes);
    if (!data) emu.Get<cerfcore::Fatal>().UserError("Probe unmapped dump: %08X/%u", address, bytes);
    auto* file = std::fopen(path.c_str(), "wb");
    if (!file) emu.Get<cerfcore::Fatal>().UserError("Probe dump open: %s", path.c_str());
    const auto done = std::fwrite(data, 1u, bytes, file);
    std::fclose(file);
    if (done != bytes) emu.Get<cerfcore::Fatal>().UserError("Probe dump short write: %s", path.c_str());
}

inline void Au1374ProbeTraces(cerfcore::CerfEmulator& emu) {
    const auto* input = std::getenv("CERF_AU1374_PROBE_TRACE");
    if (!input) return;
    std::istringstream addresses(input);
    uint32_t pc = 0u;
    while (addresses >> std::hex >> pc) {
        auto count = std::make_shared<uint32_t>(0u);
        emu.Get<cerfcore::TraceManager>().OnPc(pc, [count](const cerfcore::TraceContext& ctx) {
            if ((*count)++ >= 64u) return;
            const auto* s = ctx.mips;
            LOG(Board, "PROBE trace=%08X hit=%u ra=%08X a0=%08X a1=%08X a2=%08X v0=%08X sp=%08X\n",
                ctx.pc, *count, static_cast<uint32_t>(s->gpr[31u]), static_cast<uint32_t>(s->gpr[4u]),
                static_cast<uint32_t>(s->gpr[5u]), static_cast<uint32_t>(s->gpr[6u]),
                static_cast<uint32_t>(s->gpr[2u]), static_cast<uint32_t>(s->gpr[29u]));
        });
    }
}
