// Integration tests — full signal chain: NUOLS + BiquadChain
// Uses synthetic HRIR (Dirac delta impulse) — no .sofa file required.
// Verifies: binaural output, LFE bypass, EQ application, gain/latency.

#include <gtest/gtest.h>
#include "hearGOD/nuols_engine.h"
#include "hearGOD/biquad_chain.h"
#include "hearGOD/peq_parser.h"
#include "hearGOD/types.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace hearGOD;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<float> diracHRIR(int length)
{
    std::vector<float> h(length, 0.0f);
    h[0] = 1.0f;
    return h;
}

// Dirac with gain g at position pos
static std::vector<float> scaledDirac(int length, int pos, float g)
{
    std::vector<float> h(length, 0.0f);
    if (pos < length) h[pos] = g;
    return h;
}

// Deinterleave stereo output → L / R
static void splitLR(const float* interleaved, int frames,
                    std::vector<float>& L, std::vector<float>& R)
{
    L.resize(frames);
    R.resize(frames);
    for (int i = 0; i < frames; ++i) {
        L[i] = interleaved[2 * i];
        R[i] = interleaved[2 * i + 1];
    }
}

static float maxAbs(const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::abs(x));
    return m;
}

// Write a temp PEQ preset file, return path
static std::filesystem::path writePEQ(const std::string& name,
                                       const std::string& content)
{
    auto tmp = std::filesystem::temp_directory_path() / ("hearGOD_int_" + name);
    std::filesystem::create_directories(tmp);
    auto f = tmp / "preset.txt";
    std::ofstream(f) << content;
    return f;
}

// Build engine loaded with Dirac HRIRs for FL, FR, FC, BL, BR, SL, SR
static std::shared_ptr<NUOLSEngine> makeEngine(int partitions = 4)
{
    auto eng = std::make_shared<NUOLSEngine>(BUFFER_FRAMES, partitions);
    auto h = diracHRIR(BUFFER_FRAMES);

    // All channels share identity HRIR — convolution = passthrough
    for (int ch = 0; ch < 8; ++ch) {
        Channel c = static_cast<Channel>(ch);
        if (c == Channel::LFE) continue;
        eng->loadHRIR(c, h.data(), h.data(), static_cast<int>(h.size()));
    }
    return eng;
}

// ---------------------------------------------------------------------------
// Signal chain fixture
// ---------------------------------------------------------------------------

class IntegrationTest : public ::testing::Test {
protected:
    std::array<std::array<float, BUFFER_FRAMES>, MAX_CHANNELS> chBufs{};
    std::array<float*, MAX_CHANNELS> chPtrs{};
    std::vector<float> outBuf;

    void SetUp() override
    {
        for (int i = 0; i < MAX_CHANNELS; ++i) {
            chBufs[i].fill(0.0f);
            chPtrs[i] = chBufs[i].data();
        }
        outBuf.assign(2 * BUFFER_FRAMES, 0.0f);
    }

    // Inject constant value into one input channel
    void fillChannel(int idx, float val)
    {
        chBufs[idx].fill(val);
    }

    // Inject impulse at frame 0 into one input channel
    void impulseChannel(int idx)
    {
        chBufs[idx].fill(0.0f);
        chBufs[idx][0] = 1.0f;
    }
};

// ---------------------------------------------------------------------------
// 1. Engine readiness
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, EngineNotReadyWithoutHRIR)
{
    NUOLSEngine eng(BUFFER_FRAMES, 4);
    EXPECT_FALSE(eng.isReady());
}

TEST_F(IntegrationTest, EngineReadyAfterFLFRLoaded)
{
    auto h = diracHRIR(BUFFER_FRAMES);
    NUOLSEngine eng(BUFFER_FRAMES, 4);
    eng.loadHRIR(Channel::FL, h.data(), h.data(), BUFFER_FRAMES);
    EXPECT_FALSE(eng.isReady());  // still need FR
    eng.loadHRIR(Channel::FR, h.data(), h.data(), BUFFER_FRAMES);
    EXPECT_TRUE(eng.isReady());
}

