#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace au13xxprobe {

class ProbeTrace {
public:
    bool Hit(const std::string& category, uint32_t key, uint32_t limit = 16u);
    void Summary() const;

private:
    std::map<std::pair<std::string, uint32_t>, uint64_t> counts_;
};

}
