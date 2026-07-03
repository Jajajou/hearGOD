#include "hearGOD/sofa_loader.h"
#include <cmath>
#include <iostream>
#include <numbers>

namespace hearGOD {

static constexpr float PI = std::numbers::pi_v<float>;

SOFALoader::~SOFALoader()
{
    if (sofa_) {
        mysofa_close(sofa_);
        sofa_ = nullptr;
    }
}

bool SOFALoader::load(const std::string& sofaPath, int deviceSampleRate)
{
    if (sofa_) {
        mysofa_close(sofa_);
        sofa_ = nullptr;
        loaded_ = false;
    }

    int err = 0;
    sofa_ = mysofa_open(sofaPath.c_str(), static_cast<float>(deviceSampleRate), &irLength_, &err);
    if (!sofa_ || err != MYSOFA_OK) {
        // Retry without normalisation — needed for older SOFA 1.0 files (e.g. LISTEN/IRCAM)
        err = 0;
        sofa_ = mysofa_open_no_norm(sofaPath.c_str(), static_cast<float>(deviceSampleRate), &irLength_, &err);
    }
    if (!sofa_ || err != MYSOFA_OK) {
        std::cerr << "[SOFALoader] mysofa_open failed, err=" << err
                  << ", path=" << sofaPath << "\n";
        sofa_ = nullptr;
        return false;
    }

    sampleRate_ = deviceSampleRate;
    loaded_ = true;

    std::cout << "[SOFALoader] Loaded " << sofa_->hrtf->M
              << " measurements, IR length=" << irLength_
              << ", SR=" << sampleRate_ << "\n";
    return true;
}

void SOFALoader::degToCartesian(float azDeg, float elDeg,
                                 float& x, float& y, float& z)
{
    // SOFA spherical: az=0=front, +az=CCW (left positive), el=0=horizontal, +el=up
    // Convert to Cartesian unit sphere (libmysofa convention)
    float az = azDeg * PI / 180.0f;
    float el = elDeg * PI / 180.0f;
    x =  std::cos(el) * std::cos(az);   // front
    y =  std::cos(el) * std::sin(az);   // left
    z =  std::sin(el);                  // up
}

std::optional<HRIRPair> SOFALoader::getNearestHRIR(float azimuthDeg, float elevationDeg) const
{
    if (!loaded_ || !sofa_) return std::nullopt;

    float x, y, z;
    degToCartesian(azimuthDeg, elevationDeg, x, y, z);

    HRIRPair pair;
    pair.irLength  = irLength_;
    pair.azimuth   = azimuthDeg;
    pair.elevation = elevationDeg;
    pair.left.resize(irLength_);
    pair.right.resize(irLength_);

    mysofa_getfilter_float(sofa_, x, y, z,
                           pair.left.data(), pair.right.data(),
                           &pair.delayL, &pair.delayR);
    // ITD stored as seconds in pair.delayL/R; not baked into IR.
    // Callers apply fractional delay as needed (Phase 2+3).

    return pair;
}

std::optional<HRIRPair> SOFALoader::getHRIRForChannel(Channel ch) const
{
    if (!loaded_) return std::nullopt;

    int idx = static_cast<int>(ch);
    if (idx < 0 || idx >= static_cast<int>(SPEAKER_POSITIONS_7_1_4.size()))
        return std::nullopt;

    const auto& pos = SPEAKER_POSITIONS_7_1_4[idx];
    return getNearestHRIR(pos.azimuth, pos.elevation);
}

std::vector<std::vector<float>> SOFALoader::getAllHRIRs() const
{
    if (!loaded_ || !sofa_) return {};

    int M = sofa_->hrtf->M;  // number of measurements
    int N = irLength_;
    // SOFA data layout: DataIR[M * 2 * N] — M measurements × 2 ears × N samples
    const float* data = sofa_->hrtf->DataIR.values;

    std::vector<std::vector<float>> result;
    result.reserve(M * 2);
    for (int m = 0; m < M; ++m) {
        // Left ear
        result.emplace_back(data + m * 2 * N,       data + m * 2 * N + N);
        // Right ear
        result.emplace_back(data + m * 2 * N + N,   data + m * 2 * N + 2 * N);
    }
    return result;
}

float SOFALoader::greatCircleDistance(float az1, float el1, float az2, float el2) const
{
    auto toRad = [](float d) { return d * PI / 180.0f; };
    float a1 = toRad(az1), e1 = toRad(el1);
    float a2 = toRad(az2), e2 = toRad(el2);

    float daz = a2 - a1;
    float del = e2 - e1;
    float h = std::sin(del / 2) * std::sin(del / 2)
            + std::cos(e1) * std::cos(e2) * std::sin(daz / 2) * std::sin(daz / 2);
    return 2.0f * std::asin(std::sqrt(h));
}

} // namespace hearGOD
