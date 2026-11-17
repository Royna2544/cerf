#pragma once

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_nmea_feed.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

class Au1374ProbeNmea {
public:
    explicit Au1374ProbeNmea(cerfcore::CerfEmulator& emu) {
        const char* path = std::getenv("CERF_AU1374_PROBE_NMEA");
        if (path == nullptr) return;
        fd_ = ::open(path, O_RDONLY | O_NONBLOCK);
        if (fd_ < 0) emu.Get<cerfcore::Fatal>().UserError("Probe NMEA open: %s", path);
        feed_   = &emu.Get<cerfcore::HostNmeaFeed>();
        reader_ = std::thread([this] { Read(); });
    }

    ~Au1374ProbeNmea() {
        stop_.store(true);
        if (reader_.joinable()) reader_.join();
        if (fd_ >= 0) ::close(fd_);
    }

private:
    void Read() {
        std::string line;
        char        buffer[512];
        while (!stop_.load()) {
            pollfd fd{fd_, POLLIN, 0};
            if (::poll(&fd, 1, 100) <= 0) continue;
            const ssize_t got = ::read(fd_, buffer, sizeof(buffer));
            if (got == 0) {
                ::usleep(100000);
                continue;
            }
            if (got < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                return;
            }
            for (ssize_t i = 0; i < got; ++i) {
                if (buffer[i] != '\n') {
                    line += buffer[i];
                    continue;
                }
                feed_->PushSentence(line);
                line.clear();
            }
        }
    }

    int                        fd_   = -1;
    cerfcore::HostNmeaFeed*    feed_ = nullptr;
    std::atomic<bool>          stop_{false};
    std::thread                reader_;
};
