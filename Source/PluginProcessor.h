#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "DrumEngine.h"
#include "Abdruck.h"

struct SporeVis { int from, to; };

/** Everything the BROTKRUMEN trail remembers about one state of the machine. */
struct Snapshot
{
    std::array<int8_t, tw::numVoices * tw::numSteps> steps {};
    std::array<int8_t, tw::numVoices> lens {};
    std::array<float, tw::numVoices * tw::numVoices> myzel {};
    std::array<float, tw::numVoices * tw::numVoiceParams> params {};   // normalised

    bool approxEquals (const Snapshot& o) const;
};

class TraunwerkProcessor : public juce::AudioProcessor
{
public:
    TraunwerkProcessor();
    ~TraunwerkProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Traunwerk"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    DrumEngine engine;

    // ---- pattern (message thread writes, audio thread reads) --------------
    int  getStep (int v, int s) const        { return steps[(size_t) v][(size_t) s].load(); }
    void setStep (int v, int s, int value)   { steps[(size_t) v][(size_t) s].store (juce::jlimit (0, 2, value)); }
    int  getLength (int v) const             { return lens[(size_t) v].load(); }
    void setLength (int v, int len)          { lens[(size_t) v].store (juce::jlimit (1, tw::numSteps, len)); }
    bool isLocked (int v) const              { return locks[(size_t) v].load(); }
    void setLocked (int v, bool b)           { locks[(size_t) v].store (b); }
    float getMyzel (int from, int to) const  { return myzel[(size_t) from][(size_t) to].load(); }
    void setMyzel (int from, int to, float w){ myzel[(size_t) from][(size_t) to].store (from == to ? 0.0f : juce::jlimit (0.0f, 1.0f, w)); }
    void clearPattern();

    // ---- live state for the editor ----------------------------------------
    int   getSand (int v, int s) const       { return sandVis[(size_t) v][(size_t) s].load(); }
    float getGhost (int v, int s) const      { return ghostVis[(size_t) v][(size_t) s].load(); }
    int   getPlayColumn (int v) const        { return playCol[(size_t) v].load(); }
    int   getLastAvalanche() const           { return lastAvalanche.load(); }
    int   getAvalancheCount() const          { return avalancheCounter.load(); }
    float getSandFill() const                { return sandFill.load(); }
    bool  isHostPlaying() const              { return hostPlayingVis.load(); }
    float getGrooveOffset (int s) const      { return grooveOffset[(size_t) s].load(); }

    /** TRAUNFALL: where step s (0..16) of the current bar lands, 0..1 of the bar. */
    float getWarpedPosition (float s) const;
    int   getRatchetCount() const            { return ratchetCounter.load(); }
    bool  isTraunfallOn() const              { return pTfOn->load() > 0.5f; }

    juce::AbstractFifo sporeFifo { 256 };
    std::array<SporeVis, 256> sporeBuf {};

    void triggerLawine()                     { lawineRequested.store (true); }
    void audition (int v)                    { auditionMask.fetch_or (1 << v); }
    void freshCompost()                      { engine.requestFreshCompost(); }

    // ---- sources -----------------------------------------------------------
    bool loadVoiceSample (int v, const juce::File&);
    void resetVoiceSample (int v);
    juce::String getVoiceSourceName (int v) const;
    bool loadAbdruck (const juce::File&);
    juce::String getAbdruckName() const      { return abdruckFile.getFileNameWithoutExtension(); }
    juce::String getAbdruckInfo() const      { return abdruckInfo; }

    // ---- BROTKRUMEN randomizer --------------------------------------------
    void randomize();
    int   getTrailSize() const               { return (int) trail.size(); }
    float getTrailPos() const                { return trailPos; }
    void  scrubTrail (float pos);

    // ---- presets -----------------------------------------------------------
    void applyPreset (int index);
    int getPresetChangeCount() const noexcept { return presetChangeCount.load(); }

    int selectedVoice = 0;   // editor state, kept here so it survives reopening

    void setParam (const juce::String& paramId, float plainValue);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Event
    {
        int offset;
        int voice;       // -1: a sequencer step
        float vel;
        int kind;
        int depth;
        int64_t step;
    };
    struct Spore { int64_t due; int voice; float vel; int depth; int from; };

