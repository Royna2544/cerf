#pragma once

#include "probe_trace.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace au13xxprobe {

class ProbeNand {
public:
    ProbeNand();
    void Import(const std::string& path, uint32_t start_page);
    void ImportRaw(const std::string& path);
    void Command(uint8_t command);
    void Address(uint8_t value);
    uint8_t Read();
    void Write(uint8_t value);
    void Summary() const;

private:
    enum class Mode { kRead, kProgram, kErase, kId, kStatus };
    void Begin(Mode mode);
    void FinishAddress();
    void ConsumePointer();
    void Program();
    void Erase();
    std::vector<uint8_t> cells_;
    std::array<uint8_t, 528u> input_;
    ProbeTrace trace_;
    Mode mode_ = Mode::kRead;
    Mode status_from_ = Mode::kRead;
    uint32_t pointer_ = 0u;
    uint32_t row_ = 0u;
    uint32_t column_ = 0u;
    uint32_t address_cycle_ = 0u;
    uint32_t id_index_ = 0u;
    uint32_t latched_read_block_ = 0u;
    bool addressed_ = false;
    bool resume_address_ = false;
    bool read_started_ = false;
    bool require_same_block_ = false;
};

}
