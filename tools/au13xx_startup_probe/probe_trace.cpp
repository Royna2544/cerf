#include "probe_trace.h"

#include <cstdio>

namespace au13xxprobe {

bool ProbeTrace::Hit(const std::string& category, uint32_t key, uint32_t limit) {
    uint64_t& count = counts_[{category, key}];
    ++count;
    if (count == static_cast<uint64_t>(limit) + 1u) {
        std::printf("TRACE_LIMIT %s key=%08X detail_limit=%u\n", category.c_str(), key, limit);
    }
    return count <= limit;
}

void ProbeTrace::Summary() const {
    for (const auto& [key, count] : counts_) {
        std::printf("TRACE_COUNT %s key=%08X count=%llu\n", key.first.c_str(), key.second,
                    static_cast<unsigned long long>(count));
    }
}

}
