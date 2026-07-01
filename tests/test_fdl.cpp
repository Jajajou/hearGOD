#include <gtest/gtest.h>
#include "hearGOD/fdl.h"
#include <cmath>
#include <numeric>
#include <vector>

using namespace hearGOD;

// ---- helpers ---------------------------------------------------------------

static std::vector<float> impulse(int len)
{
    std::vector<float> h(len, 0.0f);
    h[0] = 1.0f;
    return h;
}

static std::vector<float> delayedImpulse(int len, int delaySamples)
{
    std::vector<float> h(len, 0.0f);
    if (delaySamples < len) h[delaySamples] = 1.0f;
    return h;
}

// ---------------------------------------------------------------------------

TEST(FDL, ConvolvesImpulseResponseWithImpulseInput)
{
    // HRIR = impulse → output must equal input exactly (after one partition delay)
    const int P = 256;   // partition size
    const int N = 4;     // partitions
    FDL fdl(P, N);

    auto hrir = impulse(P * N);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));
    EXPECT_TRUE(fdl.isLoaded());

    std::vector<float> in(P, 0.0f);
    in[0] = 1.0f;  // unit impulse

    std::vector<float> out(P, 0.0f);
    fdl.process(in.data(), out.data());

    // First partition: OLS adds overlap from previous (none yet) → out[0]==1
    EXPECT_NEAR(out[0], 1.0f, 1e-4f);
    for (int i = 1; i < P; ++i)
        EXPECT_NEAR(out[i], 0.0f, 1e-4f) << "out[" << i << "] non-zero";
}

TEST(FDL, EnergyPreservedAcrossPartitions)
{
    // HRIR energy must equal output energy for a white-noise input block
    const int P = 256;
    const int N = 4;
    FDL fdl(P, N);

    // HRIR = impulse (identity filter)
    auto hrir = impulse(P * N);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));

    // Input: a ramp so it's not all zeros
    std::vector<float> in(P);
    for (int i = 0; i < P; ++i) in[i] = static_cast<float>(i) / P;

    std::vector<float> out(P);
    fdl.process(in.data(), out.data());

    float inEnergy  = 0.0f, outEnergy = 0.0f;
    for (int i = 0; i < P; ++i) {
        inEnergy  += in[i]  * in[i];
        outEnergy += out[i] * out[i];
    }
    EXPECT_NEAR(inEnergy, outEnergy, inEnergy * 1e-3f);
}

TEST(FDL, DelayedImpulseResponseShiftsOutput)
{
    // HRIR = delta at sample D → output = input delayed by D
    const int P = 256;
    const int N = 2;
    const int D = 10;
    FDL fdl(P, N);

    auto hrir = delayedImpulse(P * N, D);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));

    std::vector<float> in(P, 0.0f);
    in[0] = 1.0f;
    std::vector<float> out(P, 0.0f);
    fdl.process(in.data(), out.data());

    EXPECT_NEAR(out[D], 1.0f, 1e-4f);
    for (int i = 0; i < P; ++i) {
        if (i == D) continue;
        EXPECT_NEAR(out[i], 0.0f, 1e-4f) << "out[" << i << "] non-zero";
    }
}

TEST(FDL, ZeroInputProducesZeroOutput)
{
    const int P = 256, N = 4;
    FDL fdl(P, N);
    auto hrir = impulse(P * N);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));

    std::vector<float> in(P, 0.0f);
    std::vector<float> out(P, 0.0f);
    fdl.process(in.data(), out.data());

    for (int i = 0; i < P; ++i)
        EXPECT_NEAR(out[i], 0.0f, 1e-6f);
}

TEST(FDL, IsNotLoadedAfterConstruction)
{
    FDL fdl(256, 4);
    EXPECT_FALSE(fdl.isLoaded());
}

TEST(FDL, MultipleBlocksContinuousStream)
{
    // Feed 4 blocks; check no NaN / Inf creep in
    const int P = 256, N = 4;
    FDL fdl(P, N);
    auto hrir = impulse(P * N);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));

    std::vector<float> in(P), out(P);
    for (int block = 0; block < 4; ++block) {
        for (int i = 0; i < P; ++i) in[i] = (block == 0 && i == 0) ? 1.0f : 0.0f;
        fdl.process(in.data(), out.data());
        for (int i = 0; i < P; ++i)
            EXPECT_TRUE(std::isfinite(out[i])) << "block=" << block << " out[" << i << "] not finite";
    }
}