// ---------------------------------------------------------------------------
// 2. Silent input → silent output
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, SilentInputProducesSilentOutput)
{
    auto eng = makeEngine();
    eng->process(chPtrs.data(), outBuf.data(), 8);
    for (float s : outBuf)
        EXPECT_NEAR(s, 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// 3. FL signal → binaural output (Dirac HRIR = passthrough)
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, FLSignalAppearsBothEars)
{
    auto eng = makeEngine();
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    // With Dirac HRIR both ears get signal
    EXPECT_GT(maxAbs(L), 0.5f);
    EXPECT_GT(maxAbs(R), 0.5f);
}

// ---------------------------------------------------------------------------
// 4. Channel with no HRIR → silent (ignored in process)
//    Load only FL+FR, send signal on FC → FC silent
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, ChannelWithoutHRIRIsSkipped)
{
    auto h = diracHRIR(BUFFER_FRAMES);
    NUOLSEngine eng(BUFFER_FRAMES, 4);
    eng.loadHRIR(Channel::FL, h.data(), h.data(), BUFFER_FRAMES);
    eng.loadHRIR(Channel::FR, h.data(), h.data(), BUFFER_FRAMES);
    ASSERT_TRUE(eng.isReady());

    fillChannel(static_cast<int>(Channel::FC), 1.0f);
    // FL+FR are silent inputs
    eng.process(chPtrs.data(), outBuf.data(), 8);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);
    EXPECT_NEAR(maxAbs(L), 0.0f, 1e-6f);
    EXPECT_NEAR(maxAbs(R), 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// 5. LFE bypass — signal goes through LPF then appears on L+R
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, LFEBypassAppearsOnBothOutputs)
{
    auto eng = makeEngine();
    int lfeIdx = static_cast<int>(Channel::LFE);
    int totalSamples = 0;

    // Warm up: run enough buffers to flush the 172-sample delay + LPF transient.
    // Use 20Hz sine throughout so delay output also carries signal.
    auto fillLFE = [&](int startSample) {
        for (int i = 0; i < BUFFER_FRAMES; ++i)
            chBufs[lfeIdx][i] = std::sin(2.0f * 3.14159265f * 20.0f *
                                          (startSample + i) / SAMPLE_RATE);
    };

    for (int buf = 0; buf < 4; ++buf) {
        fillLFE(totalSamples);
        eng->process(chPtrs.data(), outBuf.data(), 8);
        totalSamples += BUFFER_FRAMES;
    }

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    // 20Hz passes 125Hz LPF + delay is flushed → expect signal on both channels
    EXPECT_GT(maxAbs(L), 0.1f);
    EXPECT_GT(maxAbs(R), 0.1f);
}

TEST_F(IntegrationTest, LFEHighFreqAttenuated)
{
    // 2kHz sinewave through 120Hz 4th-order Butterworth LPF.
    // Run several buffers to reach steady-state (avoid transient startup ringing).
    auto eng = makeEngine();

    auto fillLFE2k = [&](int startSample) {
        for (int i = 0; i < BUFFER_FRAMES; ++i)
            chBufs[static_cast<int>(Channel::LFE)][i] =
                std::sin(2.0f * 3.14159265f * 2000.0f * (startSample + i)
                         / static_cast<float>(SAMPLE_RATE));
    };

    // Warm up 8 buffers so LPF biquad state reaches steady-state
    for (int b = 0; b < 8; ++b) {
        fillLFE2k(b * BUFFER_FRAMES);
        eng->process(chPtrs.data(), outBuf.data(), 8);
    }

    // Measure on 9th buffer
    fillLFE2k(8 * BUFFER_FRAMES);
    outBuf.assign(2 * BUFFER_FRAMES, 0.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    // 2kHz >> 120Hz cutoff: steady-state attenuation should be < 1% amplitude
    EXPECT_LT(maxAbs(L), 0.01f);
    EXPECT_LT(maxAbs(R), 0.01f);
}

// ---------------------------------------------------------------------------
// 6. Master gain
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, MasterGainScalesOutput)
{
    auto eng = makeEngine();
    fillChannel(static_cast<int>(Channel::FL), 1.0f);

    eng->setMasterGainDb(0.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);
    float baseMax = maxAbs(outBuf);

    eng->setMasterGainDb(-6.0f);  // ~0.5× linear
    outBuf.assign(2 * BUFFER_FRAMES, 0.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);
    float attMax = maxAbs(outBuf);

    EXPECT_NEAR(attMax, baseMax * std::pow(10.0f, -6.0f / 20.0f), baseMax * 0.01f);
}

