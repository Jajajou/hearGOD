#pragma once
#include "hearGOD/nuols_engine.h"
#include "hearGOD/biquad_chain.h"
#include "hearGOD/types.h"
#include <portaudio.h>
#include <memory>
#include <atomic>

namespace hearGOD {

class PortAudioBackend {
public:
    PortAudioBackend(AudioConfig config,
                     std::shared_ptr<NUOLSEngine> engine,
                     std::shared_ptr<BiquadChain> eq);
    ~PortAudioBackend();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // List available devices to stdout
    static void listDevices();

    // Statistics (updated each callback)
    struct Stats {
        std::atomic<int> xrunCount{0};
        std::atomic<double> cpuLoad{0.0};
    };
    const Stats& stats() const { return stats_; }

private:
    AudioConfig config_;
    std::shared_ptr<NUOLSEngine> engine_;
    std::shared_ptr<BiquadChain> eq_;
    PaStream* stream_          = nullptr;
    PaStream* keepAliveStream_ = nullptr;
    std::atomic<bool> running_{false};
    Stats stats_;
    void* callbackUserData_ = nullptr;  // owned, freed in stop()

    bool startKeepAlive();
    void stopKeepAlive();

    static int paCallback(const void* inputBuffer,
                          void* outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData);

    static int keepAliveCallback(const void* inputBuffer,
                                 void* outputBuffer,
                                 unsigned long framesPerBuffer,
                                 const PaStreamCallbackTimeInfo* timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void* userData);

};

} // namespace hearGOD
