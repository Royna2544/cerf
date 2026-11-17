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
#include <string>

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

inline void Au1374ProbeWideStringTraces(cerfcore::CerfEmulator& emu) {
    const auto* input = std::getenv("CERF_AU1374_PROBE_TRACE_WSTR");
    if (!input) return;
    std::istringstream entries(input);
    std::string entry;
    while (entries >> entry) {
        const auto colon = entry.find(':');
        const uint32_t pc  = static_cast<uint32_t>(std::stoul(entry.substr(0u, colon), nullptr, 16));
        const uint32_t reg = colon == std::string::npos ? 5u : static_cast<uint32_t>(std::stoul(entry.substr(colon + 1u)));
        if (reg >= 32u) emu.Get<cerfcore::Fatal>().UserError("Probe wide-string register %u", reg);
        emu.Get<cerfcore::TraceManager>().OnPc(pc, [reg](const cerfcore::TraceContext& ctx) {
            const uint32_t va = static_cast<uint32_t>(ctx.mips->gpr[reg]);
            std::string text;
            for (uint32_t i = 0u; i < 64u; ++i) {
                const auto unit = ctx.ReadVa16(va + i * 2u);
                if (!unit) {
                    text += "<unmapped>";
                    break;
                }
                if (*unit == 0u) break;
                text += *unit < 0x80u ? static_cast<char>(*unit) : '?';
            }
            LOG(Board, "PROBE wstr=%08X r%u=%08X \"%s\"\n", ctx.pc, reg, va, text.c_str());
        });
    }
}

inline std::string Au1374ProbeWords(const cerfcore::TraceContext& ctx, uint32_t va, uint32_t words) {
    std::string text;
    char        item[16];
    for (uint32_t i = 0u; i < words; ++i) {
        const auto word = ctx.ReadVa32(va + i * 4u);
        if (!word) return text + " <unmapped>";
        std::snprintf(item, sizeof(item), " %08X", *word);
        text += item;
    }
    return text;
}

inline void Au1374ProbeMemoryTraces(cerfcore::CerfEmulator& emu) {
    const auto* input = std::getenv("CERF_AU1374_PROBE_TRACE_MEM");
    if (!input) return;
    std::istringstream entries(input);
    std::string entry;
    while (entries >> entry) {
        uint32_t fields[5]{0u, 5u, 4u, 0u, 0u};
        std::istringstream parts(entry);
        std::string part;
        for (uint32_t i = 0u; i < 5u && std::getline(parts, part, ':'); ++i) {
            fields[i] = static_cast<uint32_t>(std::stoul(part, nullptr, i == 0u ? 16 : 10));
        }
        const uint32_t reg = fields[1], words = fields[2], index = fields[3], inner = fields[4];
        if (reg >= 32u || words > 32u || inner > 32u || (inner != 0u && index >= words)) {
            emu.Get<cerfcore::Fatal>().UserError("Probe memory trace entry %s", entry.c_str());
        }
        auto count = std::make_shared<uint32_t>(0u);
        emu.Get<cerfcore::TraceManager>().OnPc(fields[0], [=](const cerfcore::TraceContext& ctx) {
            if ((*count)++ >= 256u) return;
            const uint32_t va    = static_cast<uint32_t>(ctx.mips->gpr[reg]);
            std::string    outer = Au1374ProbeWords(ctx, va, words);
            std::string    deref;
            if (inner != 0u) {
                const auto pointer = ctx.ReadVa32(va + index * 4u);
                deref = pointer ? " ->" + Au1374ProbeWords(ctx, *pointer, inner) : " -> <unmapped>";
            }
            LOG(Board, "PROBE mem=%08X a0=%08X a1=%08X r%u=%08X:%s%s\n", ctx.pc,
                static_cast<uint32_t>(ctx.mips->gpr[4u]), static_cast<uint32_t>(ctx.mips->gpr[5u]), reg, va,
                outer.c_str(), deref.c_str());
        });
    }
}

inline void Au1374ProbeTraces(cerfcore::CerfEmulator& emu) {
    Au1374ProbeWideStringTraces(emu);
    Au1374ProbeMemoryTraces(emu);
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
