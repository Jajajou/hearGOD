#include "LogPanel.h"
#include <ctime>

namespace hearGOD {

static constexpr uint32_t kBg     = 0xFF0D0D0F;
static constexpr uint32_t kBorder = 0xFF2A2A35;
static constexpr uint32_t kText   = 0xFFE4E4E7;
static constexpr uint32_t kMuted  = 0xFF71717A;
static constexpr uint32_t kGreen  = 0xFF22C55E;
static constexpr uint32_t kAmber  = 0xFFF59E0B;
static constexpr uint32_t kRed    = 0xFFEF4444;

static juce::String timestamp()
{
    const auto t = std::time(nullptr);
    const auto* tm = std::localtime(&t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

LogPanel::LogPanel()
{
    clearBtn_.setButtonText("Clear");
    clearBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF1C1C23));
    clearBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kMuted));
    clearBtn_.onClick = [this] { clear(); };
    addAndMakeVisible(clearBtn_);
}

void LogPanel::log(const juce::String& msg, Level level)
{
    const juce::String line = timestamp() + "  " + msg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        entries_.push_back({line, level});
        while ((int)entries_.size() > kMaxEntries)
            entries_.pop_front();
    }
    juce::MessageManager::callAsync([this] { repaint(); });
}

void LogPanel::clear()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        entries_.clear();
    }
    repaint();
}

void LogPanel::resized()
{
    clearBtn_.setBounds(getWidth() - 60, 4, 56, 18);
}

void LogPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());

    std::deque<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot = entries_;
    }

    g.setFont(juce::Font(juce::FontOptions{}.withName("Courier New").withHeight(11.0f)));

    // Draw newest at bottom — scroll to bottom by clamping visible lines
    const int visibleLines = (getHeight() - 8) / kLineH;
    const int start = std::max(0, (int)snapshot.size() - visibleLines);

    float y = 8.0f + std::max(0, visibleLines - (int)snapshot.size()) * (float)kLineH;

    for (int i = start; i < (int)snapshot.size(); ++i) {
        const auto& e = snapshot[i];
        switch (e.level) {
            case Level::Warn:  g.setColour(juce::Colour(kAmber)); break;
            case Level::Error: g.setColour(juce::Colour(kRed));   break;
            default:           g.setColour(juce::Colour(kText));  break;
        }
        g.drawText(e.text, 8, (int)y, getWidth() - 72, kLineH,
                   juce::Justification::centredLeft, false);
        y += (float)kLineH;
    }

    if (snapshot.empty()) {
        g.setColour(juce::Colour(kMuted));
        g.drawText("No log entries yet.", 8, 8, getWidth() - 16, kLineH,
                   juce::Justification::centredLeft);
    }
}

} // namespace hearGOD
