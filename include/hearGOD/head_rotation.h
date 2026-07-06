#pragma once
#include "hearGOD/types.h"
#include <cmath>
#include <numbers>

namespace hearGOD {

// OpenTrack sign conventions — flip here after real-world testing if the
// soundstage rotates the wrong way. Applied to incoming yaw/pitch/roll.
inline constexpr float kYawSign   = 1.0f;
inline constexpr float kPitchSign = 1.0f;
inline constexpr float kRollSign  = 1.0f;

// Rotate a speaker position by the inverse of the head rotation so the
// virtual source stays fixed in world space while the head moves.
//
// Coordinate frame (matches SOFA / sofa_loader degToCartesian):
//   x = front, y = left, z = up
//   azimuth 0° = front, +90° = left; elevation 0° = horizontal, +90° = up
// Head rotation R = Rz(yaw) · Ry(pitch) · Rx(roll); source' = Rᵀ · source.
inline SpeakerPosition rotateByHead(const SpeakerPosition& pos,
                                    float yawDeg, float pitchDeg, float rollDeg)
{
    constexpr float d2r = std::numbers::pi_v<float> / 180.0f;

    float az = pos.azimuth * d2r;
    float el = pos.elevation * d2r;

    // az/el → unit vector (x front, y left, z up)
    float x = std::cos(el) * std::cos(az);
    float y = std::cos(el) * std::sin(az);
    float z = std::sin(el);

    float cy = std::cos(kYawSign   * yawDeg   * d2r), sy = std::sin(kYawSign   * yawDeg   * d2r);
    float cp = std::cos(kPitchSign * pitchDeg * d2r), sp = std::sin(kPitchSign * pitchDeg * d2r);
    float cr = std::cos(kRollSign  * rollDeg  * d2r), sr = std::sin(kRollSign  * rollDeg  * d2r);

    // R = Rz(yaw)·Ry(pitch)·Rx(roll), row-major. Yaw: +yaw turns head left (+y).
    // Ry uses aeronautic pitch-up-positive about y: x' = x·cp + z·sp form below.
    float R[3][3] = {
        { cy * cp,  cy * sp * sr - sy * cr,  cy * sp * cr + sy * sr },
        { sy * cp,  sy * sp * sr + cy * cr,  sy * sp * cr - cy * sr },
        { -sp,      cp * sr,                 cp * cr                }
    };

    // source' = Rᵀ · source (inverse rotation)
    float xr = R[0][0] * x + R[1][0] * y + R[2][0] * z;
    float yr = R[0][1] * x + R[1][1] * y + R[2][1] * z;
    float zr = R[0][2] * x + R[1][2] * y + R[2][2] * z;

    SpeakerPosition out;
    out.azimuth   = std::atan2(yr, xr) / d2r;
    out.elevation = std::asin(std::clamp(zr, -1.0f, 1.0f)) / d2r;
    out.distance  = pos.distance;
    return out;
}

} // namespace hearGOD
