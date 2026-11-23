#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DrumSources.h"
#include "Presets.h"

using namespace tw;

namespace
{
    const float stepTemplates[numVoices][numSteps]
    {
        { .95f,.05f,.15f,.10f, .50f,.05f,.20f,.15f, .85f,.05f,.25f,.10f, .45f,.10f,.20f,.20f }, // KICK
        { .02f,.05f,.05f,.08f, .95f,.05f,.10f,.15f, .03f,.08f,.10f,.10f, .95f,.10f,.15f,.25f }, // SNARE
        { .00f,.03f,.05f,.05f, .55f,.05f,.05f,.10f, .02f,.05f,.05f,.10f, .60f,.05f,.10f,.15f }, // CLAP
        { .80f,.35f,.85f,.35f, .80f,.35f,.85f,.35f, .80f,.35f,.85f,.35f, .80f,.35f,.85f,.40f }, // HAT
        { .02f,.05f,.50f,.05f, .02f,.05f,.50f,.05f, .02f,.05f,.50f,.05f, .02f,.05f,.55f,.10f }, // OPEN
        { .05f,.10f,.05f,.15f, .05f,.10f,.10f,.15f, .05f,.10f,.10f,.20f, .05f,.15f,.25f,.30f }, // TOM
        { .10f,.20f,.15f,.45f, .10f,.20f,.45f,.15f, .10f,.20f,.15f,.45f, .10f,.30f,.40f,.20f }, // HOLZ
        { .15f,.05f,.10f,.10f, .10f,.05f,.20f,.10f, .15f,.05f,.10f,.15f, .10f,.10f,.15f,.10f }, // GLOCKE
    };

    juce::NormalisableRange<float> rangeFor (const ParamSpec& s)
    {
        juce::NormalisableRange<float> r (s.min, s.max);
        if (s.centre > s.min) r.setSkewForCentre (s.centre);
        return r;
    }

    juce::String csv (const float* v, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i) a.add (juce::String (v[i], 4));
        return a.joinIntoString (",");
    }
}

bool Snapshot::approxEquals (const Snapshot& o) const
{
    if (steps != o.steps || lens != o.lens) return false;
    for (size_t i = 0; i < myzel.size(); ++i)  if (std::abs (myzel[i] - o.myzel[i]) > 1.0e-4f) return false;
    for (size_t i = 0; i < params.size(); ++i) if (std::abs (params[i] - o.params[i]) > 1.0e-4f) return false;
    return true;
}

//==============================================================================
TraunwerkProcessor::TraunwerkProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    for (int v = 0; v < numVoices; ++v)
    {
        for (int p = 0; p < numVoiceParams; ++p)
            vp[(size_t) v][(size_t) p] = apvts.getRawParameterValue (voiceParamId (v, p));

        for (int s = 0; s < numSteps; ++s)
        {
            steps[(size_t) v][(size_t) s].store (0);
            sandVis[(size_t) v][(size_t) s].store (0);
            ghostVis[(size_t) v][(size_t) s].store (0.0f);
        }
        lens[(size_t) v].store (numSteps);
        locks[(size_t) v].store (false);
        playCol[(size_t) v].store (-1);
        for (int j = 0; j < numVoices; ++j) myzel[(size_t) v][(size_t) j].store (0.0f);

        auto src = std::make_shared<DrumSource>();
        src->data = DrumSources::make (v);
        src->rate = DrumSources::rate;
        builtIn[(size_t) v] = src;
        setSourceKeepAlive (v, src);
    }
    for (int s = 0; s < numSteps; ++s) { grooveOffset[(size_t) s].store (0.0f); grooveAccent[(size_t) s].store (1.0f); }

    pMaster   = apvts.getRawParameterValue (id::master);
    pDrive    = apvts.getRawParameterValue (id::drive);
    pSwing    = apvts.getRawParameterValue (id::swing);
    pGroove   = apvts.getRawParameterValue (id::groove);
    pBpm      = apvts.getRawParameterValue (id::bpm);
    pPlay     = apvts.getRawParameterValue (id::play);
    pFeed     = apvts.getRawParameterValue (id::feed);
    pThresh   = apvts.getRawParameterValue (id::thresh);
    pSpur     = apvts.getRawParameterValue (id::spur);
    pGhost    = apvts.getRawParameterValue (id::ghost);
    pMyzel    = apvts.getRawParameterValue (id::myzel);
    pMyzelDiv = apvts.getRawParameterValue (id::myzelDiv);
    pKompost  = apvts.getRawParameterValue (id::kompost);
    pRecover  = apvts.getRawParameterValue (id::recover);
    pRndAmt   = apvts.getRawParameterValue (id::rndAmt);
    pRndTarget= apvts.getRawParameterValue (id::rndTarget);
    pTfOn      = apvts.getRawParameterValue (id::tfOn);
    pTfWarp    = apvts.getRawParameterValue (id::tfWarp);
    pTfStutter = apvts.getRawParameterValue (id::tfStutter);
    pTfShuffle = apvts.getRawParameterValue (id::tfShuffle);
    pTfFray    = apvts.getRawParameterValue (id::tfFray);

    events.reserve (512);
    spores.reserve (256);
    resetSand();
    applyPreset (currentProgram);
}