    void gatherSettings();
    void fire (int voice, float vel, int kind, int depth, int offset);
    void sequencerStep (int64_t k, int offset);
    void sandStep();
    void resetSand();
    float stepOffset (int64_t k) const;
    double stepTime (int64_t k);                 // ppq of step k, TRAUNFALL warp included
    void updateBar (int64_t bar);
    VoiceSettings frayed (const VoiceSettings&);
    static double warpCurve (double x, double a1, double a2);

    Snapshot capture() const;
    void applySnapshot (const Snapshot&);
    void installAbdruck (const AbdruckResult&, bool setPattern);
    bool readFileMono (const juce::File&, double maxSeconds, juce::AudioBuffer<float>& out, double& rate);
    void setSourceKeepAlive (int v, SourcePtr src);

    std::array<std::array<std::atomic<int>, tw::numSteps>, tw::numVoices> steps;
    std::array<std::atomic<int>, tw::numVoices> lens;
    std::array<std::atomic<bool>, tw::numVoices> locks;
    std::array<std::array<std::atomic<float>, tw::numVoices>, tw::numVoices> myzel;
    std::array<std::atomic<float>, tw::numSteps> grooveOffset, grooveAccent;

    // audio-thread state
    std::array<std::array<int, tw::numSteps>, tw::numVoices> sand {};
    std::array<std::array<float, tw::numSteps>, tw::numVoices> ghost {};
    std::array<VoiceSettings, tw::numVoices> settings {};
    std::vector<Event> events;
    std::vector<Spore> spores;
    juce::Random rng { 0x7ac0 };
    double sr = 48000.0;
    int64_t absSample = 0;
    int64_t lastStep = -1;
    double lastEndPpq = 0.0, internalPpq = 0.0;
    bool wasRunning = false, wasInternal = false;
    double samplesPer16th = 6000.0;
    int lastThresh = 4;
    double lastStepT = -1.0e9;

    // TRAUNFALL state (audio thread)
    int64_t tfBar = -1;
    double warpA1 = 0.0, warpA2 = 0.0;
    std::array<int, tw::numVoices> rotation {};
    std::atomic<float> warpVisA1 { 0.0f }, warpVisA2 { 0.0f };
    std::atomic<int> ratchetCounter { 0 };

    // editor mirrors
    std::array<std::array<std::atomic<int>, tw::numSteps>, tw::numVoices> sandVis;
    std::array<std::array<std::atomic<float>, tw::numSteps>, tw::numVoices> ghostVis;
    std::array<std::atomic<int>, tw::numVoices> playCol;
    std::atomic<int> lastAvalanche { 0 }, avalancheCounter { 0 };
    std::atomic<float> sandFill { 0.0f };
    std::atomic<bool> hostPlayingVis { false };
    std::atomic<bool> lawineRequested { false };
    std::atomic<int> auditionMask { 0 };

    std::array<std::array<std::atomic<float>*, tw::numVoiceParams>, tw::numVoices> vp {};
    std::atomic<float>* pMaster = nullptr; std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pSwing = nullptr;  std::atomic<float>* pGroove = nullptr;
    std::atomic<float>* pBpm = nullptr;    std::atomic<float>* pPlay = nullptr;
    std::atomic<float>* pFeed = nullptr;   std::atomic<float>* pThresh = nullptr;
    std::atomic<float>* pSpur = nullptr;   std::atomic<float>* pGhost = nullptr;
    std::atomic<float>* pMyzel = nullptr;  std::atomic<float>* pMyzelDiv = nullptr;
    std::atomic<float>* pKompost = nullptr; std::atomic<float>* pRecover = nullptr;
    std::atomic<float>* pRndAmt = nullptr; std::atomic<float>* pRndTarget = nullptr;
    std::atomic<float>* pTfOn = nullptr;   std::atomic<float>* pTfWarp = nullptr;
    std::atomic<float>* pTfStutter = nullptr; std::atomic<float>* pTfShuffle = nullptr;
    std::atomic<float>* pTfFray = nullptr;

    juce::dsp::Limiter<float> limiter;

    // sources
    juce::AudioFormatManager formatManager;
    std::array<juce::File, tw::numVoices> voiceFiles;
    std::array<SourcePtr, tw::numVoices> builtIn;
    std::vector<SourcePtr> keepAlive;
    juce::File abdruckFile;
    double abdruckRate = 48000.0;
    juce::String abdruckInfo;

    // trail
    std::vector<Snapshot> trail;
    float trailPos = 0.0f;

    int currentProgram = 1;
    std::atomic<int> presetChangeCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TraunwerkProcessor)
};
