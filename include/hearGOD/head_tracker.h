#pragma once
#include <atomic>
#include <thread>
#include <cstdint>
#include <chrono>

namespace hearGOD {

struct HeadPose {
    float yaw   = 0.0f;  // degrees
    float pitch = 0.0f;
    float roll  = 0.0f;
};

// Receives OpenTrack "UDP over network" output packets:
// 6 little-endian doubles = x, y, z (cm), yaw, pitch, roll (degrees), 48 bytes.
// Runs its own recv thread; pose is published via atomics (lock-free reads).
class HeadTracker {
public:
    HeadTracker() = default;
    ~HeadTracker() { stop(); }

    HeadTracker(const HeadTracker&) = delete;
    HeadTracker& operator=(const HeadTracker&) = delete;

    // Bind UDP port and start recv thread. Returns false on bind failure.
    bool start(uint16_t port = 4242);
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    // True if a valid packet arrived within the last 500 ms.
    bool isReceiving() const;

    HeadPose getPose() const {
        return { yaw_.load(std::memory_order_relaxed),
                 pitch_.load(std::memory_order_relaxed),
                 roll_.load(std::memory_order_relaxed) };
    }

private:
    void recvLoop();

    std::atomic<float> yaw_{0.0f}, pitch_{0.0f}, roll_{0.0f};
    std::atomic<int64_t> lastPacketMs_{0};  // steady_clock ms of last valid packet
    std::atomic<bool> running_{false};
    std::thread thread_;
    int sock_ = -1;
};

} // namespace hearGOD
