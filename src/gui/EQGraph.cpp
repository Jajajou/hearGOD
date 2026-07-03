#include "EQGraph.h"
#include <cmath>
#include <algorithm>

namespace hearGOD {

EQGraph::EQGraph()
{
    setOpaque(true);
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

void EQGraph::setPreset(const PEQPreset& preset, float sampleRate)
{
    eqPreset_     = preset;
    eqSampleRate_ = sampleRate;
    hasPreset_    = true;
    recomputeMagnitudes(getWidth() > 0 ? getWidth() : 512);
    repaint();
}

void EQGraph::clearPreset()
{
    hasPreset_ = false;
    eqMag_.clear();
    repaint();
}

void EQGraph::setRawFR(std::vector<std::pair<float, float>> points)
{
    std::sort(points.begin(), points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    rawFRRaw_ = std::move(points);
    repaint();
}

void EQGraph::clearRawFR()  { rawFRRaw_.clear();   repaint(); }

void EQGraph::setTargetCurve(std::vector<std::pair<float, float>> points)
{
    std::sort(points.begin(), points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    targetRaw_ = std::move(points);
    repaint();
}

void EQGraph::clearTargetCurve() { targetRaw_.clear(); repaint(); }

void EQGraph::pushWaveformSamples(const float* mono, int n) noexcept
{
    int write = waveWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        waveRing_[write & (kWaveRingSize - 1)] = mono[i];
        ++write;
    }
    waveWrite_.store(write, std::memory_order_release);
    // Clamp available count so GUI reads at most kWaveRingSize samples.
    int avail = waveCount_.load(std::memory_order_relaxed) + n;
    if (avail > kWaveRingSize) avail = kWaveRingSize;
    waveCount_.store(avail, std::memory_order_release);
}

void EQGraph::setNormFreq(float hz)
{
    normFreq_ = hz;
    repaint();
}

void EQGraph::setRawFROffset(float db)
{
    rawFROffset_ = db;
    repaint();
}

// --------------------------------------------------------------------------
// Magnitude computation
// --------------------------------------------------------------------------

static float biquadMagnitudeDb(const BiquadCoeff& c, float freqHz, float sr) noexcept
{
    const float w    = 2.0f * juce::MathConstants<float>::pi * freqHz / sr;
    const float cosw = std::cos(w), cos2w = std::cos(2.0f * w);
    const float sinw = std::sin(w), sin2w = std::sin(2.0f * w);
    const float nR   = c.b0 + c.b1 * cosw + c.b2 * cos2w;
    const float nI   = -(c.b1 * sinw + c.b2 * sin2w);
    const float dR   = 1.0f + c.a1 * cosw + c.a2 * cos2w;
    const float dI   = -(c.a1 * sinw + c.a2 * sin2w);
    const float d    = dR * dR + dI * dI;
    if (d < 1e-12f) return 0.0f;
    return 10.0f * std::log10((nR * nR + nI * nI) / d);
}

void EQGraph::recomputeMagnitudes(int width)
{
    if (!hasPreset_) { eqMag_.clear(); return; }
    eqMag_.resize((size_t)width);
    cachedWidth_ = width;
    const auto coeffs = buildCoeffs(eqPreset_, eqSampleRate_);
    for (int x = 0; x < width; ++x) {
        const float freq = freqForX((float)x, (float)width);
        float db = eqPreset_.preampDb;
        for (const auto& c : coeffs)
            db += biquadMagnitudeDb(c, freq, eqSampleRate_);
        eqMag_[(size_t)x] = db;
    }
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

float EQGraph::freqForX(float x, float width) const noexcept
{
    constexpr float lo = 20.0f, hi = 20000.0f;
    return lo * std::pow(hi / lo, x / (width - 1.0f));
}

float EQGraph::yForDb(float db, float height, float dbRange) const noexcept
{
    return height * 0.5f - (db / dbRange) * (height * 0.5f);
}

float EQGraph::interpolate(
    const std::vector<std::pair<float, float>>& pts, float freq) noexcept
{
    if (pts.empty()) return 0.0f;
    if (freq <= pts.front().first) return pts.front().second;
    if (freq >= pts.back().first)  return pts.back().second;
    auto it = std::lower_bound(pts.begin(), pts.end(), std::make_pair(freq, -1e9f),
                               [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto& hi = *it;
    const auto& lo = *std::prev(it);
    if (hi.first <= lo.first) return lo.second;
    const float t = (std::log(freq) - std::log(lo.first)) /
                    (std::log(hi.first) - std::log(lo.first));
    return lo.second + t * (hi.second - lo.second);
}

std::vector<std::pair<float, float>> EQGraph::normalizedCopy(
    const std::vector<std::pair<float, float>>& pts, float extraOffsetDb) const
{
    if (pts.empty()) return {};
    const float ref = interpolate(pts, normFreq_);
    auto out = pts;
    for (auto& p : out) p.second = p.second - ref + extraOffsetDb;
    return out;
}

juce::Path EQGraph::buildCurvePath(
    const std::vector<std::pair<float, float>>& pts,
    float W, float H, float dbRange) const
{
    juce::Path path;
    bool started = false;
    for (int x = 0; x < (int)W; ++x) {
        const float db = interpolate(pts, freqForX((float)x, W));
        const float y  = yForDb(db, H, dbRange);
        if (!started) { path.startNewSubPath((float)x, y); started = true; }
        else          { path.lineTo((float)x, y); }
    }
    return path;
}

// --------------------------------------------------------------------------
// Paint
// --------------------------------------------------------------------------

void EQGraph::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float H = bounds.getHeight();
    constexpr float kDbRange = 20.0f;

    g.fillAll(juce::Colour(kBg));

    // dB grid
    g.setColour(juce::Colour(kGrid));
    for (float db : { -20.0f, -10.0f, 10.0f, 20.0f })
        g.drawHorizontalLine((int)yForDb(db, H), 0.0f, W);
    for (float freq : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f }) {
        const float t = std::log(freq / 20.0f) / std::log(1000.0f);
        g.drawVerticalLine((int)(t * W), 0.0f, H);
    }

    // 0 dB line (brighter)
    g.setColour(juce::Colour(kZeroDb));
    g.drawHorizontalLine((int)yForDb(0.0f, H), 0.0f, W);

    // Freq labels
    g.setColour(juce::Colour(kText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    for (auto [freq, label] : std::initializer_list<std::pair<float, const char*>>{
            {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}}) {
        const float t = std::log(freq / 20.0f) / std::log(1000.0f);
        g.drawText(label, (int)(t * W - 12), (int)(H - 14), 24, 12,
                   juce::Justification::centred);
    }

    // dB labels (right margin)
    for (float db : { -20.0f, -10.0f, 0.0f, 10.0f, 20.0f }) {
        const float y = yForDb(db, H);
        g.drawText(juce::String((int)db), (int)(W - 36), (int)(y - 6), 28, 12,
                   juce::Justification::centredRight);
    }

    // Norm reference line (subtle vertical at normFreq_)
    {
        const float t  = std::log(normFreq_ / 20.0f) / std::log(1000.0f);
        const float nx = t * W;
        g.setColour(juce::Colour(kText).withAlpha(0.3f));
        g.drawVerticalLine((int)nx, 0.0f, H);
    }

    // ---- Prepare normalized copies ----
    const auto rawNorm    = normalizedCopy(rawFRRaw_, rawFROffset_);
    const auto targetNorm = normalizedCopy(targetRaw_);

    // ---- Target curve (white, behind everything) ----
    if (!targetNorm.empty()) {
        g.setColour(juce::Colour(kTarget).withAlpha(0.55f));
        g.strokePath(buildCurvePath(targetNorm, W, H, kDbRange), juce::PathStrokeType(1.5f));
    }

    // ---- Raw FR (cyan) ----
    if (!rawNorm.empty()) {
        g.setColour(juce::Colour(kRawFR).withAlpha(0.75f));
        g.strokePath(buildCurvePath(rawNorm, W, H, kDbRange), juce::PathStrokeType(1.5f));

        // Raw + EQ result
        if (hasPreset_ && !eqMag_.empty()) {
            juce::Path result;
            bool started = false;
            const int cols = std::min((int)eqMag_.size(), (int)W);
            for (int x = 0; x < cols; ++x) {
                const float db = interpolate(rawNorm, freqForX((float)x, W)) + eqMag_[x];
                const float y  = yForDb(db, H, kDbRange);
                if (!started) { result.startNewSubPath((float)x, y); started = true; }
                else          { result.lineTo((float)x, y); }
            }
            juce::Path fill = result;
            fill.lineTo(W, yForDb(0.0f, H)); fill.lineTo(0.0f, yForDb(0.0f, H));
            fill.closeSubPath();
            g.setColour(juce::Colour(kResult).withAlpha(0.12f));
            g.fillPath(fill);
            g.setColour(juce::Colour(kResult).withAlpha(0.9f));
            g.strokePath(result, juce::PathStrokeType(1.8f));
        }
    } else if (hasPreset_ && !eqMag_.empty()) {
        // No raw FR — plain EQ filter display
        juce::Path curve;
        const int cols = std::min((int)eqMag_.size(), (int)W);
        if (cols > 0) {
            curve.startNewSubPath(0.0f, yForDb(eqMag_[0], H));
            for (int x = 1; x < cols; ++x)
                curve.lineTo((float)x, yForDb(eqMag_[(size_t)x], H));
        }
        juce::Path fill = curve;
        fill.lineTo(W, yForDb(0.0f, H)); fill.lineTo(0.0f, yForDb(0.0f, H));
        fill.closeSubPath();
        g.setColour(juce::Colour(kAccent).withAlpha(0.15f));
        g.fillPath(fill);
        g.setColour(juce::Colour(kAccent));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    } else {
        g.setColour(juce::Colour(kText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        g.drawText("No EQ loaded", bounds.reduced(8.0f), juce::Justification::centred);
    }

    // ---- Legend ----
    {
        int lx = 8, ly = 6;
        const int sw = 14, gap = 8, th = 12, tw = 72;
        auto drawItem = [&](uint32_t col, float alpha, const char* label) {
            g.setColour(juce::Colour(col).withAlpha(alpha));
            g.fillRect((float)lx, (float)(ly + 5), (float)sw, 2.0f);
            g.setColour(juce::Colour(kText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText(label, lx + sw + 4, ly, tw, th, juce::Justification::centredLeft);
            lx += sw + 4 + tw + gap;
        };

        if (!rawFRRaw_.empty()) {
            juce::String rawLabel = "Raw FR";
            if (rawFROffset_ != 0.0f)
                rawLabel += " (" + juce::String(rawFROffset_, 1) + " dB)";
            drawItem(kRawFR, 0.75f, rawLabel.toRawUTF8());
            if (hasPreset_) drawItem(kResult, 0.9f, "Raw + EQ");
        } else if (hasPreset_) {
            drawItem(kAccent, 1.0f, "EQ filter");
        }
        if (!targetRaw_.empty()) drawItem(kTarget, 0.55f, "Target");

        // Norm Hz indicator (top-right)
        g.setColour(juce::Colour(kText).withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        juce::String normStr = "Norm: ";
        normStr += normFreq_ >= 1000.0f
            ? (juce::String(normFreq_ / 1000.0f, 0) + " kHz")
            : (juce::String((int)normFreq_) + " Hz");
        g.drawText(normStr, (int)(W - 80), ly, 72, th, juce::Justification::centredRight);
    }

    // Waveform overlay — post-mix mono signal oscillates around 0dB line.
    const int avail = waveCount_.load(std::memory_order_acquire);
    if (avail >= 2) {
        const int write = waveWrite_.load(std::memory_order_acquire);
        const int nDraw = std::min(avail, (int)W);
        const float zeroY = H * (1.0f - (kDbRange / (2.0f * kDbRange)));
        // 0dB line is at centre of kDbRange
        const float dbCentreY = H * 0.5f; // 0dB at centre
        juce::Path wave;
        bool first = true;
        for (int px = 0; px < nDraw; ++px) {
            const int idx = (write - nDraw + px) & (kWaveRingSize - 1);
            const float sample = waveRing_[idx];
            // Map amplitude [-1,+1] → ±kWaveDbRange dB → pixel
            const float dbOffset = sample * kWaveDbRange;
            const float py = dbCentreY - (dbOffset / kDbRange) * H;
            if (first) { wave.startNewSubPath((float)px, py); first = false; }
            else        { wave.lineTo((float)px, py); }
        }
        g.setColour(juce::Colour(kWave));
        g.strokePath(wave, juce::PathStrokeType(1.0f));
        waveCount_.store(0, std::memory_order_release);
    }
}

} // namespace hearGOD
