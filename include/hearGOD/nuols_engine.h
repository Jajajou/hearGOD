#pragma once
#include "hearGOD/fdl.h"
#include "hearGOD/types.h"
#include <array>
#include <memory>

namespace hearGOD {

// NUOLS Engine — processes one multichannel buffer → stereo binaural
// One FDL pair (L ear + R ear) per input channel, excluding LFE
class NUOLSEngine {
public:
    explicit NUOLSEngine(int partitionSize = BUFFER_FRAMES,
                         int numPartitions = 4);

    void loadHRIR(Channel ch, const float* hrirL, const float* hrirR, int hrirLength);

    // in:  deinterleaved input, numChannels × BUFFER_FRAMES
    // out: interleaved stereo output, 2 × BUFFER_FRAMES
    void process(const float* const* in, float* out, int numChannels);

    void setLfeGainDb(float db);
    void setMasterGainDb(float db);

    bool isReady() const;

private:
    int partitionSize_;
    int numPartitions_;

    struct ChannelFDLs {
        std::unique_ptr<FDL> left;
        std::unique_ptr<FDL> right;
    };
    std::array<ChannelFDLs, MAX_CHANNELS> fdls_;

    float lfeGainLinear_    = 1.0f;
    float masterGainLinear_ = 1.0f;

    // 4th-order Butterworth LPF for LFE @ 125Hz — 2 cascaded Direct Form II T biquads
    struct Biquad2 {
        float b0, b1, b2, a1, a2;
        float s1 = 0, s2 = 0;  // state (transposed DF-II)
        float tick(float x) {
            float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }
    };
    Biquad2 lfeBq1_, lfeBq2_;

    // 172-sample dry delay to align LFE with HRTF convolution latency
    static constexpr int LFE_DELAY_SAMPLES = 172;
    std::array<float, LFE_DELAY_SAMPLES> lfeDelayBuf_{};
    int lfeDelayWrite_ = 0;

    void initLfeLpf();

    std::vector<float> tempL_, tempR_;
};

} // namespace hearGOD
