#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include "Params.h"

/** A source the voices granulate: mono audio at its own sample rate. */
struct DrumSource
{
    juce::AudioBuffer<float> data;
    double rate = 48000.0;
    juce::String name;      // file name, or empty for the built-in sound
};
using SourcePtr = std::shared_ptr<const DrumSource>;

/** The sound settings a hit takes with it when it is triggered. */
struct VoiceSettings
{
    float level = 0.8f, pan = 0.0f, pitch = 0.0f, decay = 0.4f, attackMs = 6.0f, bite = 0.9f,
          density = 140.0f, grainMs = 28.0f, spray = 0.08f, scan = 1.0f, tone = 0.0f, wear = 0.25f;
};

/** Visual feedback: one entry per grain spawned, read by the editor. */
struct GrainDot
{
    int   voice;
    float t;        // seconds since the hit started
    float pos;      // read position, 0..1 of the source (compost grains: 0..1 of the window)
    float amp;
    bool  compost;
};

struct HitInfo
{
    int   voice;
    float velocity;
    int   kind;     // 0 pattern, 1 lawine ghost, 2 myzel spore, 3 midi / audition, 4 traunfall ratchet
    int64_t time;   // engine sample clock at the trigger
};

/** Eight granular drum voices.

    Every hit is two layers:
      - ANSCHLAG: the first few milliseconds of the source played straight,
        so the transient stays sharp;
      - KÖRPER: a grain cloud behind it whose grain *rate* follows the decay
        envelope (a density envelope rather than an amplitude envelope).

    VERWITTERUNG: each voice carries a wear state that grows with every hit
    and recovers in the rests. Wear shortens grains, thins them, darkens the
    tone, drifts the read position and lowers the pitch a little.

    KOMPOST: the engine's own output is written into a ring buffer. Body
    grains can read from the moment this voice was last heard in it, so the
    kit gradually feeds on what it played before. */
class DrumEngine
{
public:
    static constexpr int hitsPerVoice = 8;
    static constexpr int maxGrains    = 40;

    DrumEngine();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Called from the message thread; the swap is picked up at the next block. */
    void setSource (int voice, SourcePtr src);
    SourcePtr getSource (int voice) const;

    void setKompost (float amount)         { kompostAmt = amount; }
    void setRecovery (float seconds)       { recoverySec = juce::jmax (0.05f, seconds); }
    void requestFreshCompost()             { freshRequested.store (true); }

    void trigger (int voice, float velocity, const VoiceSettings&, int kind);

    /** Adds numSamples of output to L and R. */
    void render (float* L, float* R, int numSamples, const std::array<VoiceSettings, tw::numVoices>&);

    // --- editor feedback ----------------------------------------------------
    float getWear (int v) const            { return wearVis[(size_t) v].load(); }
    float getCompostBin (int i) const      { return compostBins[(size_t) i].load(); }
    float getCompostFill() const           { return compostFillVis.load(); }
    static constexpr int numCompostBins = 64;

    juce::AbstractFifo grainFifo { 4096 };
    std::array<GrainDot, 4096> grainBuf {};
    juce::AbstractFifo hitFifo { 256 };
    std::array<HitInfo, 256> hitBuf {};

private:
    struct Grain
    {
        bool   active = false;
        bool   compost = false;
        double pos = 0.0;      // read position (source samples, or ring index offset)
        double inc = 1.0;
        int    age = 0, len = 1;
        float  gL = 0.0f, gR = 0.0f;
    };

    struct Hit
    {
        bool   active = false;
        int    fadeOut = 0;        // > 0: being choked / stolen
        int    fadeLen = 1;
        int    voice = 0;
        double t = 0.0;            // samples since start
        SourcePtr src;
        double ratio = 1.0;        // source samples per output sample at the set pitch
        double scanRate = 1.0;     // source samples per output sample for the read head
        double directPos = 0.0;
        float  attackSamples = 0.0f;
        float  bite = 1.0f;
        float  level = 1.0f;
        float  panL = 0.7f, panR = 0.7f;
        float  decaySamples = 1.0f;
        float  grainsPerSample = 0.0f;
        float  grainLen = 1000.0f;
        float  spraySamples = 0.0f;
        float  sprayAmt = 0.0f;
        float  drift = 0.0f;       // source samples of wear drift
        float  norm = 1.0f;
        double nextGrain = 0.0;
        bool   bodyDone = false;
        int64_t compostAnchor = -1; // absolute write index of this voice's previous hit
        int    compostSpan = 0;    // samples available after the anchor
        float  scanOut = 1.0f;     // read-head speed in output samples (for compost)
        double pitchRatio = 1.0;   // pure pitch ratio (for compost)
        float  pan = 0.0f;
        float  compostAmt = 0.0f;
        uint32_t serial = 0;
        std::array<Grain, maxGrains> grains;
    };

    void spawnGrain (Hit&);
    void renderHit (Hit&, float* L, float* R, int n);
    float readSource (const DrumSource&, double pos) const noexcept;
    float readCompost (double pos) const noexcept;

    void pushGrainDot (const GrainDot&);
    void pushHit (const HitInfo&);

    double sr = 48000.0;
    int maxBlockSize = 512;

    std::array<SourcePtr, tw::numVoices> sources;      // audio-thread copy
    std::array<SourcePtr, tw::numVoices> pending;      // guarded by pendingLock
    std::array<bool, tw::numVoices> pendingFlag {};
    juce::SpinLock pendingLock;

    std::array<std::array<Hit, hitsPerVoice>, tw::numVoices> hits;
    std::array<float, tw::numVoices> wear {};
    std::array<std::atomic<float>, tw::numVoices> wearVis;
    std::array<float, tw::numVoices> driftDir {};
    uint32_t serialCounter = 0;
    int64_t renderedSamples = 0;

    // per voice stereo work buffers + filter state
    juce::AudioBuffer<float> voiceBuf;
    struct SvfState { float s1 = 0, s2 = 0; };
    std::array<std::array<SvfState, 4>, tw::numVoices> filt {};  // LP L/R, HP L/R

    // compost
    std::vector<float> compost;
    int compostWrite = 0;
    int64_t compostAbs = 0;
    int compostFilled = 0;
    std::array<int64_t, tw::numVoices> compostLastAnchor {};
    float kompostAmt = 0.0f;
    float recoverySec = 1.5f;
    std::atomic<bool> freshRequested { false };
    std::array<std::atomic<float>, numCompostBins> compostBins;
    std::atomic<float> compostFillVis { 0.0f };
    float binPeak = 0.0f;
    int   lastBin = -1;

    std::array<float, 1024> window {};
    juce::Random rng { 0x9a1e };   // fixed seed: reproducible renders (the music is still random)
};
