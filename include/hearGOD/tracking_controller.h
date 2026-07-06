#pragma once
#include "hearGOD/head_tracker.h"
#include "hearGOD/sofa_loader.h"
#include "hearGOD/nuols_engine.h"
#include "hearGOD/dfe_compensator.h"
#include <memory>

namespace hearGOD {

// Glue between HeadTracker pose and NUOLSEngine HRIR crossfade.
// Call update() from any control thread (GUI timer / CLI loop) — never
// from the audio thread. Rate-limits by angle delta and in-flight fades.
class TrackingController {
public:
    TrackingController(HeadTracker& tracker,
                       SOFALoader& sofa,
                       std::shared_ptr<NUOLSEngine> engine)
        : tracker_(tracker), sofa_(sofa), engine_(std::move(engine)) {}

    // Min pose change (degrees, max over yaw/pitch/roll) before HRIRs are re-fetched.
    void setThresholdDeg(float d) { thresholdDeg_ = d; }

    // Optional: apply DFE compensation to fetched HRIRs (must outlive controller).
    void setDFE(const DFECompensator* dfe) { dfe_ = dfe; }

    // Returns true if a new HRIR set was pushed this call.
    bool update();

    HeadPose appliedPose() const { return applied_; }

private:
    HeadTracker& tracker_;
    SOFALoader& sofa_;
    std::shared_ptr<NUOLSEngine> engine_;
    const DFECompensator* dfe_ = nullptr;
    HeadPose applied_ {};
    bool everApplied_ = false;
    float thresholdDeg_ = 1.5f;
};

} // namespace hearGOD
