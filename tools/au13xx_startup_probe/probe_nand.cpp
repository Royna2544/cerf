#include "probe_nand.h"

#include "test_host.h"

#include <fstream>

namespace au13xxprobe {

ProbeNand::ProbeNand() : cells_(131072u * 528u, 0xFFu) {
    input_.fill(0xFFu);
}

void ProbeNand::Import(const std::string& path, uint32_t start_page) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) TestFail("NAND import open: %s", path.c_str());
    const auto size = file.tellg();
    if (size <= 0 || size % 512 != 0 || start_page >= 131072u ||
        static_cast<uint64_t>(size) / 512u > 131072u - start_page) {
        TestFail("NAND import size or start page is invalid");
    }
    file.seekg(0);
    for (uint64_t i = 0u; i < static_cast<uint64_t>(size) / 512u; ++i) {
        file.read(reinterpret_cast<char*>(cells_.data() + (start_page + i) * 528u), 512);
        if (!file) TestFail("NAND import short read");
    }
    std::printf("NAND_IMPORT main_bytes=%llu start_page=%X erased_OOB=1 synthetic_linear=1\n",
                static_cast<unsigned long long>(size), start_page);
}

void ProbeNand::ImportRaw(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) TestFail("NAND raw import open: %s", path.c_str());
    if (file.tellg() != static_cast<std::streamoff>(cells_.size())) {
        TestFail("NAND raw import requires exactly 131072 pages of 512+16 bytes");
    }
    file.seekg(0);
    file.read(reinterpret_cast<char*>(cells_.data()), cells_.size());
    if (!file) TestFail("NAND raw import short read");
    std::printf("NAND_RAW_IMPORT bytes=%zu main_and_OOB=1\n", cells_.size());
}

void ProbeNand::Begin(Mode mode) {
    mode_ = mode;
    row_ = 0u;
    column_ = 0u;
    address_cycle_ = 0u;
    addressed_ = false;
    resume_address_ = false;
    read_started_ = false;
    require_same_block_ = false;
}

void ProbeNand::ConsumePointer() {
    if (pointer_ == 256u) pointer_ = 0u;
}

void ProbeNand::Command(uint8_t command) {
    if (trace_.Hit("NAND_COMMAND", command)) std::printf("NAND_COMMAND %02X\n", command);
    if (mode_ == Mode::kProgram && command != 0x10u && command != 0xFFu) {
        TestFail("NAND command %02X during data input", command);
    }
    if (mode_ == Mode::kErase && command != 0xD0u && command != 0xFFu) {
        TestFail("NAND command %02X during erase setup", command);
    }
    switch (command) {
    case 0x00u:
    case 0x01u:
    case 0x50u:
        pointer_ = command == 0x50u ? 512u : (command == 0x01u ? 256u : 0u);
        if (mode_ == Mode::kStatus && status_from_ == Mode::kRead && addressed_) {
            mode_ = Mode::kRead;
            resume_address_ = true;
        } else {
            Begin(Mode::kRead);
        }
        return;
    case 0x80u:
        Begin(Mode::kProgram);
        input_.fill(0xFFu);
        return;
    case 0x60u:
        Begin(Mode::kErase);
        return;
    case 0x90u:
        Begin(Mode::kId);
        id_index_ = 0u;
        return;
    case 0x70u:
        if (mode_ != Mode::kStatus) status_from_ = mode_;
        mode_ = Mode::kStatus;
        return;
    case 0x10u:
        if (mode_ != Mode::kProgram || !addressed_) TestFail("NAND program without address");
        Program();
        break;
    case 0xD0u:
        if (mode_ != Mode::kErase || !addressed_) TestFail("NAND erase without address");
        Erase();
        break;
    case 0xFFu:
        ConsumePointer();
        Begin(Mode::kRead);
        return;
    default:
        TestFail("NAND unsupported command=%02X", command);
    }
    status_from_ = mode_;
    mode_ = Mode::kStatus;
    addressed_ = false;
    ConsumePointer();
}

