cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS. Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target")
endif()

project(Traunwerk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # Static runtime so the VST3 runs without a redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# VST3 and Standalone everywhere; AU only on macOS; LV2 on Linux.
set(TRAUNWERK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TRAUNWERK_FORMATS AU)
elseif(UNIX AND NOT APPLE)
    list(APPEND TRAUNWERK_FORMATS LV2)
endif()

set(TRAUNWERK_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DrumEngine.cpp
    Source/DrumSources.cpp
    Source/Abdruck.cpp
    Source/Presets.cpp
    Source/LookAndFeel.cpp
)

juce_add_plugin(Traunwerk
    COMPANY_NAME             "trockenmoos"
    COMPANY_WEBSITE          "https://www.trockenmoos.com"
    BUNDLE_ID                "com.trockenmoos.traunwerk"
    PLUGIN_MANUFACTURER_CODE Trkn
    PLUGIN_CODE              Trwk
    LV2URI                   "https://www.trockenmoos.com/plugins/traunwerk"
    FORMATS                  ${TRAUNWERK_FORMATS}
    PRODUCT_NAME             "Traunwerk"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_CATEGORIES          "Instrument" "Drum"
    AU_MAIN_TYPE             "kAudioUnitType_MusicDevice"
)

target_sources(Traunwerk PRIVATE ${TRAUNWERK_SOURCES})

target_compile_definitions(Traunwerk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_JACK=1
    JUCE_ALSA=1
)

target_link_libraries(Traunwerk PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Offline render test: renders every preset headlessly through a simulated
# host transport, checks levels and stability, writes demo WAVs and an editor
# screenshot. Build with -DTRAUNWERK_BUILD_TESTS=ON
option(TRAUNWERK_BUILD_TESTS "Build the offline render test" OFF)

if(TRAUNWERK_BUILD_TESTS)
    juce_add_console_app(TraunwerkRender PRODUCT_NAME "TraunwerkRender")
    target_sources(TraunwerkRender PRIVATE tests/RenderTest.cpp ${TRAUNWERK_SOURCES})
    target_compile_definitions(TraunwerkRender PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Traunwerk"
        JucePlugin_IsSynth=1
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
    )
    target_link_libraries(TraunwerkRender PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
endif()
