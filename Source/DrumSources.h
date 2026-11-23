#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

/** The built-in kit, synthesised in code so the plugin plays without any
    samples. These are deliberately plain, dry "raw material": the granular
    voice engine is what gives them their character. */
namespace DrumSources
{
    constexpr double rate = 48000.0;

    /** Mono source for voice v (0..7) at DrumSources::rate. Deterministic. */
    juce::AudioBuffer<float> make (int voice);
}
