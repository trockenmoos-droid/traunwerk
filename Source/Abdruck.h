#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "Params.h"

/** ABDRUCK ("imprint"): analyses one recording and takes from it
      - a kit: one slice per voice, chosen by brightness (dark slices become
        KICK and TOM, bright ones HAT and OPEN),
      - a groove: the micro-timing of every 16th and its typical loudness,
      - a pattern: where each kind of sound tends to fall in the bar.
    So the sound and the feel come from the same place. */
struct AbdruckResult
{
    bool valid = false;
    int  numOnsets = 0;
    double bpm = 0.0;
    bool hasGroove = false;

    std::array<juce::AudioBuffer<float>, tw::numVoices> slices;
    std::array<bool, tw::numVoices> hasSlice {};

    std::array<float, tw::numSteps> offsets {};   // fraction of a 16th, -0.4..0.4
    std::array<float, tw::numSteps> accents {};   // velocity factor 0.6..1.2
    std::array<std::array<int, tw::numSteps>, tw::numVoices> steps {};
};

namespace Abdruck
{
    /** mono: the recording, any length (the first 60 s are used). */
    AbdruckResult analyse (const juce::AudioBuffer<float>& mono, double rate);
}