TEST_F(IntegrationTest, MasterGainPlusDbAmplifies)
{
    auto eng = makeEngine();
    fillChannel(static_cast<int>(Channel::FL), 0.5f);

    eng->setMasterGainDb(0.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);
    float base = maxAbs(outBuf);

    eng->setMasterGainDb(6.0f);  // ~2×
    outBuf.assign(2 * BUFFER_FRAMES, 0.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);
    float boosted = maxAbs(outBuf);

    EXPECT_NEAR(boosted, base * std::pow(10.0f, 6.0f / 20.0f), base * 0.02f);
}

// ---------------------------------------------------------------------------
// 7. Asymmetric HRIR — left ear ≠ right ear
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, AsymmetricHRIRDifferentEarLevels)
{
    // FL HRIR: L gain=1.0, R gain=0.5 at sample 0
    auto hL = scaledDirac(BUFFER_FRAMES, 0, 1.0f);
    auto hR = scaledDirac(BUFFER_FRAMES, 0, 0.5f);

    NUOLSEngine eng(BUFFER_FRAMES, 4);
    eng.loadHRIR(Channel::FL, hL.data(), hR.data(), BUFFER_FRAMES);
    // FR needed for isReady but silent input → no contribution
    auto hId = diracHRIR(BUFFER_FRAMES);
    eng.loadHRIR(Channel::FR, hId.data(), hId.data(), BUFFER_FRAMES);

    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng.process(chPtrs.data(), outBuf.data(), 8);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    EXPECT_NEAR(maxAbs(L), 1.0f, 0.01f);
    EXPECT_NEAR(maxAbs(R), 0.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// 8. All 7 bed channels sum correctly
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, AllBedChannelsSumInBinaural)
{
    auto eng = makeEngine();
    // Send 1.0 on all non-LFE channels
    for (int ch = 0; ch < 8; ++ch) {
        if (static_cast<Channel>(ch) != Channel::LFE)
            fillChannel(ch, 1.0f);
    }
    eng->process(chPtrs.data(), outBuf.data(), 8);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    // 7 channels × gain 1.0 with Dirac HRIR — large output expected
    EXPECT_GT(maxAbs(L), 1.0f);
    EXPECT_GT(maxAbs(R), 1.0f);
}

// ---------------------------------------------------------------------------
// 9. EQ pipeline — BiquadChain applied after NUOLS
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, EQChainEmptyIsNoOp)
{
    auto eng = makeEngine();
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);

    auto outWithEQ = outBuf;  // copy

    BiquadChain eq;
    EXPECT_TRUE(eq.isEmpty());
    // Should not change output when empty
    eq.processStereoInterleaved(outWithEQ.data(), BUFFER_FRAMES);

    for (int i = 0; i < 2 * BUFFER_FRAMES; ++i)
        EXPECT_NEAR(outWithEQ[i], outBuf[i], 1e-6f);
}

TEST_F(IntegrationTest, EQPreampScalesOutput)
{
    // Parse PEQ with -6dB preamp, no filters
    auto path = writePEQ("preamp", "Preamp: -6 dB\n");
    auto preset = parsePEQ(path.string(), static_cast<float>(SAMPLE_RATE));
    ASSERT_TRUE(preset.has_value());
    EXPECT_NEAR(preset->preampDb, -6.0f, 0.01f);

    auto eng = makeEngine();
    eng->setMasterGainDb(preset->preampDb);  // bake preamp into gain
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);

    // Compare to 0dB
    auto eng0 = makeEngine();
    std::vector<float> outBase(2 * BUFFER_FRAMES, 0.0f);
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng0->process(chPtrs.data(), outBase.data(), 8);

    float ratio = maxAbs(outBuf) / maxAbs(outBase);
    EXPECT_NEAR(ratio, std::pow(10.0f, -6.0f / 20.0f), 0.01f);
}