void ProbeNand::FinishAddress() {
    if (row_ >= 131072u) TestFail("NAND row outside device=%X", row_);
    if (mode_ == Mode::kRead && require_same_block_ && row_ / 32u != latched_read_block_) {
        TestFail("NAND random read crosses block without a new command");
    }
    if (mode_ != Mode::kErase) column_ = pointer_ + (pointer_ == 512u ? column_ & 15u : column_);
    addressed_ = true;
    ConsumePointer();
    if (trace_.Hit("NAND_ADDRESS", static_cast<uint32_t>(mode_))) {
        std::printf("NAND_ADDRESS mode=%u row=%X column=%X\n", static_cast<uint32_t>(mode_), row_, column_);
    }
}

void ProbeNand::Address(uint8_t value) {
    if (resume_address_) {
        Begin(Mode::kRead);
    } else if (mode_ == Mode::kRead && addressed_ && read_started_) {
        const uint32_t block = row_ / 32u;
        Begin(Mode::kRead);
        latched_read_block_ = block;
        require_same_block_ = true;
    }
    if (addressed_) TestFail("NAND address after a complete address phase");
    if (mode_ == Mode::kId) {
        if (value != 0u || address_cycle_ != 0u) TestFail("NAND ID address=%X", value);
        ++address_cycle_;
        addressed_ = true;
        return;
    }
    if (mode_ == Mode::kErase) {
        row_ |= static_cast<uint32_t>(value) << (8u * address_cycle_++);
        if (address_cycle_ == 3u) FinishAddress();
        return;
    }
    if (mode_ != Mode::kRead && mode_ != Mode::kProgram) TestFail("NAND address without setup");
    if (address_cycle_ == 0u) column_ = value;
    else row_ |= static_cast<uint32_t>(value) << (8u * (address_cycle_ - 1u));
    if (++address_cycle_ == 4u) FinishAddress();
}

uint8_t ProbeNand::Read() {
    resume_address_ = false;
    if (mode_ == Mode::kStatus) return 0xC0u;
    if (!addressed_) TestFail("NAND data read before address");
    if (mode_ == Mode::kId) {
        constexpr std::array<uint8_t, 4u> id = {0xECu, 0x76u, 0x5Au, 0x3Fu};
        if (id_index_ >= id.size()) TestFail("NAND ID read past modeled ID");
        return id[id_index_++];
    }
    if (mode_ != Mode::kRead) TestFail("NAND data read outside read mode");
    read_started_ = true;
    if (column_ == 528u) {
        if ((row_ & 31u) == 31u) TestFail("NAND sequential read crosses block boundary");
        ++row_;
        column_ = pointer_ == 512u ? 512u : 0u;
    }
    return cells_[row_ * 528u + column_++];
}

void ProbeNand::Write(uint8_t value) {
    if (mode_ != Mode::kProgram || !addressed_ || column_ >= 528u) TestFail("NAND invalid data input");
    input_[column_++] = value;
}

void ProbeNand::Program() {
    for (size_t i = 0u; i < input_.size(); ++i) cells_[row_ * 528u + i] &= input_[i];
    if (trace_.Hit("NAND_PROGRAM", 0u, 128u)) {
        std::printf("NAND_PROGRAM row=%X OOB=", row_);
        for (uint32_t i = 512u; i < 528u; ++i) std::printf("%02X", cells_[row_ * 528u + i]);
        std::printf("\n");
    }
}

void ProbeNand::Erase() {
    const uint32_t first = (row_ & ~31u) * 528u;
    std::fill_n(cells_.begin() + first, 32u * 528u, 0xFFu);
    if (trace_.Hit("NAND_ERASE", 0u)) std::printf("NAND_ERASE block=%X\n", row_ / 32u);
}

void ProbeNand::Summary() const {
    trace_.Summary();
}

}
