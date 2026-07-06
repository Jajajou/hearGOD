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
                         int numPartitions = 4,
                         int sampleRate    = 48000);

    void loadHRIR(Channel ch, const float* hrirL, const float* hrirR, int hrirLength);

    // Stage a new HRIR pair for click-free crossfade while the stream runs.
    // Control thread only. Returns false if a previous swap is still fading.
    bool loadHRIRPending(Channel ch, const float* hrirL, const float* hrirR, int hrirLength);

    // True if any channel is still crossfading to a pending HRIR.
    bool isCrossfading() const;

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

    int sampleRate_    = 48000;
    int lfeDelaySamples_ = 172; // computed from sampleRate_ in constructor

    std::vector<float> lfeDelayBuf_;
    int lfeDelayWrite_ = 0;

    void initLfeLpf();

    std::vector<float> tempL_, tempR_;
};

} // namespace hearGOD
