#include <gtest/gtest.h>
#include "hearGOD/peq_parser.h"
#include "hearGOD/biquad_chain.h"
#include <fstream>
#include <filesystem>
#include <cmath>

using namespace hearGOD;

// ---- test fixture: write temp .txt files -----------------------------------

class PEQTest : public ::testing::Test {
protected:
    std::filesystem::path tmpDir_;

    void SetUp() override {
        tmpDir_ = std::filesystem::temp_directory_path() / "hearGOD_test_peq";
        std::filesystem::create_directories(tmpDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmpDir_);
    }

    std::string writePreset(const std::string& name, const std::string& content) {
        auto path = tmpDir_ / name;
        std::ofstream f(path);
        f << content;
        return path.string();
    }
};

// ---------------------------------------------------------------------------

TEST_F(PEQTest, ParsesMissingFileAsNullopt)
{
    auto result = parsePEQ("/nonexistent/path/eq.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(PEQTest, EmptyFileGivesEmptyPreset)
{
    auto path = writePreset("empty.txt", "\n# just a comment\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->preampDb, 0.0f);
    EXPECT_TRUE(result->filters.empty());
}

TEST_F(PEQTest, ParsesPreamp)
{
    auto path = writePreset("preamp.txt", "Preamp: -6.1 dB\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->preampDb, -6.1f, 1e-3f);
}

TEST_F(PEQTest, ParsesPeakFilter)
{
    auto path = writePreset("peak.txt",
        "Filter 1: ON PK Fc 1000 Hz Gain 3.0 dB Q 1.41\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_EQ(result->filters[0].type, FilterType::PEAK);
    EXPECT_NEAR(result->filters[0].freqHz, 1000.0f, 0.1f);
    EXPECT_NEAR(result->filters[0].gainDb, 3.0f,   1e-3f);
    EXPECT_NEAR(result->filters[0].Q,      1.41f,  1e-2f);
}

TEST_F(PEQTest, ParsesLowShelf)
{
    auto path = writePreset("ls.txt",
        "Filter 1: ON LS Fc 105 Hz Gain -3.5 dB Q 0.9\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_EQ(result->filters[0].type, FilterType::LOWSHELF);
    EXPECT_NEAR(result->filters[0].gainDb, -3.5f, 1e-3f);
}

TEST_F(PEQTest, ParsesHighShelf)
{
    auto path = writePreset("hs.txt",
        "Filter 1: ON HS Fc 10000 Hz Gain 2.0 dB Q 0.71\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_EQ(result->filters[0].type, FilterType::HIGHSHELF);
}

TEST_F(PEQTest, ParsesLowpassAndHighpass)
{
    auto path = writePreset("lphp.txt",
        "Filter 1: ON LP Fc 20000 Hz Q 0.707\n"
        "Filter 2: ON HP Fc 20 Hz Q 0.707\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 2u);
    EXPECT_EQ(result->filters[0].type, FilterType::LOWPASS);
    EXPECT_EQ(result->filters[1].type, FilterType::HIGHPASS);
}

TEST_F(PEQTest, SkipsOFFFilters)
{
    auto path = writePreset("off.txt",
        "Filter 1: OFF PK Fc 1000 Hz Gain 3.0 dB Q 1.41\n"
        "Filter 2: ON  PK Fc 2000 Hz Gain 1.0 dB Q 1.0\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_NEAR(result->filters[0].freqHz, 2000.0f, 0.1f);
}

TEST_F(PEQTest, ParsesFullAutoEQPreset)
{
    // Typical AutoEQ export for a popular IEM
    auto path = writePreset("autoeq.txt",
        "# AutoEQ results for Moondrop Aria\n"
        "Preamp: -7.3 dB\n"
        "Filter 1: ON PK  Fc 32    Hz Gain  4.0 dB Q 1.41\n"
        "Filter 2: ON PK  Fc 105   Hz Gain -3.0 dB Q 0.90\n"
        "Filter 3: ON PK  Fc 900   Hz Gain  1.5 dB Q 2.00\n"
        "Filter 4: ON LS  Fc 80    Hz Gain  3.0 dB Q 0.71\n"
        "Filter 5: ON HS  Fc 10000 Hz Gain  2.0 dB Q 0.71\n"
        "Filter 6: OFF PK Fc 1000  Hz Gain  0.0 dB Q 1.00\n");

    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->preampDb, -7.3f, 1e-2f);
    EXPECT_EQ(result->filters.size(), 5u);  // 6 lines - 1 OFF
}

TEST_F(PEQTest, CommentLinesIgnored)
{
    auto path = writePreset("comments.txt",
        "# This is a comment\n"
        "Preamp: -2.0 dB\n"
        "# Another comment\n"
        "Filter 1: ON PK Fc 500 Hz Gain 1.0 dB Q 1.0\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->filters.size(), 1u);
}

TEST_F(PEQTest, BuildCoeffsProducesCorrectCount)
{
    PEQPreset preset;
    preset.preampDb = -3.0f;
    preset.filters = {
        { FilterType::PEAK,     1000.0f, 3.0f,  1.0f },
        { FilterType::LOWSHELF, 100.0f, -2.0f,  0.7f },
        { FilterType::HIGHPASS, 20.0f,   0.0f,  0.707f },
    };
    auto coeffs = buildCoeffs(preset, 48000.0f);
    EXPECT_EQ(coeffs.size(), 3u);
}

TEST_F(PEQTest, BuildCoeffsUnityGainAtPassband)
{
    // Peak +6dB at 1kHz → BiquadChain at 1kHz should give ≈2× amplitude
    PEQPreset preset;
    preset.preampDb = 0.0f;
    preset.filters = {{ FilterType::PEAK, 1000.0f, 6.0f, 1.0f }};

    auto coeffs = buildCoeffs(preset, 48000.0f);
    BiquadChain chain;
    chain.setFilters(coeffs);

    const int SR = 48000;
    const int N  = SR;
    std::vector<float> buf(N * 2);
    for (int i = 0; i < N; ++i) {
        float s = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f * i / SR);
        buf[2 * i] = buf[2 * i + 1] = s;
    }

    float peak = 0.0f;
    for (int off = 0; off + 256 <= N; off += 256) {
        chain.processStereoInterleaved(buf.data() + 2 * off, 256);
        if (off > N / 2)
            for (int i = 0; i < 256; ++i)
                peak = std::max(peak, std::abs(buf[2 * (off + i)]));
    }
    EXPECT_NEAR(peak, 2.0f, 0.1f);
}

TEST_F(PEQTest, CaseInsensitiveFilterType)
{
    // "pk" lowercase
    auto path = writePreset("lower.txt",
        "Filter 1: ON pk Fc 1000 Hz Gain 1.0 dB Q 1.0\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_EQ(result->filters[0].type, FilterType::PEAK);
}

TEST_F(PEQTest, LscAliasMapsToLowShelf)
{
    auto path = writePreset("lsc.txt",
        "Filter 1: ON LSC Fc 200 Hz Gain -2.0 dB Q 0.9\n");
    auto result = parsePEQ(path);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->filters.size(), 1u);
    EXPECT_EQ(result->filters[0].type, FilterType::LOWSHELF);
}
