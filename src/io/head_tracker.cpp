#include "hearGOD/head_tracker.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace hearGOD {

static int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool HeadTracker::start(uint16_t port)
{
    if (running_.load()) return true;

    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        std::perror("HeadTracker socket");
        return false;
    }

    int reuse = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // recv timeout so recvLoop can notice stop() and join cleanly
    timeval tv { 0, 200000 };  // 200 ms
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("HeadTracker bind");
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&HeadTracker::recvLoop, this);
    return true;
}

void HeadTracker::stop()
{
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

void HeadTracker::recvLoop()
{
    // OpenTrack packet: 6 doubles LE = x, y, z, yaw, pitch, roll
    double buf[6];
    while (running_.load(std::memory_order_acquire)) {
        ssize_t n = ::recvfrom(sock_, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n != sizeof(buf)) continue;  // timeout, error, or wrong-size packet

        // ponytail: assumes little-endian host (all Apple Silicon / x86) —
        // add byte swap if ever ported to a BE platform.
        yaw_.store(static_cast<float>(buf[3]), std::memory_order_relaxed);
        pitch_.store(static_cast<float>(buf[4]), std::memory_order_relaxed);
        roll_.store(static_cast<float>(buf[5]), std::memory_order_relaxed);
        lastPacketMs_.store(nowMs(), std::memory_order_relaxed);
    }
}

bool HeadTracker::isReceiving() const
{
    int64_t last = lastPacketMs_.load(std::memory_order_relaxed);
    return last != 0 && (nowMs() - last) < 500;
}

} // namespace hearGOD
