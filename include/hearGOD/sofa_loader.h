#pragma once
#include "hearGOD/types.h"
#include <string>
#include <vector>
#include <optional>
#include <mysofa.h>

namespace hearGOD {

struct HRIRPair {
    std::vector<float> left;   // time-domain, length = irLength
    std::vector<float> right;
    int irLength = 0;
    float azimuth   = 0.0f;  // degrees (target position used to query)
    float elevation = 0.0f;
};

class SOFALoader {
public:
    SOFALoader() = default;
    ~SOFALoader();

    SOFALoader(const SOFALoader&) = delete;
    SOFALoader& operator=(const SOFALoader&) = delete;

    // Load .sofa file. Returns false + logs error on failure.
    bool load(const std::string& sofaPath);

    // Get HRIR nearest to target spherical position (degrees).
    // Uses mysofa_getfilter_float (Cartesian internally) — correct for any SOFA dataset.
    std::optional<HRIRPair> getNearestHRIR(float azimuthDeg, float elevationDeg) const;

    // Get HRIR for a 7.1.4 channel using standard speaker positions.
    // Returns nullopt if SOFA not loaded.
    std::optional<HRIRPair> getHRIRForChannel(Channel ch) const;

    int sampleRate() const { return sampleRate_; }
    int irLength()   const { return irLength_; }
    bool isLoaded()  const { return loaded_; }

    // Return all L+R IRs from every measurement — used by DFECompensator.
    // Each element is one IR (length = irLength_). L and R are separate entries.
    std::vector<std::vector<float>> getAllHRIRs() const;

protected:
    // Exposed for unit tests only — white-box great-circle distance math
    float greatCircleDistance(float az1, float el1, float az2, float el2) const;

private:
    MYSOFA_EASY* sofa_ = nullptr;
    int sampleRate_ = 0;
    int irLength_   = 0;
    bool loaded_    = false;

    // sph(degrees) → SOFA Cartesian unit sphere (libmysofa convention)
    static void degToCartesian(float azDeg, float elDeg,
                                float& x, float& y, float& z);
};

} // namespace hearGOD