juce::AudioProcessorValueTreeState::ParameterLayout TraunwerkProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int v = 0; v < numVoices; ++v)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "voice" + juce::String (v + 1), juce::String (voiceNames[v]), "|");
        for (int p = 0; p < numVoiceParams; ++p)
        {
            const auto& s = voiceSpecs[p];
            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { voiceParamId (v, p), 1 },
                juce::String (voiceNames[v]) + " " + s.label,
                rangeFor (s), voiceDefault (v, p),
                juce::AudioParameterFloatAttributes().withLabel (s.unit)));
        }
        layout.add (std::move (group));
    }

    auto f = [&] (const char* pid, const char* name, float lo, float hi, float def, float centre = -1.0e9f, const char* unit = "")
    {
        juce::NormalisableRange<float> r (lo, hi);
        if (centre > lo) r.setSkewForCentre (centre);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, 1 }, name, r, def,
                                                                  juce::AudioParameterFloatAttributes().withLabel (unit)));
    };

    f (id::master,  "Master",          0.0f, 1.5f, 0.8f);
    f (id::drive,   "Drive",           0.0f, 1.0f, 0.15f);
    f (id::swing,   "Swing",           0.0f, 0.5f, 0.0f);
    f (id::groove,  "Abdruck Groove",  0.0f, 1.0f, 0.0f);
    f (id::bpm,     "Internal BPM",   60.0f, 200.0f, 120.0f, -1.0e9f, "bpm");
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::play, 1 }, "Internal Play", false));
    f (id::feed,    "Lawine Feed",     0.0f, 1.0f, 0.25f);
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id::thresh, 1 }, "Lawine Threshold", 4, 10, 4));
    f (id::spur,    "Lawine Erosion",  0.0f, 1.0f, 0.0f);
    f (id::ghost,   "Lawine Ghosts",   0.0f, 1.0f, 0.6f);
    f (id::myzel,   "Myzel Amount",    0.0f, 1.0f, 0.5f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::myzelDiv, 1 }, "Myzel Delay", myzelDivNames, 2));
    f (id::kompost, "Kompost",         0.0f, 1.0f, 0.0f);
    f (id::recover, "Recovery",        0.1f, 10.0f, 1.5f, 1.5f, "s");
    f (id::rndAmt,  "Random Amount",   0.0f, 1.0f, 0.5f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::rndTarget, 1 }, "Random Target", rndTargetNames, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::tfOn, 1 }, "Traunfall", false));
    f (id::tfWarp,    "Traunfall Warp",     0.0f, 1.0f, 0.5f);
    f (id::tfStutter, "Traunfall Stutter",  0.0f, 1.0f, 0.4f);
    f (id::tfShuffle, "Traunfall Shuffle",  0.0f, 1.0f, 0.3f);
    f (id::tfFray,    "Traunfall Fray",     0.0f, 1.0f, 0.4f);

    return layout;
}

void TraunwerkProcessor::setParam (const juce::String& paramId, float plain)
{
    if (auto* p = apvts.getParameter (paramId))
    {
        const float norm = p->convertTo0to1 (plain);
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

bool TraunwerkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TraunwerkProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (64, samplesPerBlock), 2 };
    limiter.prepare (spec);
    limiter.setThreshold (-0.6f);
    limiter.setRelease (80.0f);
    absSample = 0;
    lastStep = -1;
    lastStepT = -1.0e9;
    tfBar = -1;
    wasRunning = false;
    spores.clear();
    resetSand();      // also clears pending ghost notes
}

//==============================================================================
void TraunwerkProcessor::gatherSettings()
{
    for (int v = 0; v < numVoices; ++v)
    {
        auto& s = settings[(size_t) v];
        const auto& p = vp[(size_t) v];
        s.level = p[pLevel]->load();   s.pan = p[pPan]->load();     s.pitch = p[pPitch]->load();
        s.decay = p[pDecay]->load();   s.attackMs = p[pAttack]->load(); s.bite = p[pBite]->load();
        s.density = p[pDensity]->load(); s.grainMs = p[pGrain]->load(); s.spray = p[pSpray]->load();
        s.scan = p[pScan]->load();     s.tone = p[pTone]->load();   s.wear = p[pWear]->load();
    }
}

float TraunwerkProcessor::stepOffset (int64_t k) const
{
    float off = (k & 1) ? pSwing->load() : 0.0f;
    off += pGroove->load() * grooveOffset[(size_t) (k % numSteps)].load();
    return juce::jlimit (-0.45f, 0.55f, off);
}

//==============================================================================
// TRAUNFALL
//
// WARP bends time inside each bar: step x (0..1 of the bar) lands at
//   w(x) = x + a1 sin(2 pi x) / (2 pi) + a2 sin(4 pi x) / (4 pi)
// with |a1| + |a2| < 1, so w is monotonic and w(0) = 0, w(1) = 1: the bar
// stretches and rushes inside, but every bar still starts on the host's
// downbeat. a1 and a2 drift from bar to bar.
double TraunwerkProcessor::warpCurve (double x, double a1, double a2)
{
    constexpr double twoPi = 6.283185307179586;
    return x + a1 * std::sin (twoPi * x) / twoPi + a2 * std::sin (2.0 * twoPi * x) / (2.0 * twoPi);
}

