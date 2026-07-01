#include <gtest/gtest.h>
#include "hearGOD/sofa_loader.h"
#include "hearGOD/types.h"
#include <cmath>

using namespace hearGOD;

// ---- great-circle distance tests (white-box, no SOFA file needed) ----------

// Expose greatCircleDistance via a subclass since it's private
class SOFALoaderTest : public SOFALoader {
public:
    float gcDist(float az1, float el1, float az2, float el2) const {
        return greatCircleDistance(az1, el1, az2, el2);
    }
};

TEST(SOFALoader, GreatCircleSamePointIsZero)
{
    SOFALoaderTest s;
    EXPECT_NEAR(s.gcDist(0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(s.gcDist(30.0f, 15.0f, 30.0f, 15.0f), 0.0f, 1e-6f);
}

TEST(SOFALoader, GreatCircleAntipodalIsPI)
{
    SOFALoaderTest s;
    float d = s.gcDist(0.0f, 90.0f, 0.0f, -90.0f);  // top to bottom
    EXPECT_NEAR(d, static_cast<float>(M_PI), 1e-4f);
}

TEST(SOFALoader, GreatCircleSymmetric)
{
    SOFALoaderTest s;
    float d1 = s.gcDist(30.0f, 0.0f, -30.0f, 0.0f);
    float d2 = s.gcDist(-30.0f, 0.0f, 30.0f, 0.0f);
    EXPECT_NEAR(d1, d2, 1e-6f);
}

TEST(SOFALoader, GreatCircle90DegreesApart)
{
    SOFALoaderTest s;
    // front (0°) to left (90°) on horizontal plane = π/2
    float d = s.gcDist(0.0f, 0.0f, 90.0f, 0.0f);
    EXPECT_NEAR(d, static_cast<float>(M_PI) / 2.0f, 1e-4f);
}

// ---- SPEAKER_POSITIONS_7_1 sanity checks -----------------------------------

TEST(SpeakerPositions, FLAndFRSymmetric)
{
    const auto& fl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::FL)];
    const auto& fr = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::FR)];
    EXPECT_NEAR(fl.azimuth, -fr.azimuth, 1e-3f);
    EXPECT_NEAR(fl.elevation, fr.elevation, 1e-3f);
}

TEST(SpeakerPositions, SLAndSRSymmetric)
{
    const auto& sl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::SL)];
    const auto& sr = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::SR)];
    EXPECT_NEAR(sl.azimuth, -sr.azimuth, 1e-3f);
    EXPECT_NEAR(sl.elevation, sr.elevation, 1e-3f);
}

TEST(SpeakerPositions, BLAndBRSymmetric)
{
    const auto& bl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::BL)];
    const auto& br = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::BR)];
    EXPECT_NEAR(bl.azimuth, -br.azimuth, 1e-3f);
    EXPECT_NEAR(bl.elevation, br.elevation, 1e-3f);
}

TEST(SpeakerPositions, FCIsOnAxis)
{
    const auto& fc = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::FC)];
    EXPECT_NEAR(fc.azimuth, 0.0f, 1e-3f);
    EXPECT_NEAR(fc.elevation, 0.0f, 1e-3f);
}

TEST(SpeakerPositions, FLAzimuthIs30Degrees)
{
    const auto& fl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::FL)];
    EXPECT_NEAR(fl.azimuth, 30.0f, 1e-3f);
}

TEST(SpeakerPositions, SLAzimuthIs90Degrees)
{
    const auto& sl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::SL)];
    EXPECT_NEAR(sl.azimuth, 90.0f, 1e-3f);
}

TEST(SpeakerPositions, BLAzimuthIs135Degrees)
{
    const auto& bl = SPEAKER_POSITIONS_7_1[static_cast<int>(Channel::BL)];
    EXPECT_NEAR(bl.azimuth, 135.0f, 1e-3f);
}

TEST(SpeakerPositions, AllDistancesAreOne)
{
    for (int i = 0; i < static_cast<int>(SPEAKER_POSITIONS_7_1.size()); ++i)
        EXPECT_NEAR(SPEAKER_POSITIONS_7_1[i].distance, 1.0f, 1e-6f);
}

