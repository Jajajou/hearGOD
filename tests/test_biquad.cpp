#include <gtest/gtest.h>
#include "hearGOD/biquad_chain.h"
#include "hearGOD/types.h"
#include <cmath>
#include <vector>

using namespace hearGOD;

// ---- helpers ---------------------------------------------------------------

// Generate one second of a pure sine, return peak amplitude after filtering
static float sinePeakAfterFilter(BiquadChain& chain, float freqHz, float sampleRate = 48000.0f)
{
    const int N = static_cast<int>(sampleRate);
    std::vector<float> buf(N * 2);  // stereo interleaved

    for (int i = 0; i < N; ++i) {
        float s = std::sin(2.0f * static_cast<float>(M_PI) * freqHz * i / sampleRate);
        buf[2 * i]     = s;
        buf[2 * i + 1] = s;
    }

    // steady state: process in BUFFER_FRAMES blocks
    float peak = 0.0f;
    int offset = 0;
    while (offset + BUFFER_FRAMES <= N) {
        chain.processStereoInterleaved(buf.data() + 2 * offset, BUFFER_FRAMES);
        // measure only last 0.5s (skip transient)
        if (offset > N / 2) {
            for (int i = 0; i < BUFFER_FRAMES; ++i)
                peak = std::max(peak, std::abs(buf[2 * (offset + i)]));
        }
        offset += BUFFER_FRAMES;
    }
    return peak;
}

// ---------------------------------------------------------------------------

TEST(BiquadChain, IsEmptyWithNoFilters)
{
    BiquadChain chain;
    EXPECT_TRUE(chain.isEmpty());
}

TEST(BiquadChain, NotEmptyAfterSetFilters)
{
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::PEAK, 1000.0f, 0.0f, 1.0f, 48000.0f);
    chain.setFilters({c});
    EXPECT_FALSE(chain.isEmpty());
}

TEST(BiquadChain, PassthroughAtUnityGain)
{
    // Peak @ 1kHz with 0 dB gain → unity
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::PEAK, 1000.0f, 0.0f, 1.0f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 1000.0f);
    EXPECT_NEAR(peak, 1.0f, 0.01f);
}

TEST(BiquadChain, LowpassAttenuatesAboveCutoff)
{
    // LPF @ 200 Hz — a 4kHz tone should be well below -20 dB (amplitude < 0.1)
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::LOWPASS, 200.0f, 0.0f, 0.707f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 4000.0f);
    EXPECT_LT(peak, 0.1f);
}

TEST(BiquadChain, LowpassPassesBelowCutoff)
{
    // LPF @ 4000 Hz — a 100 Hz tone should pass (amplitude > 0.9)
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::LOWPASS, 4000.0f, 0.0f, 0.707f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 100.0f);
    EXPECT_GT(peak, 0.9f);
}

TEST(BiquadChain, HighpassAttenuatesBelowCutoff)
{
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::HIGHPASS, 4000.0f, 0.0f, 0.707f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 100.0f);
    EXPECT_LT(peak, 0.1f);
}

TEST(BiquadChain, PeakBoostIncreasesAmplitude)
{
    // +6 dB peak @ 1 kHz → amplitude ≈ 2.0 at that frequency
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::PEAK, 1000.0f, 6.0f, 1.0f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 1000.0f);
    EXPECT_NEAR(peak, 2.0f, 0.1f);
}

TEST(BiquadChain, PeakCutReducesAmplitude)
{
    // -6 dB peak @ 1 kHz → amplitude ≈ 0.5
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::PEAK, 1000.0f, -6.0f, 1.0f, 48000.0f);
    chain.setFilters({c});

    float peak = sinePeakAfterFilter(chain, 1000.0f);
    EXPECT_NEAR(peak, 0.5f, 0.05f);
}

TEST(BiquadChain, MultipleSectionsStack)
{
    // Two +6 dB peaks @ same freq → ≈ +12 dB (amplitude ≈ 4.0)
    BiquadChain chain;
    BiquadCoeff c = makeBiquad(FilterType::PEAK, 1000.0f, 6.0f, 1.0f, 48000.0f);
    chain.setFilters({c, c});

    float peak = sinePeakAfterFilter(chain, 1000.0f);
    EXPECT_NEAR(peak, 4.0f, 0.2f);
}

TEST(BiquadChain, LfeButterworth120HzAttenuation)
{
    // Validate the LFE biquad math used in NUOLSEngine inline:
    // Butterworth LPF 120 Hz / 48kHz — 1 kHz should be deeply attenuated
    // Build it via makeBiquad LOWPASS as a proxy check
    BiquadChain chain;
    BiquadCoeff s1 = makeBiquad(FilterType::LOWPASS, 120.0f, 0.0f, 0.54120f, 48000.0f);
    BiquadCoeff s2 = makeBiquad(FilterType::LOWPASS, 120.0f, 0.0f, 1.30656f, 48000.0f);
    chain.setFilters({s1, s2});

    float peak = sinePeakAfterFilter(chain, 1000.0f);
    EXPECT_LT(peak, 0.01f);  // > 40 dB attenuation at 1 kHz

    // 40 Hz should pass
    BiquadChain chain2;
    chain2.setFilters({s1, s2});
    float peakLow = sinePeakAfterFilter(chain2, 40.0f);
    EXPECT_GT(peakLow, 0.9f);
}