float TraunwerkProcessor::getWarpedPosition (float s) const
{
    if (! isTraunfallOn()) return s / 16.0f;
    return (float) warpCurve (s / 16.0, warpVisA1.load(), warpVisA2.load());
}

void TraunwerkProcessor::updateBar (int64_t bar)
{
    if (bar <= tfBar) return;      // only ever move forward: the scheduler looks ahead
    tfBar = bar;

    // WARP: random walk of the curve, bounded by the amount
    const double limit = 0.85 * pTfWarp->load();
    warpA1 = juce::jlimit (-limit, limit, 0.45 * warpA1 + (rng.nextDouble() * 2.0 - 1.0) * limit * 0.8);
    const double rest = juce::jmax (0.0, limit - std::abs (warpA1));
    warpA2 = juce::jlimit (-rest, rest, 0.45 * warpA2 + (rng.nextDouble() * 2.0 - 1.0) * rest * 0.8);
    warpVisA1.store ((float) warpA1);
    warpVisA2.store ((float) warpA2);

    // UMSCHICHTEN: some rows read their pattern from a rotated position this bar
    const float shuf = pTfShuffle->load();
    for (int v = 0; v < numVoices; ++v)
    {
        const int len = lens[(size_t) v].load();
        rotation[(size_t) v] = (! isLocked (v) && rng.nextFloat() < shuf * 0.5f) ? rng.nextInt (len) : 0;
    }
}

double TraunwerkProcessor::stepTime (int64_t k)
{
    double pos = (double) k;
    float offScale = 1.0f;
    if (pTfOn->load() > 0.5f)
    {
        const int64_t bar = k / numSteps;
        updateBar (bar);
        pos = (double) (bar * numSteps) + numSteps * warpCurve ((double) (k % numSteps) / numSteps, warpA1, warpA2);
        offScale = 1.0f - 0.7f * pTfWarp->load();
    }
    return (pos + stepOffset (k) * offScale) / 4.0;
}

/** ZERFASERN: every hit gets its own small variation of the sound. */
VoiceSettings TraunwerkProcessor::frayed (const VoiceSettings& in)
{
    const float z = pTfFray->load();
    if (z <= 0.0f) return in;
    auto r = [this] { return rng.nextFloat() * 2.0f - 1.0f; };
    VoiceSettings s = in;
    s.pitch   += r() * z * 2.5f;
    s.grainMs  = juce::jlimit (2.0f, 150.0f, s.grainMs * std::pow (2.0f, r() * z * 1.2f));
    s.density  = juce::jlimit (4.0f, 600.0f, s.density * std::pow (2.0f, r() * z));
    s.decay    = juce::jlimit (0.02f, 3.0f, s.decay * std::pow (2.0f, r() * z * 0.8f));
    s.spray    = juce::jlimit (0.0f, 1.0f, s.spray + std::abs (r()) * z * 0.25f);
    s.scan     = juce::jlimit (0.0f, 2.0f, s.scan * std::pow (2.0f, r() * z * 0.7f));
    s.pan      = juce::jlimit (-1.0f, 1.0f, s.pan + r() * z * 0.3f);
    return s;
}

void TraunwerkProcessor::resetSand()
{
    const int thr = juce::jlimit (4, 10, (int) (pThresh != nullptr ? pThresh->load() : 4.0f));
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
        {
            sand[(size_t) v][(size_t) s] = rng.nextInt (thr - 1);
            ghost[(size_t) v][(size_t) s] = 0.0f;
        }
    lastThresh = thr;
}

void TraunwerkProcessor::sandStep()
{
    const int thr = juce::jlimit (4, 10, (int) pThresh->load());
    if (thr != lastThresh) lastThresh = thr;

    const float feed = pFeed->load();
    const float spur = pSpur->load();
    const float expected = feed * feed * 3.0f;
    int grains = (int) expected + (rng.nextFloat() < expected - std::floor (expected) ? 1 : 0);

    auto drop = [this] (int v, int c, int amount)
    {
        if (locks[(size_t) v].load()) return;
        sand[(size_t) v][(size_t) c] += amount;
    };

    for (int i = 0; i < grains; ++i)
    {
        const int v = rng.nextInt (numVoices);
        const int c = rng.nextInt (lens[(size_t) v].load());
        drop (v, c, steps[(size_t) v][(size_t) c].load() > 0 ? 2 : 1);
    }

    if (lawineRequested.exchange (false))
    {
        const int c = rng.nextInt (numSteps);
        for (int v = 0; v < numVoices; ++v) drop (v, c % lens[(size_t) v].load(), thr);
        drop (rng.nextInt (numVoices), c, thr * 2);
    }

    int topples = 0;
    for (int iter = 0; iter < 4000; ++iter)
    {
        bool any = false;
        for (int v = 0; v < numVoices; ++v)
        {
            const int len = lens[(size_t) v].load();
            for (int c = 0; c < len; ++c)
            {
                if (sand[(size_t) v][(size_t) c] < thr) continue;
                any = true;
                ++topples;
                sand[(size_t) v][(size_t) c] -= 4;

                // a little sand blows away with every slide (bulk dissipation):
                // without it a pile this small keeps producing huge avalanches
                auto pass = [this] { return rng.nextFloat() >= 0.12f; };
                if (v > 0 && ! locks[(size_t) v - 1].load() && pass())
                    sand[(size_t) v - 1][(size_t) (c % lens[(size_t) v - 1].load())] += 1;
                if (v < numVoices - 1 && ! locks[(size_t) v + 1].load() && pass())
                    sand[(size_t) v + 1][(size_t) (c % lens[(size_t) v + 1].load())] += 1;
                // open edges in time as well: grains that slide past the first
                // or last step of a row are gone, which keeps the pile critical
                // instead of saturated
                if (c + 1 < len && pass()) sand[(size_t) v][(size_t) (c + 1)] += 1;
                if (c > 0 && pass()) sand[(size_t) v][(size_t) (c - 1)] += 1;

                if (rng.nextFloat() < 0.4f)
                    ghost[(size_t) v][(size_t) c] = juce::jmax (ghost[(size_t) v][(size_t) c], 0.45f + 0.15f * rng.nextFloat());

                if (spur > 0.0f && rng.nextFloat() < spur * 0.35f)
                    steps[(size_t) v][(size_t) c].store (steps[(size_t) v][(size_t) c].load() > 0 ? 0 : 1);
            }
        }
        if (! any) break;
    }

    int total = 0;
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
        {
            total += sand[(size_t) v][(size_t) s];
            sandVis[(size_t) v][(size_t) s].store (sand[(size_t) v][(size_t) s]);
            ghostVis[(size_t) v][(size_t) s].store (ghost[(size_t) v][(size_t) s]);
        }
    sandFill.store ((float) total / (float) (numVoices * numSteps * (thr - 1)));

    if (topples > 0)
    {
        lastAvalanche.store (topples);
        avalancheCounter.fetch_add (1);
    }
}