TEST_F(IntegrationTest, EQPeakFilterModifiesFrequencyContent)
{
    // +6dB peak at 1kHz — verify broadband signal is louder than without EQ
    auto path = writePEQ("peak", "Filter 1: ON PK Fc 1000 Hz Gain 6.0 dB Q 1.0\n");
    auto preset = parsePEQ(path.string(), static_cast<float>(SAMPLE_RATE));
    ASSERT_TRUE(preset.has_value());
    ASSERT_EQ(preset->filters.size(), 1u);

    auto coeffs = buildCoeffs(*preset, static_cast<float>(SAMPLE_RATE));

    // Broadband signal: all channels at 1.0
    auto eng = makeEngine();
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);
    float preEQ = maxAbs(outBuf);

    BiquadChain eq;
    eq.setFilters(coeffs);
    eq.processStereoInterleaved(outBuf.data(), BUFFER_FRAMES);
    float postEQ = maxAbs(outBuf);

    // +6dB boost should increase peak amplitude
    EXPECT_GT(postEQ, preEQ);
}

// ---------------------------------------------------------------------------
// 10. Latency — OLS convolution: impulse appears in same buffer (zero extra delay)
//     The FDL uses overlap-save: output[n] = input[n] * h[n] with OLS overlap.
//     With Dirac HRIR (h[0]=1), convolution = identity.
//     Overlap tail drains in the buffer following the last non-zero input.
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, ImpulseResponseAppearsInSameBuffer)
{
    // Dirac HRIR → convolution is identity (no algorithmic delay added)
    NUOLSEngine eng(BUFFER_FRAMES, 1);
    auto h = diracHRIR(BUFFER_FRAMES);
    eng.loadHRIR(Channel::FL, h.data(), h.data(), BUFFER_FRAMES);
    eng.loadHRIR(Channel::FR, h.data(), h.data(), BUFFER_FRAMES);

    impulseChannel(static_cast<int>(Channel::FL));
    eng.process(chPtrs.data(), outBuf.data(), 8);

    // With identity HRIR, impulse input → impulse in output at same buffer
    EXPECT_GT(maxAbs(outBuf), 0.5f);
}

