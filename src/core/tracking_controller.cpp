#include "hearGOD/tracking_controller.h"
#include "hearGOD/head_rotation.h"
#include <cmath>

namespace hearGOD {

bool TrackingController::update()
{
    if (!tracker_.isReceiving() || !sofa_.isLoaded() || !engine_) return false;

    HeadPose pose = tracker_.getPose();

    if (everApplied_) {
        float d = std::max({ std::fabs(pose.yaw   - applied_.yaw),
                             std::fabs(pose.pitch - applied_.pitch),
                             std::fabs(pose.roll  - applied_.roll) });
        if (d < thresholdDeg_) return false;
    }
    if (engine_->isCrossfading()) return false;  // let previous fade finish

    bool pushedAny = false;
    for (int i = 0; i < 12; ++i) {  // 7.1.4 bed+height; LFE skipped below
        auto ch = static_cast<Channel>(i);
        if (ch == Channel::LFE) continue;

        SpeakerPosition rotated = rotateByHead(SPEAKER_POSITIONS_7_1_4[i],
                                               pose.yaw, pose.pitch, pose.roll);
        auto hrir = sofa_.getNearestHRIR(rotated.azimuth, rotated.elevation);
        if (!hrir) continue;

        bool ok;
        if (dfe_ && dfe_->isReady()) {
            auto compL = dfe_->apply(hrir->left.data(),  hrir->irLength);
            auto compR = dfe_->apply(hrir->right.data(), hrir->irLength);
            ok = engine_->loadHRIRPending(ch, compL.data(), compR.data(), hrir->irLength);
        } else {
            ok = engine_->loadHRIRPending(ch, hrir->left.data(), hrir->right.data(),
                                          hrir->irLength);
        }
        if (ok) pushedAny = true;
    }

    if (pushedAny) {
        applied_ = pose;
        everApplied_ = true;
    }
    return pushedAny;
}

} // namespace hearGOD