void TraunwerkProcessor::fire (int v, float vel, int kind, int depth, int offset)
{
    if (pTfOn->load() > 0.5f)
    {
        auto s = frayed (settings[(size_t) v]);
        if (kind == 4) s.decay *= 0.45f;        // ratchet hits are tight
        engine.trigger (v, vel, s, kind);
    }
    else
    {
        engine.trigger (v, vel, settings[(size_t) v], kind);
    }

    const float amt = pMyzel->load();
    if (depth >= 4 || amt <= 0.0f) return;

    const int div = juce::jlimit (0, 4, (int) pMyzelDiv->load());
    const double delay = samplesPer16th * myzelDivSixteenths[div];

    for (int j = 0; j < numVoices; ++j)
    {
        const float w = myzel[(size_t) v][(size_t) j].load();
        if (w <= 0.0f || j == v) continue;
        const float p = w * amt * std::pow (0.6f, (float) depth);
        if (rng.nextFloat() >= p || spores.size() >= 250) continue;
        const double d = delay * (1.0 + 0.03 * (rng.nextFloat() * 2.0 - 1.0)) * (depth + 1);
        spores.push_back ({ absSample + offset + (int64_t) d, j, vel * (0.45f + 0.4f * w), depth + 1, v });
    }
}

void TraunwerkProcessor::sequencerStep (int64_t k, int offset)
{
    sandStep();

    const float grooveAmt = pGroove->load();
    const float ghostLvl  = pGhost->load();
    const int slot = (int) (k % numSteps);

    const bool tf = pTfOn->load() > 0.5f;
    const float shuffle = pTfShuffle->load();
    const float stutter = pTfStutter->load();
    if (tf) updateBar (k / numSteps);   // no-op if the scheduler already moved on

    for (int v = 0; v < numVoices; ++v)
    {
        const int len = lens[(size_t) v].load();
        const int col = (int) (k % len);
        playCol[(size_t) v].store (col);

        // UMSCHICHTEN: read the pattern from somewhere else in the row
        int readCol = col;
        if (tf && ! isLocked (v))
        {
            readCol = (col + rotation[(size_t) v]) % len;
            if (rng.nextFloat() < shuffle * 0.2f) readCol = rng.nextInt (len);
        }

        const int st = steps[(size_t) v][(size_t) readCol].load();
        const float g = ghost[(size_t) v][(size_t) col];
        ghost[(size_t) v][(size_t) col] = 0.0f;
        ghostVis[(size_t) v][(size_t) col].store (0.0f);

        float vel = 0.0f; int kind = 0;
        if (st > 0)
        {
            vel = st == 2 ? 1.0f : 0.72f;
            vel *= 1.0f + grooveAmt * (grooveAccent[(size_t) slot].load() - 1.0f);
        }
        else if (g > 0.0f && ghostLvl > 0.0f)
        {
            vel = g * ghostLvl;
            kind = 1;
        }
        if (vel <= 0.01f) continue;
        vel = juce::jmin (1.0f, vel);
        fire (v, vel, kind, 0, offset);

        // STOTTERN: a burst of 2..6 hits inside this step, even, rushing or dragging
        if (tf && stutter > 0.0f && rng.nextFloat() < stutter * (st == 2 ? 0.5f : 0.3f))
        {
            const int n = 2 + rng.nextInt (stutter > 0.6f ? 5 : 3);
            const int mode = rng.nextInt (3);
            const bool rising = rng.nextBool();
            for (int i = 1; i < n && spores.size() < 250; ++i)
            {
                double t = (double) i / n;
                if (mode == 1) t = std::pow (t, 0.6);      // rushing into the next step
                if (mode == 2) t = std::pow (t, 1.7);      // bunched at the start
                const float vi = rising ? vel * (0.45f + 0.55f * (float) i / (float) n)
                                        : vel * std::pow (0.8f, (float) i);
                spores.push_back ({ absSample + offset + (int64_t) (t * samplesPer16th), v, vi, 4, -1 });
            }
            ratchetCounter.fetch_add (1);
        }
    }
}

void TraunwerkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;

    gatherSettings();
    engine.setKompost (pKompost->load());
    engine.setRecovery (pRecover->load());
    events.clear();

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int v = voiceForNote (msg.getNoteNumber());
            if (v >= 0) events.push_back ({ juce::jlimit (0, n - 1, meta.samplePosition), v, msg.getFloatVelocity(), 3, 0, 0 });
        }
    }
    midi.clear();

    if (const int mask = auditionMask.exchange (0))
        for (int v = 0; v < numVoices; ++v)
            if (mask & (1 << v)) events.push_back ({ 0, v, 0.85f, 3, 0, 0 });

    // ---- clock -------------------------------------------------------------
    bool hostPlaying = false;
    double bpm = pBpm->load();
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (auto p = pos->getPpqPosition()) { hostPlaying = true; ppq = *p; }
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    hostPlayingVis.store (hostPlaying);
    samplesPer16th = sr * 60.0 / bpm / 4.0;

    const bool internal = ! hostPlaying && pPlay->load() > 0.5f;
    if (internal)
    {
        if (! wasInternal) internalPpq = 0.0;
        ppq = internalPpq;
    }
    const bool running = hostPlaying || internal;

    if (running)
    {
        const double ppqPerSample = bpm / 60.0 / sr;
        const double end = ppq + n * ppqPerSample;
        if (! wasRunning || ppq < lastEndPpq - 1.0e-3 || ppq > lastEndPpq + 0.25)
        {
            lastStep = (int64_t) std::floor (ppq * 4.0 - 1.0e-9) - 1;
            lastStepT = -1.0e9;
            tfBar = -1;
        }

        for (int guard = 0; guard < 64; ++guard)
        {
            const int64_t k = lastStep + 1;
            if (k < 0) { lastStep = k; continue; }
            // keep the order of steps even when warp and groove pull hard
            const double T = juce::jmax (stepTime (k), lastStepT + 1.0e-4);
            if (T >= end) break;
            lastStepT = T;
            const int off = juce::jlimit (0, n - 1, (int) ((T - ppq) / ppqPerSample));
            events.push_back ({ off, -1, 0.0f, 0, 0, k });
            lastStep = k;
        }
        lastEndPpq = end;
        if (internal) internalPpq = end;
    }
    else if (wasRunning)
    {
        for (auto& c : playCol) c.store (-1);
    }
    wasRunning = running;
    wasInternal = internal;

    std::stable_sort (events.begin(), events.end(),
                      [] (const Event& a, const Event& b) { return a.offset < b.offset; });

    // ---- render, interleaving events and due spores --------------------------
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    size_t ei = 0;

    for (int guard = 0; guard < 2048; ++guard)
    {
        // earliest due spore in this block
        int sporeIdx = -1; int64_t sporeDue = absSample + n;
        for (size_t i = 0; i < spores.size(); ++i)
            if (spores[i].due < sporeDue) { sporeDue = spores[i].due; sporeIdx = (int) i; }
        const int sporeOff = sporeIdx >= 0 ? (int) juce::jmax ((int64_t) pos, sporeDue - absSample) : n;
        const int eventOff = ei < events.size() ? events[ei].offset : n;

        const int next = juce::jmin (sporeOff, eventOff);
        if (next >= n) break;

        if (next > pos)
        {
            engine.render (L + pos, R + pos, next - pos, settings);
            pos = next;
        }

        if (eventOff <= sporeOff)
        {
            const auto e = events[ei++];
            if (e.voice < 0) sequencerStep (e.step, pos);
            else fire (e.voice, e.vel, e.kind, e.depth, pos);
        }
        else
        {
            const auto s = spores[(size_t) sporeIdx];
            spores.erase (spores.begin() + sporeIdx);
            fire (s.voice, s.vel, s.from < 0 ? 4 : 2, s.depth, pos);
            if (s.from >= 0)
            {
                const auto scope = sporeFifo.write (1);
                if (scope.blockSize1 > 0) sporeBuf[(size_t) scope.startIndex1] = { s.from, s.voice };
            }
        }
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos, settings);

    // ---- master ------------------------------------------------------------
    const float drive = pDrive->load();
    const float master = pMaster->load();
    const float g = 1.0f + drive * 5.0f;
    const float comp = 1.0f / std::sqrt (g);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            d[i] = (drive > 0.001f ? std::tanh (d[i] * g) * comp : d[i]) * master;
    }
    juce::dsp::AudioBlock<float> block (buffer);
    limiter.process (juce::dsp::ProcessContextReplacing<float> (block));

    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    absSample += n;
}

//==============================================================================
void TraunwerkProcessor::clearPattern()
{
    for (int v = 0; v < numVoices; ++v)
        if (! isLocked (v))
            for (int s = 0; s < numSteps; ++s) setStep (v, s, 0);
}

