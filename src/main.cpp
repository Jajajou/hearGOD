#include "hearGOD/types.h"
#include "hearGOD/sofa_loader.h"
#include "hearGOD/nuols_engine.h"
#include "hearGOD/biquad_chain.h"
#include "hearGOD/peq_parser.h"
#include "hearGOD/portaudio_backend.h"
#include "hearGOD/config_file.h"
#include "hearGOD/dfe_compensator.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <portaudio.h>

static std::atomic<bool> gRunning{true};
static void sigHandler(int) { gRunning = false; }

static void printUsage(const char* prog)
{
    std::cout <<
        "Usage: " << prog << " [options]\n"
        "\n"
        "Options:\n"
        "  --sofa <path>        Path to .sofa HRIR file\n"
        "  --eq <path>          Parametric EQ preset (Equalizer APO format)\n"
        "  --mode <mode>        'binaural' (default) or 'stereo-eq' (bypass HRIR, EQ only)\n"
        "  --in <N>             Input device index\n"
        "  --out <N>            Output device index\n"
        "  --channels <N>       Input channel count (default 16 for 9.1.6, 12 for 7.1.4, 8 for 7.1)\n"
        "  --master-gain <dB>   Master output gain (default 0)\n"
        "  --lfe-gain <dB>      LFE channel gain (default 0)\n"
        "  --config <path>      Config file path (default ~/.config/hearGOD/config.json)\n"
        "  --save-config        Save current options to config file and exit\n"
        "  --buffer-size <N>    PortAudio buffer size in frames (default 256, try 512/1024 for xruns)\n"
        "  --keep-alive         Send silent stream to keep DAC awake (avoids idle pops)\n"
        "  --swap-lr            Swap L/R output channels (for reversed IEM wear)\n"
        "  --no-dfe             Disable diffuse-field EQ compensation (raw HRTF)\n"
        "  --list-devices       List audio devices and exit\n"
        "  --help               Show this help\n"
        "\n"
        "EQ format (Equalizer APO / AutoEQ):\n"
        "  Preamp: -6.1 dB\n"
        "  Filter 1: ON PK  Fc 32 Hz  Gain 4.0 dB  Q 1.41\n"
        "  Filter 2: ON LS  Fc 105 Hz Gain -3.5 dB Q 0.9\n"
        "\n"
        "Example:\n"
        "  " << prog << " --sofa ~/Downloads/FABIAN.sofa --eq ~/eq/moondrop_aria.txt --in 2 --out 1\n"
        "  " << prog << " --sofa ~/Downloads/FABIAN.sofa --channels 12 --in 2 --out 1  # 7.1.4\n"
        "  " << prog << " --mode stereo-eq --eq ~/eq/ic100_dsp.txt --in 2 --out 1      # EQ only\n";
}

