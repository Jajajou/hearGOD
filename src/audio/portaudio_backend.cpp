#include "hearGOD/portaudio_backend.h"
#include <iostream>
#include <cstring>
#include <array>

namespace hearGOD {

struct CallbackUserData {
    NUOLSEngine*  engine;
    BiquadChain*  eq;
    PortAudioBackend::Stats* stats;
    PaStream*     stream = nullptr;  // set after Pa_OpenStream, for CPU load query
    int inputChannels;
    bool swapLR       = false;
    bool stereoEqMode = false;
    std::array<std::array<float, MAX_BUFFER_FRAMES>, MAX_CHANNELS> chBufs;
    std::array<float*, MAX_CHANNELS> chPtrs;
};

// Pa_Initialize reference-counted by PortAudio itself on newer versions,
// but we guard explicitly to handle listDevices() + start() call sequence.
static int sPaRefCount = 0;

static bool paInit()
{
    if (sPaRefCount++ == 0) {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "[PortAudio] Pa_Initialize: " << Pa_GetErrorText(err) << "\n";
            --sPaRefCount;
            return false;
        }
    }
    return true;
}

static void paTerm()
{
    if (--sPaRefCount == 0)
        Pa_Terminate();
}

PortAudioBackend::PortAudioBackend(AudioConfig config,
                                   std::shared_ptr<NUOLSEngine> engine,
                                   std::shared_ptr<BiquadChain> eq)
    : config_(config)
    , engine_(std::move(engine))
    , eq_(std::move(eq))
{}

PortAudioBackend::~PortAudioBackend()
{
    stop();
}

bool PortAudioBackend::start()
{
    if (!paInit()) return false;

    PaStreamParameters inParams{};
    inParams.device = (config_.inputDevice >= 0)
                    ? config_.inputDevice
                    : Pa_GetDefaultInputDevice();
    inParams.channelCount = config_.stereoEqMode ? 2 : config_.inputChannels;
    inParams.sampleFormat = paFloat32 | paNonInterleaved;
    inParams.suggestedLatency = Pa_GetDeviceInfo(inParams.device)->defaultLowInputLatency;

    PaStreamParameters outParams{};
    outParams.device = (config_.outputDevice >= 0)
                     ? config_.outputDevice
                     : Pa_GetDefaultOutputDevice();
    outParams.channelCount = config_.outputChannels;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;

    auto* ud = new CallbackUserData();
    ud->engine = engine_.get();
    ud->eq     = eq_.get();
    ud->stats  = &stats_;
    ud->inputChannels = config_.inputChannels;
    ud->swapLR        = config_.swapLR;
    ud->stereoEqMode  = config_.stereoEqMode;
    for (int i = 0; i < MAX_CHANNELS; ++i)
        ud->chPtrs[i] = ud->chBufs[i].data();

    double deviceSR = Pa_GetDeviceInfo(outParams.device)->defaultSampleRate;
    deviceSampleRate_ = static_cast<int>(deviceSR);
    PaError err = Pa_OpenStream(&stream_,
                                &inParams, &outParams,
                                deviceSR,
                                config_.bufferFrames,
                                paClipOff,
                                paCallback,
                                ud);
    if (err != paNoError) {
        std::cerr << "[PortAudio] Pa_OpenStream: " << Pa_GetErrorText(err) << "\n";
        delete ud;
        paTerm();
        return false;
    }

    // Set stream pointer before Pa_StartStream so callback sees it immediately.
    ud->stream = stream_;

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "[PortAudio] Pa_StartStream: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        delete ud;
        paTerm();
        return false;
    }

    callbackUserData_ = ud;
    running_ = true;

    if (config_.keepAlive)
        startKeepAlive();

    return true;
}

