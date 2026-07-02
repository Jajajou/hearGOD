#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include <mutex>

namespace hearGOD {

class LogPanel : public juce::Component
{
public:
    enum class Level { Info, Warn, Error };

    LogPanel();

    void log(const juce::String& msg, Level level = Level::Info);
    void clear();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Entry {
        juce::String text;
        Level        level;
    };

    std::deque<Entry> entries_;
    std::mutex        mutex_;

    juce::TextButton  clearBtn_;

    static constexpr int kMaxEntries = 500;
    static constexpr int kLineH      = 16;
};

} // namespace hearGOD
