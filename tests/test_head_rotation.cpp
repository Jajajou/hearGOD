#include <gtest/gtest.h>
#include "hearGOD/head_rotation.h"
#include <cmath>

using namespace hearGOD;

static constexpr float kTol = 1e-4f;

static SpeakerPosition sp(float az, float el, float dist = 1.0f) {
    return {az, el, dist};
}

// Yaw 90° (head turns left): front source (az 0) should appear on the right (az -90)
TEST(HeadRotation, Yaw90TurnsSourceRight)
{
    auto r = rotateByHead(sp(0.0f, 0.0f), 90.0f, 0.0f, 0.0f);
    EXPECT_NEAR(r.azimuth, -90.0f, 1.0f);
    EXPECT_NEAR(r.elevation, 0.0f, kTol);
}

// Yaw 90° on FL (az 30°): az should be ~30-90 = -60°
TEST(HeadRotation, Yaw90RotatesFL)
{
    auto r = rotateByHead(sp(30.0f, 0.0f), 90.0f, 0.0f, 0.0f);
    EXPECT_NEAR(r.azimuth, -60.0f, 1.0f);
    EXPECT_NEAR(r.elevation, 0.0f, kTol);
}

// Zero rotation: source unchanged
TEST(HeadRotation, ZeroRotationIdentity)
{
    auto r = rotateByHead(sp(45.0f, 20.0f), 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(r.azimuth,   45.0f, kTol);
    EXPECT_NEAR(r.elevation, 20.0f, kTol);
    EXPECT_NEAR(r.distance,   1.0f, kTol);
}

// Pitch 90° (head tips forward) with source at front: source appears directly overhead (+90°)
// Rᵀ·[1,0,0] with Ry(90°) = [0,0,1] → el=+90°
TEST(HeadRotation, Pitch90TurnsSourceUp)
{
    auto r = rotateByHead(sp(0.0f, 0.0f), 0.0f, 90.0f, 0.0f);
    EXPECT_NEAR(r.elevation, 90.0f, 1.0f);
}

// Round-trip: rotate by yaw then by -yaw → original
TEST(HeadRotation, RoundTrip)
{
    const float yaw = 37.5f;
    auto r1 = rotateByHead(sp(20.0f, 10.0f), yaw, 0.0f, 0.0f);
    auto r2 = rotateByHead(r1, -yaw, 0.0f, 0.0f);
    EXPECT_NEAR(r2.azimuth,   20.0f, 0.1f);
    EXPECT_NEAR(r2.elevation, 10.0f, 0.1f);
}

// Distance preserved through rotation
TEST(HeadRotation, DistancePreserved)
{
    auto r = rotateByHead(sp(30.0f, 15.0f, 3.5f), 45.0f, 10.0f, 5.0f);
    EXPECT_NEAR(r.distance, 3.5f, kTol);
}