bool PortAudioBackend::startKeepAlive()
{
    PaStreamParameters kaParams{};
    kaParams.device = (config_.outputDevice >= 0)
                    ? config_.outputDevice
                    : Pa_GetDefaultOutputDevice();

    // Don't open second stream on same device as main stream — causes xruns.
    // Main stream already keeps DAC alive via continuous paContinue callbacks.
    PaDeviceIndex mainOut = (config_.outputDevice >= 0)
                          ? config_.outputDevice
                          : Pa_GetDefaultOutputDevice();
    if (kaParams.device == mainOut) {
        std::cout << "[keep-alive] Main stream on same device — DAC already kept awake.\n";
        return true;
    }

    kaParams.channelCount    = 2;
    kaParams.sampleFormat    = paFloat32;
    kaParams.suggestedLatency = Pa_GetDeviceInfo(kaParams.device)->defaultLowOutputLatency;

    PaError err = Pa_OpenStream(&keepAliveStream_,
                                nullptr, &kaParams,
                                static_cast<double>(deviceSampleRate_), BUFFER_FRAMES,
                                paClipOff,
                                keepAliveCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "[PortAudio] keep-alive Pa_OpenStream: " << Pa_GetErrorText(err) << "\n";
        keepAliveStream_ = nullptr;
        return false;
    }
    err = Pa_StartStream(keepAliveStream_);
    if (err != paNoError) {
        std::cerr << "[PortAudio] keep-alive Pa_StartStream: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(keepAliveStream_);
        keepAliveStream_ = nullptr;
        return false;
    }
    std::cout << "[keep-alive] Silent stream started on output device " << kaParams.device << "\n";
    return true;
}

void PortAudioBackend::stopKeepAlive()
{
    if (!keepAliveStream_) return;
    Pa_StopStream(keepAliveStream_);
    Pa_CloseStream(keepAliveStream_);
    keepAliveStream_ = nullptr;
}

void PortAudioBackend::stop()
{
    if (!running_) return;
    running_ = false;

    stopKeepAlive();

    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    delete static_cast<CallbackUserData*>(callbackUserData_);
    callbackUserData_ = nullptr;
    paTerm();
}

void PortAudioBackend::listDevices()
{
    if (!paInit()) return;
    int numDevices = Pa_GetDeviceCount();
    std::cout << "Available audio devices:\n";
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        std::cout << "  [" << i << "] " << info->name
                  << "  in=" << info->maxInputChannels
                  << "  out=" << info->maxOutputChannels << "\n";
    }
    paTerm();
}

int PortAudioBackend::probeDeviceSampleRate(const AudioConfig& cfg)
{
    if (!paInit()) return 48000;
    PaDeviceIndex outDev = cfg.outputDevice >= 0
        ? cfg.outputDevice
        : Pa_GetDefaultOutputDevice();
    int sr = 48000;
    if (outDev != paNoDevice) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(outDev);
        if (info) sr = static_cast<int>(info->defaultSampleRate);
    }
    paTerm();
    return sr;
}

int PortAudioBackend::paCallback(const void* inputBuffer,
                                  void* outputBuffer,
                                  unsigned long framesPerBuffer,
                                  const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                  PaStreamCallbackFlags statusFlags,
                                  void* userData)
{
    auto* ud = static_cast<CallbackUserData*>(userData);

    if (statusFlags & (paInputUnderflow | paInputOverflow |
                       paOutputUnderflow | paOutputOverflow))
        ud->stats->xrunCount.fetch_add(1, std::memory_order_relaxed);

    const float* const* in = static_cast<const float* const*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    if (ud->stereoEqMode) {
        // Stereo passthrough: deinterleave non-interleaved L/R → interleaved output
        const float* inL = in[0];
        const float* inR = in[1];
        for (unsigned long f = 0; f < framesPerBuffer; ++f) {
            out[2 * f]     = inL[f];
            out[2 * f + 1] = inR[f];
        }
    } else {
        ud->engine->process(in, out, ud->inputChannels);
    }

    if (!ud->eq->isEmpty())
        ud->eq->processStereoInterleaved(out, static_cast<int>(framesPerBuffer));

    if (ud->swapLR) {
        for (unsigned long f = 0; f < framesPerBuffer; ++f)
            std::swap(out[2 * f], out[2 * f + 1]);
    }

    // Update CPU load — Pa_GetStreamCpuLoad not available in static callback,
    // so we store stream pointer via a small trampoline
    if (ud->stream)
        ud->stats->cpuLoad.store(Pa_GetStreamCpuLoad(ud->stream),
                                 std::memory_order_relaxed);

    return paContinue;
}


int PortAudioBackend::keepAliveCallback(const void* /*inputBuffer*/,
                                         void* outputBuffer,
                                         unsigned long framesPerBuffer,
                                         const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                         PaStreamCallbackFlags /*statusFlags*/,
                                         void* /*userData*/)
{
    std::memset(outputBuffer, 0, framesPerBuffer * 2 * sizeof(float));
    return paContinue;
}

} // namespace hearGOD
