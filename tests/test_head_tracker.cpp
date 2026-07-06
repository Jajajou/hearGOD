#include <gtest/gtest.h>
#include "hearGOD/head_tracker.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

using namespace hearGOD;

// Send one OpenTrack UDP packet: 6 little-endian doubles (x,y,z,yaw,pitch,roll)
static void sendPacket(uint16_t port, double x, double y, double z,
                       double yaw, double pitch, double roll)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;

    double buf[6] = {x, y, z, yaw, pitch, roll};
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    ::sendto(s, buf, sizeof(buf), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    ::close(s);
}

TEST(HeadTracker, ParsesPoseFromLoopback)
{
    static constexpr uint16_t kPort = 14243; // avoid collision with real tracker on 4242
    HeadTracker tracker;
    ASSERT_TRUE(tracker.start(kPort));

    // Not receiving yet
    EXPECT_FALSE(tracker.isReceiving());

    // Send a packet: yaw=45.0, pitch=-10.0, roll=0.0
    sendPacket(kPort, 0.0, 0.0, 0.0, 45.0, -10.0, 0.0);

    // Give recv thread up to 300 ms
    using namespace std::chrono_literals;
    for (int i = 0; i < 30 && !tracker.isReceiving(); ++i)
        std::this_thread::sleep_for(10ms);

    EXPECT_TRUE(tracker.isReceiving());
    auto p = tracker.getPose();
    EXPECT_NEAR(p.yaw,   45.0f, 0.01f);
    EXPECT_NEAR(p.pitch, -10.0f, 0.01f);
    EXPECT_NEAR(p.roll,   0.0f,  0.01f);
}

TEST(HeadTracker, IgnoresShortPacket)
{
    static constexpr uint16_t kPort = 14244;
    HeadTracker tracker;
    ASSERT_TRUE(tracker.start(kPort));

    // Send malformed (short) packet
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    uint8_t garbage[10] = {0xFF};
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(kPort);
    ::inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    ::sendto(s, garbage, sizeof(garbage), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    ::close(s);

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(tracker.isReceiving());

    // Pose should remain zero
    auto p = tracker.getPose();
    EXPECT_NEAR(p.yaw,   0.0f, 0.01f);
    EXPECT_NEAR(p.pitch, 0.0f, 0.01f);
}

TEST(HeadTracker, StopJoinsCleanly)
{
    static constexpr uint16_t kPort = 14245;
    HeadTracker tracker;
    ASSERT_TRUE(tracker.start(kPort));
    EXPECT_TRUE(tracker.isRunning());
    tracker.stop();
    EXPECT_FALSE(tracker.isRunning());
}