// ---- Height channel positions (7.1.4 Atmos) --------------------------------

TEST(SpeakerPositions, TFLAndTFRSymmetric)
{
    const auto& tfl = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TFL)];
    const auto& tfr = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TFR)];
    EXPECT_NEAR(tfl.azimuth, -tfr.azimuth, 1e-3f);
    EXPECT_NEAR(tfl.elevation, tfr.elevation, 1e-3f);
}

TEST(SpeakerPositions, TBLAndTBRSymmetric)
{
    const auto& tbl = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TBL)];
    const auto& tbr = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TBR)];
    EXPECT_NEAR(tbl.azimuth, -tbr.azimuth, 1e-3f);
    EXPECT_NEAR(tbl.elevation, tbr.elevation, 1e-3f);
}

TEST(SpeakerPositions, HeightChannelsElevation45Degrees)
{
    for (Channel ch : {Channel::TFL, Channel::TFR, Channel::TBL, Channel::TBR}) {
        const auto& pos = SPEAKER_POSITIONS_7_1_4[static_cast<int>(ch)];
        EXPECT_NEAR(pos.elevation, 45.0f, 1e-3f)
            << "Expected 45° elevation for " << channelName(ch);
    }
}

TEST(SpeakerPositions, BedChannelsElevationZero)
{
    for (Channel ch : {Channel::FL, Channel::FR, Channel::FC,
                       Channel::BL, Channel::BR, Channel::SL, Channel::SR}) {
        const auto& pos = SPEAKER_POSITIONS_7_1_4[static_cast<int>(ch)];
        EXPECT_NEAR(pos.elevation, 0.0f, 1e-3f)
            << "Expected 0° elevation for " << channelName(ch);
    }
}

TEST(SpeakerPositions, TFLAzimuth45Degrees)
{
    const auto& tfl = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TFL)];
    EXPECT_NEAR(tfl.azimuth, 45.0f, 1e-3f);
}

TEST(SpeakerPositions, TBLAzimuth135Degrees)
{
    const auto& tbl = SPEAKER_POSITIONS_7_1_4[static_cast<int>(Channel::TBL)];
    EXPECT_NEAR(tbl.azimuth, 135.0f, 1e-3f);
}

TEST(SpeakerPositions, AllPositions7_1_4DistanceIsOne)
{
    for (int i = 0; i < static_cast<int>(SPEAKER_POSITIONS_7_1_4.size()); ++i)
        EXPECT_NEAR(SPEAKER_POSITIONS_7_1_4[i].distance, 1.0f, 1e-6f);
}

TEST(ChannelName, AllChannelsHaveNames)
{
    EXPECT_STREQ(channelName(Channel::FL),  "FL");
    EXPECT_STREQ(channelName(Channel::FR),  "FR");
    EXPECT_STREQ(channelName(Channel::FC),  "FC");
    EXPECT_STREQ(channelName(Channel::LFE), "LFE");
    EXPECT_STREQ(channelName(Channel::BL),  "BL");
    EXPECT_STREQ(channelName(Channel::BR),  "BR");
    EXPECT_STREQ(channelName(Channel::SL),  "SL");
    EXPECT_STREQ(channelName(Channel::SR),  "SR");
    EXPECT_STREQ(channelName(Channel::TFL), "TFL");
    EXPECT_STREQ(channelName(Channel::TFR), "TFR");
    EXPECT_STREQ(channelName(Channel::TBL), "TBL");
    EXPECT_STREQ(channelName(Channel::TBR), "TBR");
    EXPECT_STREQ(channelName(Channel::INVALID), "INVALID");
}

// ---- SOFALoader unloaded state ---------------------------------------------

TEST(SOFALoader, GetHRIRForChannelFailsWhenNotLoaded)
{
    SOFALoader loader;
    auto result = loader.getHRIRForChannel(Channel::FL);
    EXPECT_FALSE(result.has_value());
}

TEST(SOFALoader, GetNearestHRIRFailsWhenNotLoaded)
{
    SOFALoader loader;
    auto result = loader.getNearestHRIR(0.0f, 0.0f);
    EXPECT_FALSE(result.has_value());
}