TEST_F(IntegrationTest, OLSOverlapTailDrainsAfterSilentBuffer)
{
    // After a block of signal, the OLS overlap carries into the next buffer.
    // Two silent buffers past a Dirac HRIR should fully drain the tail.
    NUOLSEngine eng(BUFFER_FRAMES, 1);
    auto h = diracHRIR(BUFFER_FRAMES);
    eng.loadHRIR(Channel::FL, h.data(), h.data(), BUFFER_FRAMES);
    eng.loadHRIR(Channel::FR, h.data(), h.data(), BUFFER_FRAMES);

    // Signal buffer
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng.process(chPtrs.data(), outBuf.data(), 8);

    // Silent buffer 1 — drains OLS overlap
    for (auto& buf : chBufs) buf.fill(0.0f);
    std::vector<float> out2(2 * BUFFER_FRAMES, 0.0f);
    eng.process(chPtrs.data(), out2.data(), 8);

    // Silent buffer 2 — must be fully zero (Dirac HRIR = 1 sample, zero overlap)
    std::vector<float> out3(2 * BUFFER_FRAMES, 0.0f);
    eng.process(chPtrs.data(), out3.data(), 8);

    EXPECT_NEAR(maxAbs(out3), 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// 11. Consecutive buffers — no state corruption between calls
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, ConsecutiveBuffersNoCrossContamination)
{
    auto eng = makeEngine();

    // Buffer 0: signal on FL
    fillChannel(static_cast<int>(Channel::FL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 8);

    // Buffer 1: all silence
    for (auto& buf : chBufs) buf.fill(0.0f);
    std::vector<float> out2(2 * BUFFER_FRAMES, 0.0f);
    eng->process(chPtrs.data(), out2.data(), 8);

    // Buffer 2: all silence — FDL tail should have decayed (Dirac = single sample)
    std::vector<float> out3(2 * BUFFER_FRAMES, 0.0f);
    eng->process(chPtrs.data(), out3.data(), 8);

    // After 2 silent buffers past a Dirac HRIR, output should be zero
    EXPECT_NEAR(maxAbs(out3), 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// 12. LFE gain knob
// ---------------------------------------------------------------------------

TEST_F(IntegrationTest, LFEGainKnobScalesLFE)
{
    // Measure each gain level with a fresh engine so biquad state doesn't carry over.
    // Warm up several buffers to reach LPF steady-state, then sample.

    auto measureLFE = [&](float gainDb) -> float {
        auto eng = makeEngine();
        eng->setLfeGainDb(gainDb);

        auto fillLFE = [&](int startSample) {
            for (int i = 0; i < BUFFER_FRAMES; ++i)
                chBufs[static_cast<int>(Channel::LFE)][i] =
                    std::sin(2.0f * 3.14159265f * 40.0f * (startSample + i)
                             / static_cast<float>(SAMPLE_RATE));
        };

        std::vector<float> out(2 * BUFFER_FRAMES);
        for (int b = 0; b < 8; ++b) {
            fillLFE(b * BUFFER_FRAMES);
            out.assign(2 * BUFFER_FRAMES, 0.0f);
            eng->process(chPtrs.data(), out.data(), 8);
        }
        // Sample at 9th buffer (steady-state)
        fillLFE(8 * BUFFER_FRAMES);
        out.assign(2 * BUFFER_FRAMES, 0.0f);
        eng->process(chPtrs.data(), out.data(), 8);
        return maxAbs(out);
    };

    float base = measureLFE(0.0f);
    float att  = measureLFE(-6.0f);

    ASSERT_GT(base, 1e-4f);  // sanity: signal actually passed

    float ratio = att / base;
    float expected = std::pow(10.0f, -6.0f / 20.0f);  // ≈ 0.5012
    EXPECT_NEAR(ratio, expected, 0.02f);
}

// ---------------------------------------------------------------------------
// 13. Height channels (7.1.4) — TFL/TFR/TBL/TBR
// ---------------------------------------------------------------------------

static std::shared_ptr<NUOLSEngine> makeEngine714()
{
    auto eng = std::make_shared<NUOLSEngine>(BUFFER_FRAMES, 4);
    auto h = diracHRIR(BUFFER_FRAMES);
    for (int ch = 0; ch < 12; ++ch) {
        Channel c = static_cast<Channel>(ch);
        if (c == Channel::LFE) continue;
        eng->loadHRIR(c, h.data(), h.data(), static_cast<int>(h.size()));
    }
    return eng;
}

TEST_F(IntegrationTest, TFLSignalAppearsBothEars)
{
    auto eng = makeEngine714();
    fillChannel(static_cast<int>(Channel::TFL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 12);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    EXPECT_GT(maxAbs(L), 0.5f);
    EXPECT_GT(maxAbs(R), 0.5f);
}

TEST_F(IntegrationTest, TFRSignalAppearsBothEars)
{
    auto eng = makeEngine714();
    fillChannel(static_cast<int>(Channel::TFR), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 12);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    EXPECT_GT(maxAbs(L), 0.5f);
    EXPECT_GT(maxAbs(R), 0.5f);
}

TEST_F(IntegrationTest, AllHeightChannelsSumWithBed)
{
    auto eng = makeEngine714();
    // Send 1.0 on all 11 non-LFE channels (7 bed + 4 height)
    for (int ch = 0; ch < 12; ++ch) {
        if (static_cast<Channel>(ch) != Channel::LFE)
            fillChannel(ch, 1.0f);
    }
    eng->process(chPtrs.data(), outBuf.data(), 12);

    std::vector<float> L, R;
    splitLR(outBuf.data(), BUFFER_FRAMES, L, R);

    // 11 channels × 1.0 Dirac HRIR — output should be 11× single channel
    auto engOne = makeEngine714();
    std::array<std::array<float, BUFFER_FRAMES>, MAX_CHANNELS> singleCh{};
    std::array<float*, MAX_CHANNELS> singlePtrs{};
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        singleCh[i].fill(0.0f);
        singlePtrs[i] = singleCh[i].data();
    }
    singleCh[static_cast<int>(Channel::FL)].fill(1.0f);
    std::vector<float> outOne(2 * BUFFER_FRAMES, 0.0f);
    engOne->process(singlePtrs.data(), outOne.data(), 12);

    float singlePeak = maxAbs(outOne);
    EXPECT_NEAR(maxAbs(L), singlePeak * 11.0f, singlePeak * 0.5f);
}

TEST_F(IntegrationTest, HeightChannelWithoutHRIRIgnored)
{
    // Only bed channels loaded (makeEngine), send height → should be silent
    auto eng = makeEngine();  // only FL-SR loaded
    fillChannel(static_cast<int>(Channel::TFL), 1.0f);
    eng->process(chPtrs.data(), outBuf.data(), 12);

    // TFL has no HRIR → skipped, bed channels silent input → silent output
    EXPECT_NEAR(maxAbs(outBuf), 0.0f, 1e-6f);
}