void TraunwerkProcessor::setSourceKeepAlive (int v, SourcePtr src)
{
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const SourcePtr& p) { return p.use_count() <= 1; }),
                     keepAlive.end());
    keepAlive.push_back (src);
    engine.setSource (v, std::move (src));
}

bool TraunwerkProcessor::readFileMono (const juce::File& file, double maxSeconds,
                                       juce::AudioBuffer<float>& out, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;

    rate = reader->sampleRate;
    const int len = (int) juce::jmin ((juce::int64) (maxSeconds * rate), reader->lengthInSamples);
    const int chans = (int) juce::jmax (1u, reader->numChannels);
    juce::AudioBuffer<float> tmp (chans, len);
    reader->read (&tmp, 0, len, 0, true, chans > 1);

    out.setSize (1, len);
    out.clear();
    for (int c = 0; c < chans; ++c) out.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) chans);
    return true;
}

bool TraunwerkProcessor::loadVoiceSample (int v, const juce::File& file)
{
    juce::AudioBuffer<float> mono; double rate = 48000.0;
    if (! readFileMono (file, 2.5, mono, rate)) return false;

    // trim leading silence so ANSCHLAG lands on the transient
    const float pk = mono.getMagnitude (0, 0, mono.getNumSamples());
    if (pk <= 1.0e-5f) return false;
    const float* d = mono.getReadPointer (0);
    int start = 0;
    while (start < mono.getNumSamples() - 32 && std::abs (d[start]) < pk * 0.02f) ++start;
    start = juce::jmax (0, start - 16);

    auto src = std::make_shared<DrumSource>();
    src->data.setSize (1, mono.getNumSamples() - start);
    src->data.copyFrom (0, 0, mono, 0, start, mono.getNumSamples() - start);
    src->data.applyGain (0.9f / pk);
    src->rate = rate;
    src->name = file.getFileNameWithoutExtension();
    voiceFiles[(size_t) v] = file;
    setSourceKeepAlive (v, src);
    return true;
}

void TraunwerkProcessor::resetVoiceSample (int v)
{
    voiceFiles[(size_t) v] = juce::File();
    setSourceKeepAlive (v, builtIn[(size_t) v]);
}

juce::String TraunwerkProcessor::getVoiceSourceName (int v) const
{
    auto s = engine.getSource (v);
    return s != nullptr ? s->name : juce::String();
}

void TraunwerkProcessor::installAbdruck (const AbdruckResult& r, bool setPattern)
{
    for (int v = 0; v < numVoices; ++v)
    {
        if (! r.hasSlice[(size_t) v] || isLocked (v)) continue;
        auto src = std::make_shared<DrumSource>();
        src->data = r.slices[(size_t) v];
        src->rate = abdruckRate;
        src->name = "~" + abdruckFile.getFileNameWithoutExtension();
        voiceFiles[(size_t) v] = juce::File();
        setSourceKeepAlive (v, src);
    }

    if (r.hasGroove)
    {
        for (int s = 0; s < numSteps; ++s)
        {
            grooveOffset[(size_t) s].store (r.offsets[(size_t) s]);
            grooveAccent[(size_t) s].store (r.accents[(size_t) s]);
        }
        if (setPattern)
        {
            for (int v = 0; v < numVoices; ++v)
            {
                if (isLocked (v)) continue;
                setLength (v, numSteps);
                for (int s = 0; s < numSteps; ++s) setStep (v, s, r.steps[(size_t) v][(size_t) s]);
            }
            if (pGroove->load() < 0.05f) setParam (id::groove, 0.6f);
        }
    }

    abdruckInfo = juce::String (r.numOnsets) + " onsets"
                + (r.hasGroove ? juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (r.bpm, 1) + " bpm" : juce::String());
}

bool TraunwerkProcessor::loadAbdruck (const juce::File& file)
{
    juce::AudioBuffer<float> mono; double rate = 48000.0;
    if (! readFileMono (file, 60.0, mono, rate)) return false;
    const auto r = Abdruck::analyse (mono, rate);
    if (! r.valid) return false;
    abdruckFile = file;
    abdruckRate = rate;
    installAbdruck (r, true);
    presetChangeCount.fetch_add (1);
    return true;
}

//==============================================================================
Snapshot TraunwerkProcessor::capture() const
{
    Snapshot s;
    for (int v = 0; v < numVoices; ++v)
    {
        for (int i = 0; i < numSteps; ++i) s.steps[(size_t) (v * numSteps + i)] = (int8_t) getStep (v, i);
        s.lens[(size_t) v] = (int8_t) getLength (v);
        for (int j = 0; j < numVoices; ++j) s.myzel[(size_t) (v * numVoices + j)] = getMyzel (v, j);
        for (int p = 0; p < numVoiceParams; ++p)
            s.params[(size_t) (v * numVoiceParams + p)] = apvts.getParameter (voiceParamId (v, p))->getValue();
    }
    return s;
}

void TraunwerkProcessor::applySnapshot (const Snapshot& s)
{
    for (int v = 0; v < numVoices; ++v)
    {
        for (int i = 0; i < numSteps; ++i) setStep (v, i, s.steps[(size_t) (v * numSteps + i)]);
        setLength (v, s.lens[(size_t) v]);
        for (int j = 0; j < numVoices; ++j) setMyzel (v, j, s.myzel[(size_t) (v * numVoices + j)]);
        for (int p = 0; p < numVoiceParams; ++p)
        {
            auto* prm = apvts.getParameter (voiceParamId (v, p));
            const float target = s.params[(size_t) (v * numVoiceParams + p)];
            if (std::abs (prm->getValue() - target) > 1.0e-5f) prm->setValueNotifyingHost (target);
        }
    }
}