// ANSI TUI — draws a live status block, refreshes in-place
static void tuiLoop(const hearGOD::PortAudioBackend& backend,
                    const hearGOD::AudioConfig& cfg)
{
    // Move cursor up N lines: ESC[NA
    auto clearLines = [](int n) {
        for (int i = 0; i < n; ++i)
            std::cout << "\033[A\033[2K";
    };

    constexpr int TUI_LINES = 12;
    bool first = true;

    while (gRunning) {
        const auto& s = backend.stats();
        double cpu = s.cpuLoad.load(std::memory_order_relaxed);
        int xruns   = s.xrunCount.load(std::memory_order_relaxed);

        if (!first) clearLines(TUI_LINES);
        first = false;

        std::string cpuBar(20, ' ');
        int filled = static_cast<int>(cpu * 20.0);
        for (int i = 0; i < filled && i < 20; ++i)
            cpuBar[i] = (i < 14) ? '#' : (i < 18 ? '!' : 'X');

        std::string eqLine = cfg.eqPath.empty()
            ? "\033[2mnone\033[0m"
            : cfg.eqPath;

        std::string modeLine = cfg.stereoEqMode
            ? "\033[33mstereo-eq\033[0m (HRIR bypassed)"
            : "\033[36mbinaural\033[0m";

        std::cout
            << "\033[1;36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n"
            << "  \033[1mhearGOD\033[0m — real-time audio engine\n"
            << "  Mode   : " << modeLine << "\n"
            << "  SOFA   : " << (cfg.stereoEqMode ? "\033[2mn/a\033[0m" : cfg.sofaPath) << "\n"
            << "  EQ     : " << eqLine << "\n"
            << "  Devices: in=" << cfg.inputDevice << "  out=" << cfg.outputDevice << "\n"
            << "  Layout : " << (cfg.stereoEqMode ? "2ch stereo" :
                                 std::to_string(cfg.inputChannels) + "ch " +
                                 (cfg.inputChannels >= 16 ? "9.1.6 (Atmos)" :
                                  cfg.inputChannels >= 12 ? "7.1.4 (Atmos)" : "7.1")) << "\n"
            << "  Gain   : master=" << cfg.masterGainDb << " dB"
            << "  lfe=" << cfg.lfeGainDb << " dB\n"
            << "  Keep-alive: " << (cfg.keepAlive ? "\033[32mon\033[0m" : "\033[2moff\033[0m")
            << "  L/R swap: "  << (cfg.swapLR    ? "\033[33mon\033[0m" : "\033[2moff\033[0m") << "\n"
            << "  CPU    : [" << cpuBar << "] " << int(cpu * 100) << "%\n"
            << "  Xruns  : " << (xruns == 0 ? "\033[32m0 ✓\033[0m"
                                             : "\033[31m" + std::to_string(xruns) + " !\033[0m")
            << "\n"
            << "\033[1;36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m"
            << "  \033[2mCtrl+C to stop\033[0m\n";
        std::cout.flush();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

struct ParseResult {
    hearGOD::AudioConfig cfg;
    std::filesystem::path configPath;
    bool saveConfig  = false;
    bool listDevices = false;
    bool noDFE       = false;
};

static ParseResult parseArgs(int argc, char** argv)
{
    ParseResult r;
    r.configPath = hearGOD::ConfigFile::defaultPath();
    hearGOD::ConfigFile::load(r.cfg, r.configPath);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if      (a == "--sofa")         r.cfg.sofaPath      = next();
        else if (a == "--eq")           r.cfg.eqPath        = next();
        else if (a == "--mode") {
            std::string mode = next();
            if (mode == "stereo-eq")       r.cfg.stereoEqMode = true;
            else if (mode != "binaural") {
                std::cerr << "Unknown mode: " << mode << " (use 'binaural' or 'stereo-eq')\n";
                std::exit(1);
            }
        }
        else if (a == "--in")           r.cfg.inputDevice   = std::stoi(next());
        else if (a == "--out")          r.cfg.outputDevice  = std::stoi(next());
        else if (a == "--channels")     r.cfg.inputChannels = std::stoi(next());
        else if (a == "--master-gain")  r.cfg.masterGainDb  = std::stof(next());
        else if (a == "--lfe-gain")     r.cfg.lfeGainDb     = std::stof(next());
        else if (a == "--config")       r.configPath        = next();
        else if (a == "--save-config")  r.saveConfig  = true;
        else if (a == "--list-devices") r.listDevices = true;
        else if (a == "--buffer-size")  r.cfg.bufferFrames  = std::stoi(next());
        else if (a == "--keep-alive")   r.cfg.keepAlive = true;
        else if (a == "--swap-lr")      r.cfg.swapLR    = true;
        else if (a == "--no-dfe")       r.noDFE         = true;
        else if (a == "--help" || a == "-h") { printUsage(argv[0]); std::exit(0); }
        else { std::cerr << "Unknown option: " << a << "\n"; printUsage(argv[0]); std::exit(1); }
    }
    return r;
}

int main(int argc, char** argv)
{
    auto [cfg, configPath, saveConfig, listDevices, noDFE] = parseArgs(argc, argv);

    if (listDevices) {
        hearGOD::PortAudioBackend::listDevices();
        return 0;
    }

    if (!cfg.stereoEqMode && cfg.sofaPath.empty()) {
        std::cerr << "Error: --sofa <path> required for binaural mode (or use --mode stereo-eq)\n";
        printUsage(argv[0]);
        return 1;
    }

    if (saveConfig) {
        hearGOD::ConfigFile::save(cfg, configPath);
        return 0;
    }

    // Probe device SR before constructing engine — engine/loader need it at init time.
    const int deviceSR = hearGOD::PortAudioBackend::probeDeviceSampleRate(cfg);
    std::cout << "[Audio] Device sample rate: " << deviceSR << " Hz\n";

    // --- Build NUOLS engine (binaural mode only) ---
    auto engine = std::make_shared<hearGOD::NUOLSEngine>(cfg.bufferFrames, 4, deviceSR);

    if (!cfg.stereoEqMode) {
        // --- Load SOFA ---
        hearGOD::SOFALoader sofa;
        if (!sofa.load(cfg.sofaPath, deviceSR)) {
            std::cerr << "Failed to load SOFA: " << cfg.sofaPath << "\n";
            return 1;
        }

        engine->setMasterGainDb(cfg.masterGainDb);
        engine->setLfeGainDb(cfg.lfeGainDb);

        const int numInputChannels = cfg.inputChannels;
        int loaded = 0;
        for (int i = 0; i < numInputChannels && i < hearGOD::MAX_CHANNELS; ++i) {
            auto ch = static_cast<hearGOD::Channel>(i);
            if (ch == hearGOD::Channel::LFE) continue;
            if (ch == hearGOD::Channel::INVALID) continue;
            auto hrir = sofa.getHRIRForChannel(ch);
            if (!hrir) {
                std::cerr << "Warning: no HRIR for channel " << i
                          << " (" << hearGOD::channelName(ch) << ")\n";
                continue;
            }
            engine->loadHRIR(ch, hrir->left.data(), hrir->right.data(), hrir->irLength);
            ++loaded;
        }

        if (!engine->isReady()) {
            std::cerr << "Engine not ready — FL/FR HRIR missing\n";
            return 1;
        }
        std::cout << "HRIR loaded for " << loaded << " channels\n";

        // --- Diffuse-Field EQ (on by default, --no-dfe to skip) ---
        if (!noDFE) {
            auto allHrirs = sofa.getAllHRIRs();
            hearGOD::DFECompensator dfe;
            dfe.compute(allHrirs, sofa.irLength());
            if (dfe.isReady()) {
                for (int i = 0; i < numInputChannels && i < hearGOD::MAX_CHANNELS; ++i) {
                    auto ch = static_cast<hearGOD::Channel>(i);
                    if (ch == hearGOD::Channel::LFE || ch == hearGOD::Channel::INVALID) continue;
                    auto hrir = sofa.getHRIRForChannel(ch);
                    if (!hrir) continue;
                    auto compL = dfe.apply(hrir->left.data(),  hrir->irLength);
                    auto compR = dfe.apply(hrir->right.data(), hrir->irLength);
                    engine->loadHRIR(ch, compL.data(), compR.data(), hrir->irLength);
                }
                std::cout << "[DFE] HRIRs compensated\n";
            }
        }
    } else {
        std::cout << "[Mode] stereo-eq — HRIR bypassed, EQ passthrough only\n";
    }

    // --- EQ ---
    auto eq = std::make_shared<hearGOD::BiquadChain>();
    if (!cfg.eqPath.empty()) {
        auto preset = hearGOD::parsePEQ(cfg.eqPath, static_cast<float>(deviceSR));
        if (!preset) {
            std::cerr << "Failed to load EQ preset: " << cfg.eqPath << "\n";
            return 1;
        }

        auto coeffs = hearGOD::buildCoeffs(*preset, static_cast<float>(deviceSR));
        eq->setFilters(coeffs);
        eq->setPreamp(preset->preampDb);

        if (!cfg.stereoEqMode) {
            // Binaural: also bake preamp into master gain for headroom management
            cfg.masterGainDb += preset->preampDb;
            engine->setMasterGainDb(cfg.masterGainDb);
        }

        std::cout << "[PEQ] Loaded " << preset->filters.size()
                  << " filter(s), preamp=" << preset->preampDb << " dB from " << cfg.eqPath << "\n";
        std::cout << "EQ active: " << preset->filters.size()
                  << " filter(s), preamp=" << preset->preampDb << " dB\n";
    }

    // Warm up: run one silent buffer to pull data into CPU cache before RT callback.
    {
        std::vector<float> silentOut(2 * cfg.bufferFrames, 0.0f);
        if (!cfg.stereoEqMode) {
            int warmCh = cfg.inputChannels;
            std::vector<std::vector<float>> silentIn(warmCh,
                std::vector<float>(cfg.bufferFrames, 0.0f));
            std::vector<const float*> silentPtrs(warmCh);
            for (int i = 0; i < warmCh; ++i) silentPtrs[i] = silentIn[i].data();
            engine->process(silentPtrs.data(), silentOut.data(), warmCh);
        }
        if (!eq->isEmpty())
            eq->processStereoInterleaved(silentOut.data(), cfg.bufferFrames);
    }

    // --- Start audio ---
    hearGOD::PortAudioBackend backend(cfg, engine, eq);
    if (!backend.start()) {
        std::cerr << "Failed to start audio stream\n";
        return 1;
    }

    std::signal(SIGINT,  sigHandler);
    std::signal(SIGTERM, sigHandler);

    // --- TUI loop ---
    tuiLoop(backend, cfg);

    backend.stop();
    std::cout << "\nhearGOD stopped.\n";
    return 0;
}
