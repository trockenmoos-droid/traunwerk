#pragma once
#include <vector>
#include <tuple>
#include <utility>
#include "Params.h"

/** Factory presets: a pattern per voice (string: '.' off, 'x' hit, 'X' accent;
    the string's length is the voice's length, so polymeters are just shorter
    strings), sound changes, global settings and the MYZEL network. */
struct PresetDef
{
    const char* name;
    float bpm;
    const char* pattern[tw::numVoices];
    std::vector<std::tuple<int, int, float>> voice;           // voice, VoiceParam, plain value
    std::vector<std::pair<const char*, float>> globals;       // param id, plain value
    std::vector<std::tuple<int, int, float>> myzel;           // from, to, weight
};

namespace Presets
{
    const std::vector<PresetDef>& all();
}
