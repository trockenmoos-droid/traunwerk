#pragma once
#include <juce_core/juce_core.h>

/** Shared constants: voices, per-voice parameters and global parameter IDs. */
namespace tw
{
    constexpr int numVoices = 8;
    constexpr int numSteps  = 16;

    inline const char* const voiceNames[numVoices] { "KICK", "SNARE", "CLAP", "HAT",
                                                     "OPEN", "TOM",   "HOLZ", "GLOCKE" };

    /** General MIDI drum notes per voice. Notes not in this list fall back to
        (note - 36) mod 8, so a plain chromatic run from C1 also plays the kit. */
    inline const int gmNotes[numVoices] { 36, 38, 39, 42, 46, 45, 37, 56 };

    inline int voiceForNote (int note)
    {
        for (int v = 0; v < numVoices; ++v)
            if (gmNotes[v] == note) return v;
        if (note < 24) return -1;
        return ((note - 36) % numVoices + numVoices) % numVoices;
    }

    enum VoiceParam { pLevel, pPan, pPitch, pDecay, pAttack, pBite, pDensity,
                      pGrain, pSpray, pScan, pTone, pWear, numVoiceParams };

    struct ParamSpec
    {
        const char* id;
        const char* label;
        float min, max, def;
        float centre;        // value at the knob's middle (skew); < min means linear
        const char* unit;
    };

    inline const ParamSpec voiceSpecs[numVoiceParams]
    {
        { "level",   "LEVEL",    0.0f,   1.0f,   0.8f,   -100.0f, ""   },
        { "pan",     "PAN",     -1.0f,   1.0f,   0.0f,   -100.0f, ""   },
        { "pitch",   "PITCH",  -24.0f,  24.0f,   0.0f,   -100.0f, "st" },
        { "decay",   "DECAY",    0.02f,  3.0f,   0.4f,    0.35f,  "s"  },
        { "attack",  "ATTACK",   0.0f,  40.0f,   6.0f,    8.0f,   "ms" },
        { "bite",    "BITE",     0.0f,   1.5f,   0.9f,   -100.0f, ""   },
        { "density", "DENSITY",  4.0f, 600.0f, 140.0f,   90.0f,   "/s" },
        { "grain",   "GRAIN",    2.0f, 150.0f,  28.0f,   25.0f,   "ms" },
        { "spray",   "SPRAY",    0.0f,   1.0f,   0.08f,  -100.0f, ""   },
        { "scan",    "SCAN",     0.0f,   2.0f,   1.0f,   -100.0f, "x"  },
        { "tone",    "TONE",    -1.0f,   1.0f,   0.0f,   -100.0f, ""   },
        { "wear",    "WEAR",     0.0f,   1.0f,   0.25f,  -100.0f, ""   },
    };

    inline juce::String voiceParamId (int v, int p)
    {
        return "v" + juce::String (v + 1) + "_" + voiceSpecs[p].id;
    }

    /** Per-voice factory defaults that differ from the generic spec. */
    inline float voiceDefault (int v, int p)
    {
        static const float decay[numVoices] { 0.55f, 0.25f, 0.28f, 0.07f, 0.45f, 0.45f, 0.10f, 1.10f };
        static const float pan  [numVoices] { 0.0f, 0.0f, -0.08f, 0.18f, 0.22f, -0.22f, -0.3f, 0.28f };
        static const float level[numVoices] { 0.9f, 0.78f, 0.7f, 0.55f, 0.5f, 0.72f, 0.66f, 0.5f };
        static const float attk [numVoices] { 9.0f, 6.0f, 4.0f, 2.0f, 3.0f, 8.0f, 5.0f, 6.0f };

        switch (p)
        {
            case pDecay:  return decay[v];
            case pPan:    return pan[v];
            case pLevel:  return level[v];
            case pAttack: return attk[v];
            default:      return voiceSpecs[p].def;
        }
    }

    // Global parameter IDs
    namespace id
    {
        inline constexpr const char* master    = "master";
        inline constexpr const char* drive     = "drive";
        inline constexpr const char* swing     = "swing";
        inline constexpr const char* groove    = "groove";
        inline constexpr const char* bpm       = "bpm";
        inline constexpr const char* play      = "play";
        inline constexpr const char* feed      = "feed";
        inline constexpr const char* thresh    = "thresh";
        inline constexpr const char* spur      = "spur";
        inline constexpr const char* ghost     = "ghost";
        inline constexpr const char* myzel     = "myzel";
        inline constexpr const char* myzelDiv  = "myzelDiv";
        inline constexpr const char* kompost   = "kompost";
        inline constexpr const char* recover   = "recover";
        inline constexpr const char* rndAmt    = "rndAmt";
        inline constexpr const char* rndTarget = "rndTarget";
        inline constexpr const char* tfOn      = "tfOn";
        inline constexpr const char* tfWarp    = "tfWarp";
        inline constexpr const char* tfStutter = "tfStutter";
        inline constexpr const char* tfShuffle = "tfShuffle";
        inline constexpr const char* tfFray    = "tfFray";
    }

    inline const juce::StringArray myzelDivNames { "1/32", "1/16T", "1/16", "1/8T", "1/8" };
    inline const double myzelDivSixteenths[] { 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0 };
    inline const juce::StringArray rndTargetNames { "MUSTER", "KLANG", "ALLES" };
}
