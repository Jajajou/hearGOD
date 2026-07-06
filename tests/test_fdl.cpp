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

// ---- crossfade tests ---------------------------------------------------------

// (a) Crossfade to same IR → output matches baseline (MSE < 1e-4 per block)
TEST(FDLCrossfade, SameIROutputMatchesBaseline)
{
    const int P = 256, N = 4, XF = 5;
    auto hrir = impulse(P * N);

    FDL baseline(P, N, XF);
    baseline.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));

    FDL fdl(P, N, XF);
    fdl.loadHRIR(hrir.data(), static_cast<int>(hrir.size()));
    EXPECT_TRUE(fdl.loadHRIRPending(hrir.data(), static_cast<int>(hrir.size())));

    std::vector<float> in(P, 0.0f);
    in[0] = 1.0f;

    for (int b = 0; b < XF + 1; ++b) {
        std::vector<float> outBase(P), outFdl(P);
        std::vector<float> inCopy = in;
        baseline.process(in.data(), outBase.data());
        fdl.process(inCopy.data(), outFdl.data());
        in.assign(P, 0.0f);
        float mse = 0.0f;
        for (int i = 0; i < P; ++i) { float d = outBase[i] - outFdl[i]; mse += d * d; }
        EXPECT_LT(mse / P, 1e-4f) << "block " << b;
    }
    EXPECT_FALSE(fdl.isCrossfading());
}

// (b) Crossfade to different IR → no sample discontinuity (|Δsample| <= 2.0)
TEST(FDLCrossfade, DifferentIRNoContinuityGlitch)
{
    const int P = 256, N = 4, XF = 5;
    FDL fdl(P, N, XF);
    auto hrirA = impulse(P * N);
    auto hrirB = delayedImpulse(P * N, 64);
    fdl.loadHRIR(hrirA.data(), static_cast<int>(hrirA.size()));
    EXPECT_TRUE(fdl.loadHRIRPending(hrirB.data(), static_cast<int>(hrirB.size())));

    std::vector<float> in(P, 0.0f), prev(P, 0.0f);
    in[0] = 1.0f;

    for (int b = 0; b < XF + 2; ++b) {
        std::vector<float> out(P);
        fdl.process(in.data(), out.data());
        in.assign(P, 0.0f);
        if (b > 0) {
            for (int i = 0; i < P; ++i) {
                EXPECT_LE(std::abs(out[i] - prev[i]), 2.0f) << "block " << b << " sample " << i;
                EXPECT_TRUE(std::isfinite(out[i]));
            }
        }
        prev = out;
    }
    EXPECT_FALSE(fdl.isCrossfading());
}

// (c) loadHRIRPending while fade in flight → rejected
TEST(FDLCrossfade, PendingRejectedWhileFading)
{
    const int P = 256, N = 4, XF = 20;
    FDL fdl(P, N, XF);
    auto hrirA = impulse(P * N);
    auto hrirB = delayedImpulse(P * N, 10);
    auto hrirC = delayedImpulse(P * N, 20);

    fdl.loadHRIR(hrirA.data(), static_cast<int>(hrirA.size()));
    EXPECT_TRUE(fdl.loadHRIRPending(hrirB.data(), static_cast<int>(hrirB.size())));

    std::vector<float> in(P, 0.0f), out(P);
    fdl.process(in.data(), out.data()); // triggers Fading state

    EXPECT_TRUE(fdl.isCrossfading());
    EXPECT_FALSE(fdl.loadHRIRPending(hrirC.data(), static_cast<int>(hrirC.size())));
}