void TraunwerkProcessor::randomize()
{
    const float amt = pRndAmt->load();
    const int target = juce::jlimit (0, 2, (int) pRndTarget->load());
    const bool doPattern = target == 0 || target == 2;
    const bool doSound   = target == 1 || target == 2;

    const auto before = capture();
    const int nearest = juce::jlimit (0, juce::jmax (0, (int) trail.size() - 1), juce::roundToInt (trailPos));
    if (trail.empty() || ! trail[(size_t) nearest].approxEquals (before))
        trail.push_back (before);

    juce::Random r;
    for (int v = 0; v < numVoices; ++v)
    {
        if (isLocked (v)) continue;

        if (doPattern)
        {
            for (int s = 0; s < numSteps; ++s)
                if (r.nextFloat() < amt)
                    setStep (v, s, r.nextFloat() < stepTemplates[v][s] ? (r.nextFloat() < 0.2f ? 2 : 1) : 0);

            if (v >= 3 && r.nextFloat() < amt * 0.25f)
            {
                static const int lensChoice[] { 16, 16, 16, 12, 15, 14, 13, 10, 9, 7, 5 };
                setLength (v, lensChoice[r.nextInt (11)]);
            }
        }

        if (doSound)
        {
            for (int p = 0; p < numVoiceParams; ++p)
            {
                auto* prm = apvts.getParameter (voiceParamId (v, p));
                const float cur = prm->getValue();
                float rnd = r.nextFloat();
                if (p == pLevel) rnd = juce::jlimit (0.0f, 1.0f, 0.55f + 0.4f * rnd);
                if (p == pPan)   rnd = 0.5f + (rnd - 0.5f) * 0.6f;
                if (p == pPitch) rnd = 0.5f + (rnd - 0.5f) * 0.35f;
                if (p == pBite)  rnd = 0.35f + 0.6f * rnd;
                const float k = p == pLevel ? amt * 0.3f : amt * 0.7f;
                prm->setValueNotifyingHost (cur + (rnd - cur) * k);
            }
        }
    }

    if (target == 2)
        for (int i = 0; i < numVoices; ++i)
            for (int j = 0; j < numVoices; ++j)
                if (i != j && r.nextFloat() < amt * 0.3f)
                {
                    static const float ws[] { 0.0f, 0.0f, 0.3f, 0.55f, 0.85f };
                    setMyzel (i, j, ws[r.nextInt (5)]);
                }

    trail.push_back (capture());
    while (trail.size() > 64) trail.erase (trail.begin());
    trailPos = (float) (trail.size() - 1);
}

void TraunwerkProcessor::scrubTrail (float pos)
{
    if (trail.empty()) return;
    trailPos = juce::jlimit (0.0f, (float) (trail.size() - 1), pos);
    const int a = (int) std::floor (trailPos);
    const int b = juce::jmin ((int) trail.size() - 1, a + 1);
    const float f = trailPos - (float) a;

    const auto& A = trail[(size_t) a];
    const auto& B = trail[(size_t) b];
    Snapshot m = f < 0.5f ? A : B;
    for (size_t i = 0; i < m.params.size(); ++i) m.params[i] = A.params[i] + (B.params[i] - A.params[i]) * f;
    for (size_t i = 0; i < m.myzel.size(); ++i)  m.myzel[i]  = A.myzel[i]  + (B.myzel[i]  - A.myzel[i])  * f;

    // locked voices keep what they have now
    const auto now = capture();
    for (int v = 0; v < numVoices; ++v)
        if (isLocked (v))
        {
            for (int s = 0; s < numSteps; ++s) m.steps[(size_t) (v * numSteps + s)] = now.steps[(size_t) (v * numSteps + s)];
            m.lens[(size_t) v] = now.lens[(size_t) v];
            for (int p = 0; p < numVoiceParams; ++p) m.params[(size_t) (v * numVoiceParams + p)] = now.params[(size_t) (v * numVoiceParams + p)];
        }
    applySnapshot (m);
}

//==============================================================================
int TraunwerkProcessor::getNumPrograms() { return (int) Presets::all().size(); }

const juce::String TraunwerkProcessor::getProgramName (int index)
{
    const auto& all = Presets::all();
    return juce::isPositiveAndBelow (index, (int) all.size()) ? juce::String (all[(size_t) index].name) : juce::String();
}

void TraunwerkProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms())) applyPreset (index);
}

