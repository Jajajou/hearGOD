cmake_minimum_required(VERSION 3.20)
project(hearGOD VERSION 0.3.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple frameworks
find_library(ACCELERATE_FRAMEWORK   Accelerate    REQUIRED)
find_library(COREAUDIO_FRAMEWORK    CoreAudio     REQUIRED)
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox  REQUIRED)
find_library(AUDIOUNIT_FRAMEWORK    AudioUnit     REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
find_library(CORESERVICES_FRAMEWORK CoreServices  REQUIRED)

find_package(PkgConfig REQUIRED)

# Static lib paths can be passed by CI: -DPORTAUDIO_STATIC_LIB=<path> -DMYSOFA_STATIC_LIB=<path>
# Local dev leaves these unset and falls back to pkg-config (Homebrew dynamic).
option(PORTAUDIO_STATIC_LIB "Path to static libportaudio.a" "")
option(MYSOFA_STATIC_LIB    "Path to static libmysofa.a"    "")

if(NOT PORTAUDIO_STATIC_LIB)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
endif()
if(NOT MYSOFA_STATIC_LIB)
    pkg_check_modules(MYSOFA REQUIRED libmysofa)
endif()

set(COMMON_COMPILE_OPTIONS -O3 -ffast-math -march=native)

# --------------------------------------------------------------------------
# hearGOD_core — everything except main.cpp, linkable by tests
# --------------------------------------------------------------------------
set(CORE_SOURCES
    src/core/nuols_engine.cpp
    src/core/fdl.cpp
    src/core/dfe_compensator.cpp
    src/audio/channel_router.cpp
    src/audio/portaudio_backend.cpp
    src/io/sofa_loader.cpp
    src/eq/biquad_chain.cpp
    src/eq/peq_parser.cpp
    src/config/config_file.cpp
)

add_library(hearGOD_core STATIC ${CORE_SOURCES})

if(PORTAUDIO_STATIC_LIB)
    set(_PORTAUDIO_INC "/usr/local/include")
    set(_PORTAUDIO_LIB "${PORTAUDIO_STATIC_LIB}")
else()
    set(_PORTAUDIO_INC "${PORTAUDIO_INCLUDE_DIRS}")
    set(_PORTAUDIO_LIB "${PORTAUDIO_LINK_LIBRARIES}")
endif()

if(MYSOFA_STATIC_LIB)
    set(_MYSOFA_INC "/usr/local/include")
    set(_MYSOFA_LIB "${MYSOFA_STATIC_LIB}")
else()
    set(_MYSOFA_INC "${MYSOFA_INCLUDE_DIRS}")
    set(_MYSOFA_LIB "${MYSOFA_LINK_LIBRARIES}")
endif()

target_include_directories(hearGOD_core PUBLIC
    include
    ${_PORTAUDIO_INC}
    ${_MYSOFA_INC}
)

target_link_libraries(hearGOD_core
    ${ACCELERATE_FRAMEWORK}
    ${COREAUDIO_FRAMEWORK}
    ${AUDIOTOOLBOX_FRAMEWORK}
    ${AUDIOUNIT_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
    ${CORESERVICES_FRAMEWORK}
    ${_PORTAUDIO_LIB}
    ${_MYSOFA_LIB}
)

target_compile_options(hearGOD_core PRIVATE ${COMMON_COMPILE_OPTIONS})

# --------------------------------------------------------------------------
# hearGOD — main executable
# --------------------------------------------------------------------------
add_executable(hearGOD src/main.cpp)
target_link_libraries(hearGOD hearGOD_core)
target_compile_options(hearGOD PRIVATE ${COMMON_COMPILE_OPTIONS})

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
# Install
include(GNUInstallDirs)
install(TARGETS hearGOD RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

enable_testing()
find_package(GTest REQUIRED)

set(TEST_SOURCES
    tests/test_fdl.cpp
    tests/test_biquad.cpp
    tests/test_sofa.cpp
    tests/test_peq.cpp
    tests/test_integration.cpp
)

add_executable(hearGOD_tests ${TEST_SOURCES})

target_link_libraries(hearGOD_tests
    hearGOD_core
    GTest::gtest_main
)

target_compile_options(hearGOD_tests PRIVATE ${COMMON_COMPILE_OPTIONS})

include(GoogleTest)
gtest_discover_tests(hearGOD_tests)

# --------------------------------------------------------------------------
# hearGOD_app — JUCE GUI application
# --------------------------------------------------------------------------
juce_add_gui_app(hearGOD_app
    PRODUCT_NAME    "hearGOD"
    VERSION         "${PROJECT_VERSION}"
    BUNDLE_ID       "com.heargod.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "hearGOD needs microphone access to read audio input."
)

target_sources(hearGOD_app PRIVATE
    src/gui/main_juce.cpp
    src/gui/MainWindow.cpp
    src/gui/MainComponent.cpp
    src/gui/HearGodAudioProcessor.cpp
    src/gui/EQGraph.cpp
    src/gui/MultiChannelMeter.cpp
    src/gui/HRIRPanel.cpp
    src/gui/LogPanel.cpp
    src/gui/SystemAudioSwitch.cpp
)

target_compile_definitions(hearGOD_app PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:hearGOD_app,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:hearGOD_app,JUCE_VERSION>"
)

target_link_libraries(hearGOD_app PRIVATE
    hearGOD_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    "-framework CoreAudio"
)

target_compile_options(hearGOD_app PRIVATE ${COMMON_COMPILE_OPTIONS})