void TraunwerkProcessor::applyPreset (int index)
{
    const auto& all = Presets::all();
    if (! juce::isPositiveAndBelow (index, (int) all.size())) return;
    const auto& p = all[(size_t) index];
    currentProgram = index;

    for (int v = 0; v < numVoices; ++v)
        for (int q = 0; q < numVoiceParams; ++q)
            setParam (voiceParamId (v, q), voiceDefault (v, q));

    for (auto* pid : { id::master, id::drive, id::swing, id::groove, id::feed, id::thresh, id::spur,
                       id::ghost, id::myzel, id::myzelDiv, id::kompost, id::recover,
                       id::tfOn, id::tfWarp, id::tfStutter, id::tfShuffle, id::tfFray })
        if (auto* prm = apvts.getParameter (pid))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getDefaultValue());
            prm->endChangeGesture();
        }

    setParam (id::bpm, p.bpm);

    for (int v = 0; v < numVoices; ++v)
    {
        const juce::String pat (p.pattern[v]);
        setLength (v, pat.isEmpty() ? numSteps : juce::jmin (numSteps, pat.length()));
        for (int s = 0; s < numSteps; ++s)
        {
            const auto ch = s < pat.length() ? pat[s] : '.';
            setStep (v, s, ch == 'X' ? 2 : ch == 'x' ? 1 : 0);
        }
        for (int j = 0; j < numVoices; ++j) setMyzel (v, j, 0.0f);
    }

    for (auto& [v, q, val] : p.voice)   setParam (voiceParamId (v, q), val);
    for (auto& [pid, val] : p.globals)  setParam (pid, val);
    for (auto& [a, b, w] : p.myzel)     setMyzel (a, b, w);

    presetChangeCount.fetch_add (1);
}

//==============================================================================
void TraunwerkProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree twState ("TW");

    juce::String st;
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s) st << juce::String (getStep (v, s));
    twState.setProperty ("steps", st, nullptr);

    juce::StringArray ln, lk, my;
    for (int v = 0; v < numVoices; ++v)
    {
        ln.add (juce::String (getLength (v)));
        lk.add (isLocked (v) ? "1" : "0");
        for (int j = 0; j < numVoices; ++j) my.add (juce::String (getMyzel (v, j), 3));
        twState.setProperty ("sample" + juce::String (v), voiceFiles[(size_t) v].getFullPathName(), nullptr);
    }
    twState.setProperty ("lens", ln.joinIntoString (","), nullptr);
    twState.setProperty ("locks", lk.joinIntoString (","), nullptr);
    twState.setProperty ("myzel", my.joinIntoString (","), nullptr);

    float go[numSteps], ga[numSteps];
    for (int s = 0; s < numSteps; ++s) { go[s] = grooveOffset[(size_t) s].load(); ga[s] = grooveAccent[(size_t) s].load(); }
    twState.setProperty ("grooveOff", csv (go, numSteps), nullptr);
    twState.setProperty ("grooveAcc", csv (ga, numSteps), nullptr);
    twState.setProperty ("abdruck", abdruckFile.getFullPathName(), nullptr);
    twState.setProperty ("program", currentProgram, nullptr);
    twState.setProperty ("selected", selectedVoice, nullptr);

    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).hasType ("TW")) state.removeChild (i, nullptr);
    state.addChild (twState, -1, nullptr);

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TraunwerkProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;

    auto twState = state.getChildWithName ("TW");
    state.removeChild (twState, nullptr);
    apvts.replaceState (state);
    if (! twState.isValid()) return;

    const juce::String st = twState["steps"].toString();
    for (int v = 0; v < numVoices; ++v)
        for (int s = 0; s < numSteps; ++s)
        {
            const int i = v * numSteps + s;
            setStep (v, s, i < st.length() ? st[i] - '0' : 0);
        }

    juce::StringArray ln, lk, my, go, ga;
    ln.addTokens (twState["lens"].toString(), ",", "");
    lk.addTokens (twState["locks"].toString(), ",", "");
    my.addTokens (twState["myzel"].toString(), ",", "");
    go.addTokens (twState["grooveOff"].toString(), ",", "");
    ga.addTokens (twState["grooveAcc"].toString(), ",", "");

    // ABDRUCK first, then individual samples on top
    const juce::File abd (twState["abdruck"].toString());
    if (abd.existsAsFile())
    {
        juce::AudioBuffer<float> mono; double rate = 48000.0;
        if (readFileMono (abd, 60.0, mono, rate))
        {
            const auto r = Abdruck::analyse (mono, rate);
            if (r.valid) { abdruckFile = abd; abdruckRate = rate; installAbdruck (r, false); }
        }
    }

    for (int v = 0; v < numVoices; ++v)
    {
        if (v < ln.size()) setLength (v, ln[v].getIntValue());
        if (v < lk.size()) setLocked (v, lk[v] == "1");
        for (int j = 0; j < numVoices; ++j)
        {
            const int i = v * numVoices + j;
            if (i < my.size()) setMyzel (v, j, my[i].getFloatValue());
        }
        const juce::File f (twState["sample" + juce::String (v)].toString());
        if (f.existsAsFile()) loadVoiceSample (v, f);
    }
    for (int s = 0; s < numSteps; ++s)
    {
        if (s < go.size()) grooveOffset[(size_t) s].store (go[s].getFloatValue());
        if (s < ga.size()) grooveAccent[(size_t) s].store (ga[s].getFloatValue());
    }

    currentProgram = (int) twState.getProperty ("program", currentProgram);
    selectedVoice  = juce::jlimit (0, numVoices - 1, (int) twState.getProperty ("selected", 0));
    presetChangeCount.fetch_add (1);
}

juce::AudioProcessorEditor* TraunwerkProcessor::createEditor()
{
    return new TraunwerkEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TraunwerkProcessor();
}
