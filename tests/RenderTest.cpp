// Offline render test for TRAUNWERK.
// Usage: TraunwerkRender <outDir>
// Renders every preset through a simulated host transport, checks levels and
// stability, measures each novel feature against a control, writes demo WAVs
// and an editor screenshot.

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

using namespace tw;

namespace
{
    struct TestHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setPpqPosition (ppq);
            p.setIsPlaying (playing);
            return p;
        }
    };

    constexpr double sr = 48000.0;
    constexpr int block = 512;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    }

    struct Counts { int pattern = 0, ghost = 0, spore = 0, midi = 0, ratchet = 0; std::vector<std::pair<int, int64_t>> times; };

    void drainHits (TraunwerkProcessor& p, Counts& c)
    {
        auto& f = p.engine.hitFifo;
        const auto s = f.read (f.getNumReady());
        auto h = [&] (int st, int n)
        {
            for (int i = st; i < st + n; ++i)
            {
                const int k = p.engine.hitBuf[(size_t) i].kind;
                if (k == 0) c.times.push_back ({ p.engine.hitBuf[(size_t) i].voice, p.engine.hitBuf[(size_t) i].time });
                (k == 0 ? c.pattern : k == 1 ? c.ghost : k == 2 ? c.spore : k == 4 ? c.ratchet : c.midi)++;
            }
        };
        h (s.startIndex1, s.blockSize1);
        h (s.startIndex2, s.blockSize2);
        p.engine.grainFifo.reset();
    }

    /** Renders `seconds` with the transport running; returns stereo audio. */
    juce::AudioBuffer<float> render (TraunwerkProcessor& p, TestHead& head, double seconds, Counts* counts = nullptr,
                                     double* cpuFactor = nullptr, bool drain = true)
    {
        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        Counts local;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            buf.setSize (2, n, false, false, true);
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
            head.ppq += n * head.bpm / 60.0 / sr;
            if (drain) drainHits (p, counts ? *counts : local);
        }
        if (cpuFactor) *cpuFactor = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / seconds;
        return out;
    }

    void writeWav (const juce::AudioBuffer<float>& b, const juce::File& f)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    float rms (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
    {
        if (len < 0) len = b.getNumSamples() - start;
        return 0.5f * (b.getRMSLevel (0, start, len) + b.getRMSLevel (1, start, len));
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
    }

    /** Energy above ~6 kHz (first difference, squared) relative to total. */
    float brightness (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double hf = 0.0, all = 0.0;
        const float* d = b.getReadPointer (0);
        for (int i = start + 1; i < start + len; ++i)
        {
            const double df = d[i] - d[i - 1];
            hf += df * df; all += (double) d[i] * d[i];
        }
        return all > 0.0 ? (float) (hf / all) : 0.0f;
    }

    void prep (TraunwerkProcessor& p, TestHead& h, int preset)
    {
        p.applyPreset (preset);
        h.ppq = 0.0;
        h.bpm = Presets::all()[(size_t) preset].bpm;
        h.playing = true;
        p.prepareToPlay (sr, block);
    }

    juce::String safeName (const juce::String& s) { return s.replaceCharacter (' ', '_').toLowerCase(); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getChildFile ("render"));
    outDir.createDirectory();

    TestHead head;
    auto proc = std::make_unique<TraunwerkProcessor>();
    proc->setPlayHead (&head);
    proc->setPlayConfigDetails (0, 2, sr, block);

    // ---------------------------------------------------------------------
    std::printf ("\n== presets ==\n");
    const auto& presets = Presets::all();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        prep (*proc, head, i);
        const double bars = 8.0;
        Counts c; double cpu = 0.0;
        auto out = render (*proc, head, bars * 4.0 * 60.0 / head.bpm, &c, &cpu);
        const float pk = out.getMagnitude (0, out.getNumSamples());
        const float r = rms (out);
        std::printf ("%-22s peak %.3f  rms %.4f (%.1f dBFS)  hits %d/%d/%d  cpu %.1f%%\n",
                     presets[(size_t) i].name, pk, r, juce::Decibels::gainToDecibels (r), c.pattern, c.ghost, c.spore, cpu * 100.0);
        check (finite (out), "finite");
        check (pk <= 1.0f && pk > 0.2f, "peak in range");
        check (r > 0.02f, "audible");
        check (cpu < 0.5, "cpu below 50% of one core");
        writeWav (out, outDir.getChildFile (juce::String (i).paddedLeft ('0', 2) + "_" + safeName (presets[(size_t) i].name) + ".wav"));
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== voices via MIDI (transport stopped) ==\n");
    proc->applyPreset (0);
    head.playing = false;
    proc->prepareToPlay (sr, block);
    for (int v = 0; v < numVoices; ++v)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, gmNotes[v], (juce::uint8) 110), 10);
        float pk = 0.0f; int lastLoud = 0;
        for (int b = 0; b < 300; ++b)
        {
            proc->processBlock (buf, midi);
            midi.clear();
            const float m = buf.getMagnitude (0, block);
            pk = juce::jmax (pk, m);
            if (m > 0.001f) lastLoud = b;
        }
        std::printf ("  %-7s peak %.3f  length %.2f s\n", voiceNames[v], pk, (lastLoud + 1) * block / sr);
        check (pk > 0.1f, juce::String (voiceNames[v]) + " sounds");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== ANSCHLAG keeps the transient ==\n");
    {
        auto attackPeak = [&] (float attackMs)
        {
            proc->applyPreset (0);
            proc->setParam (voiceParamId (1, pAttack), attackMs);
            proc->setParam (voiceParamId (1, pSpray), 0.6f);
            proc->setParam (voiceParamId (1, pWear), 0.0f);
            proc->prepareToPlay (sr, block);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 120), 0);
            proc->processBlock (buf, midi);
            // peak in the first 3 ms relative to the following 30 ms
            const float early = buf.getMagnitude (0, 0, (int) (0.003 * sr));
            const float later = buf.getRMSLevel (0, (int) (0.003 * sr), (int) (0.007 * sr));
            return early / juce::jmax (1.0e-6f, later);
        };
        const float with = attackPeak (8.0f), without = attackPeak (0.0f);
        std::printf ("  crest first 3 ms: attack 8 ms %.2f vs 0 ms %.2f\n", with, without);
        check (with > without * 1.2f, "ANSCHLAG gives a sharper onset");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== VERWITTERUNG darkens a roll and recovers ==\n");
    {
        auto roll = [&] (float wear, float& first, float& last, float& recovered)
        {
            proc->applyPreset (0);
            proc->clearPattern();
            for (int s = 0; s < numSteps; ++s) proc->setStep (1, s, 1);
            proc->setParam (voiceParamId (1, pWear), wear);
            proc->setParam (id::recover, 1.0f);
            proc->setParam (id::feed, 0.0f);
            proc->prepareToPlay (sr, block);
            head.ppq = 0.0; head.bpm = 120.0; head.playing = true;
            auto a = render (*proc, head, 4.0);
            const int step = (int) (sr * 0.125);
            first = brightness (a, step * 8, step / 2);
            last  = brightness (a, step * 30, step / 2);
            // stop, rest 3 s, single hit
            for (int s = 0; s < numSteps; ++s) proc->setStep (1, s, 0);
            render (*proc, head, 3.0);
            proc->setStep (1, 0, 1);
            head.ppq = std::ceil (head.ppq / 4.0) * 4.0;
            auto b = render (*proc, head, 0.12);
            recovered = brightness (b, 0, step / 2);
        };
        float f0, l0, r0, f1, l1, r1;
        roll (0.0f, f0, l0, r0);
        roll (1.0f, f1, l1, r1);
        std::printf ("  wear 0: first %.4f last %.4f | wear 1: first %.4f last %.4f recovered %.4f\n", f0, l0, f1, l1, r1);
        check (l1 < l0 * 0.4f, "a worn roll is much darker than an unworn one");
        check (std::abs (l0 - f0) < f0 * 0.4f, "without wear it stays within normal grain variation");
        check (r1 > l1 * 1.3f, "after a rest it recovers");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== LAWINE ==\n");
    {
        auto ghosts = [&] (float feed, int& avalanches)
        {
            prep (*proc, head, 0);
            proc->setParam (id::feed, feed);
            Counts c;
            const int a0 = proc->getAvalancheCount();
            render (*proc, head, 16.0, &c);
            avalanches = proc->getAvalancheCount() - a0;
            return c.ghost;
        };
        int a0, a1;
        const int g0 = ghosts (0.0f, a0), g1 = ghosts (0.6f, a1);
        std::printf ("  feed 0: %d ghosts, %d avalanches | feed 0.6: %d ghosts, %d avalanches\n", g0, a0, g1, a1);
        check (g0 == 0 || a0 <= 2, "no feed, (almost) no avalanches");
        check (g1 > 10 && a1 > 5, "feed produces avalanches and ghost notes");

        // avalanche size distribution: many small, few big (heavy tail)
        prep (*proc, head, 0);
        proc->setParam (id::feed, 0.6f);
        std::vector<int> sizes;
        int lastCount = proc->getAvalancheCount();
        for (int i = 0; i < 1200; ++i)
        {
            render (*proc, head, 0.125);
            if (proc->getAvalancheCount() != lastCount) { sizes.push_back (proc->getLastAvalanche()); lastCount = proc->getAvalancheCount(); }
        }
        int small = 0, big = 0, maxS = 0;
        for (int s : sizes) { if (s <= 2) ++small; if (s >= 12) ++big; maxS = juce::jmax (maxS, s); }
        std::printf ("  %d avalanches: %d small (<=2), %d big (>=12), largest %d\n", (int) sizes.size(), small, big, maxS);
        check (small > big * 2 && big > 0, "heavy-tailed: mostly small, sometimes big");

        // SCHUTZ: a locked voice keeps its pattern under erosion
        prep (*proc, head, 1);
        proc->setParam (id::feed, 0.8f);
        proc->setParam (id::spur, 1.0f);
        proc->setLocked (0, true);
        std::array<int, numSteps> kick {};
        for (int s = 0; s < numSteps; ++s) kick[(size_t) s] = proc->getStep (0, s);
        render (*proc, head, 16.0);
        bool same = true, changed = false;
        for (int s = 0; s < numSteps; ++s)
        {
            if (proc->getStep (0, s) != kick[(size_t) s]) same = false;
        }
        const auto& pat = Presets::all()[1].pattern[3];
        for (int s = 0; s < numSteps; ++s) if (proc->getStep (3, s) != (pat[s] == 'x' ? 1 : pat[s] == 'X' ? 2 : 0)) changed = true;
        check (same, "locked KICK untouched by erosion");
        check (changed, "unlocked HAT eroded");
        proc->setLocked (0, false);
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== MYZEL ==\n");
    {
        auto spores = [&] (float amount)
        {
            prep (*proc, head, 6);
            proc->setParam (id::myzel, amount);
            Counts c;
            render (*proc, head, 16.0, &c);
            return c;
        };
        const auto c0 = spores (0.0f), c1 = spores (0.85f);
        std::printf ("  amount 0: %d pattern / %d spores | 0.85: %d pattern / %d spores\n", c0.pattern, c0.spore, c1.pattern, c1.spore);
        check (c0.spore == 0, "no spores at 0");
        check (c1.spore > c1.pattern / 2, "the network adds a large share of hits of its own");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== KOMPOST ==\n");
    {
        auto run = [&] (float amt)
        {
            prep (*proc, head, 7);
            proc->setParam (id::kompost, amt);
            return render (*proc, head, 12.0);
        };
        const auto a = run (0.0f), b = run (0.9f);
        const int start = (int) (sr * 8.0), len = (int) (sr * 4.0);
        juce::AudioBuffer<float> d (1, len);
        d.copyFrom (0, 0, a, 0, start, len);
        d.addFrom (0, 0, b, 0, start, len, -1.0f);
        const float diff = d.getRMSLevel (0, 0, len) / juce::jmax (1.0e-6f, a.getRMSLevel (0, start, len));
        std::printf ("  late difference kompost 0 vs 0.9: %.2f of signal rms, level %.4f vs %.4f\n",
                     diff, rms (a, start, len), rms (b, start, len));
        check (diff > 0.3f, "compost changes the sound");
        check (finite (b) && b.getMagnitude (0, b.getNumSamples()) <= 1.0f, "compost stays stable");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== BROTKRUMEN ==\n");
    {
        prep (*proc, head, 1);
        std::array<int, numVoices * numSteps> orig {};
        for (int v = 0; v < numVoices; ++v) for (int s = 0; s < numSteps; ++s) orig[(size_t) (v * numSteps + s)] = proc->getStep (v, s);
        const float decay0 = proc->apvts.getRawParameterValue (voiceParamId (3, pDecay))->load();

        proc->setParam (id::rndTarget, 2.0f);
        proc->setParam (id::rndAmt, 0.7f);
        for (int i = 0; i < 5; ++i) proc->randomize();
        std::printf ("  trail size %d, position %.1f\n", proc->getTrailSize(), proc->getTrailPos());
        check (proc->getTrailSize() == 6, "every throw leaves a crumb (plus the start)");

        int diffSteps = 0;
        for (int v = 0; v < numVoices; ++v) for (int s = 0; s < numSteps; ++s)
            if (proc->getStep (v, s) != orig[(size_t) (v * numSteps + s)]) ++diffSteps;
        check (diffSteps > 10, "randomised pattern differs (" + juce::String (diffSteps) + " steps)");

        proc->scrubTrail (0.0f);
        bool back = true;
        for (int v = 0; v < numVoices; ++v) for (int s = 0; s < numSteps; ++s)
            if (proc->getStep (v, s) != orig[(size_t) (v * numSteps + s)]) back = false;
        const float decayBack = proc->apvts.getRawParameterValue (voiceParamId (3, pDecay))->load();
        check (back && std::abs (decayBack - decay0) < 1.0e-3f, "scrubbing to the start restores pattern and sound");

        proc->scrubTrail (0.5f);
        const float mid = proc->apvts.getRawParameterValue (voiceParamId (3, pDecay))->load();
        proc->scrubTrail (1.0f);
        const float one = proc->apvts.getRawParameterValue (voiceParamId (3, pDecay))->load();
        std::printf ("  HAT decay: crumb0 %.3f  half-way %.3f  crumb1 %.3f\n", decay0, mid, one);
        check (std::abs (one - decay0) < 1.0e-4f || (mid - decay0) * (one - mid) >= -1.0e-6f, "half-way morphs between crumbs");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== ABDRUCK (from our own render) ==\n");
    {
        const auto f = outDir.getChildFile ("01_moos_minimal.wav");
        prep (*proc, head, 0);
        const bool ok = proc->loadAbdruck (f);
        std::printf ("  %s: %s\n", ok ? "loaded" : "failed", proc->getAbdruckInfo().toRawUTF8());
        check (ok, "Abdruck analyses a drum loop");
        juce::String offs;
        for (int s = 0; s < numSteps; ++s) offs << juce::String (proc->getGrooveOffset (s), 2) << " ";
        std::printf ("  groove offsets: %s\n", offs.toRawUTF8());
        for (int v = 0; v < numVoices; ++v)
        {
            juce::String row;
            for (int s = 0; s < numSteps; ++s) row << (proc->getStep (v, s) == 2 ? "X" : proc->getStep (v, s) ? "x" : ".");
            std::printf ("  %-7s %s  <- %s\n", voiceNames[v], row.toRawUTF8(), proc->getVoiceSourceName (v).toRawUTF8());
        }
        int quarters = 0;
        for (int s = 0; s < numSteps; s += 4) if (proc->getStep (0, s) > 0) ++quarters;
        check (quarters >= 2 && proc->getStep (0, 0) > 0, "KICK skeleton: downbeat plus at least one more quarter");
        check (std::abs (proc->getAbdruckInfo().fromLastOccurrenceOf ("\xc2\xb7", false, false).getFloatValue() - 124.0f) < 0.6f,
               "tempo within 0.5 %");
        head.ppq = 0.0; head.bpm = 124.0;
        proc->prepareToPlay (sr, block);
        auto out = render (*proc, head, 8 * 4 * 60.0 / 124.0);
        writeWav (out, outDir.getChildFile ("09_abdruck_of_moos_minimal.wav"));
        check (finite (out) && rms (out) > 0.02f, "the imprinted kit plays");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== ABDRUCK groove from a swung loop ==\n");
    {
        prep (*proc, head, 0);
        proc->setParam (id::swing, 0.2f);
        for (int s = 0; s < numSteps; ++s) proc->setStep (3, s, 1);   // 16th hats
        auto out = render (*proc, head, 8 * 4 * 60.0 / 120.0);
        const auto f = outDir.getChildFile ("swung_init.wav");
        writeWav (out, f);
        prep (*proc, head, 0);
        check (proc->loadAbdruck (f), "loaded");
        float odd = 0.0f, even = 0.0f;
        for (int s = 0; s < numSteps; ++s) (s & 1 ? odd : even) += proc->getGrooveOffset (s) / 8.0f;
        std::printf ("  %s, mean offset even %.3f odd %.3f (swing was 0.20)\n", proc->getAbdruckInfo().toRawUTF8(), even, odd);
        check (odd - even > 0.08f && odd - even < 0.3f, "swing direction and size recovered");
        f.deleteFile();
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== TRAUNFALL ==\n");
    {
        // onsets of a single, isolated voice: first sample above a threshold after silence
        auto setup = [&] (bool on, float warp, float stutter, float shuffle, float fray)
        {
            prep (*proc, head, 0);
            proc->clearPattern();
            proc->setParam (id::feed, 0.0f);
            proc->setParam (id::tfOn, on ? 1.0f : 0.0f);
            proc->setParam (id::tfWarp, warp);
            proc->setParam (id::tfStutter, stutter);
            proc->setParam (id::tfShuffle, shuffle);
            proc->setParam (id::tfFray, fray);
            proc->setParam (id::drive, 0.0f);
            for (int v = 0; v < numVoices; ++v) proc->setParam (voiceParamId (v, pWear), 0.0f);
        };

        // WARP: 16th hats get uneven, but every bar still starts on time.
        // Uses the engine's own trigger clock, so it is sample-exact.
        auto hatRun = [&] (bool on, float warp, double& spread, double& barErr)
        {
            setup (on, warp, 0.0f, 0.0f, 0.0f);
            for (int st = 0; st < numSteps; ++st) proc->setStep (3, st, 1);
            Counts c;
            render (*proc, head, 8.0, &c);
            const double p16 = sr * 60.0 / 120.0 / 4.0;
            std::vector<int64_t> t;
            for (auto& [v, tt] : c.times) if (v == 3) t.push_back (tt);
            double mn = 1e18, mx = 0; barErr = 0.0;
            for (size_t i = 1; i < t.size(); ++i) { mn = juce::jmin (mn, (double) (t[i] - t[i - 1])); mx = juce::jmax (mx, (double) (t[i] - t[i - 1])); }
            for (size_t i = 0; i < t.size(); i += 16)
                barErr = juce::jmax (barErr, std::abs ((double) (t[i] - t[0]) - (double) i * p16) / sr * 1000.0);
            spread = (mx - mn) / p16;
            return (int) t.size();
        };
        double s0, e0, s1, e1;
        const int n0 = hatRun (false, 0.0f, s0, e0);
        const int n1 = hatRun (true, 1.0f, s1, e1);
        std::printf ("  hats: off %d hits, interval spread %.3f 16ths | warp 1: %d hits, spread %.2f 16ths, downbeat error %.3f ms\n",
                     n0, s0, n1, s1, e1);
        check (s0 < 0.01, "off: a straight grid");
        check (s1 > 0.5, "warp: time stretches and rushes inside the bar");
        check (e1 < 0.05, "warp: downbeats stay on the host grid");
        check (n0 == n1, "warp: no steps lost or doubled");

        // STOTTERN
        Counts c0, c1;
        setup (true, 0.0f, 0.0f, 0.0f, 0.0f);
        for (int st = 0; st < numSteps; st += 2) proc->setStep (1, st, 1);
        render (*proc, head, 8.0, &c0);
        setup (true, 0.0f, 1.0f, 0.0f, 0.0f);
        for (int st = 0; st < numSteps; st += 2) proc->setStep (1, st, 1);
        render (*proc, head, 8.0, &c1);
        std::printf ("  stutter 0: %d pattern / %d ratchet hits | stutter 1: %d / %d\n", c0.pattern, c0.ratchet, c1.pattern, c1.ratchet);
        check (c0.ratchet == 0 && c1.ratchet > c1.pattern / 2, "ratchets appear with STOTTERN");

        // ZERFASERN: hit-to-hit variation of the same step
        auto hitSpread = [&] (float fray)
        {
            setup (true, 0.0f, 0.0f, 0.0f, fray);
            for (int st = 0; st < numSteps; st += 4) proc->setStep (1, st, 1);
            proc->setParam (voiceParamId (1, pAttack), 40.0f);   // deterministic attack, so only ZERFASERN varies it
            const auto out = render (*proc, head, 8.0);
            // compare the first 15 ms of consecutive hits: 1 - correlation
            const int p4 = (int) (sr * 0.5), w = (int) (sr * 0.015);
            const float* d = out.getReadPointer (0);
            double acc = 0.0; int n = 0;
            for (int i = p4; i + p4 <= out.getNumSamples(); i += p4)
            {
                double xy = 0, xx = 0, yy = 0;
                for (int k = 0; k < w; ++k) { xy += d[i + k] * d[i - p4 + k]; xx += d[i + k] * d[i + k]; yy += d[i - p4 + k] * d[i - p4 + k]; }
                acc += 1.0 - xy / std::sqrt (xx * yy + 1e-12); ++n;
            }
            return (float) (acc / juce::jmax (1, n));
        };
        const float f0 = hitSpread (0.0f), f1 = hitSpread (1.0f);
        std::printf ("  snare attack difference hit to hit (1 - corr): fray 0 %.3f | fray 1 %.3f\n", f0, f1);
        check (f1 > f0 * 2.0f, "ZERFASERN makes every hit different");

        // UMSCHICHTEN: same steps, different order
        Counts u;
        setup (true, 0.0f, 0.0f, 1.0f, 0.0f);
        proc->setStep (0, 0, 1);
        render (*proc, head, 16.0, &u);
        std::printf ("  one kick per bar over 8 bars, shuffle 1: %d kicks\n", u.pattern);
        check (u.pattern >= 3 && u.pattern <= 16, "UMSCHICHTEN moves steps without flooding");

        // preset render
        const int tp = (int) presets.size() - 1;
        prep (*proc, head, tp);
        double cpu = 0.0;
        const auto out = render (*proc, head, 16 * 4 * 60.0 / head.bpm, nullptr, &cpu);
        std::printf ("  preset %s: peak %.3f rms %.4f cpu %.1f%%\n", presets[(size_t) tp].name, out.getMagnitude (0, out.getNumSamples()), rms (out), cpu * 100.0);
        check (finite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f && rms (out) > 0.02f, "Traunfall preset plays cleanly");
        writeWav (out, outDir.getChildFile ("10_traunfall_16bars.wav"));
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== state round trip ==\n");
    {
        prep (*proc, head, 2);
        proc->setLocked (3, true);
        proc->setLength (6, 7);
        juce::MemoryBlock mb;
        proc->getStateInformation (mb);
        TraunwerkProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int v = 0; v < numVoices; ++v)
        {
            for (int s = 0; s < numSteps; ++s) if (other.getStep (v, s) != proc->getStep (v, s)) same = false;
            for (int j = 0; j < numVoices; ++j) if (std::abs (other.getMyzel (v, j) - proc->getMyzel (v, j)) > 1e-3f) same = false;
            if (other.getLength (v) != proc->getLength (v) || other.isLocked (v) != proc->isLocked (v)) same = false;
            for (int q = 0; q < numVoiceParams; ++q)
                if (std::abs (other.apvts.getRawParameterValue (voiceParamId (v, q))->load()
                              - proc->apvts.getRawParameterValue (voiceParamId (v, q))->load()) > 1e-4f) same = false;
        }
        check (same, "pattern, lengths, locks, network and sound restored");
        proc->setLocked (3, false);
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== long run stability (Kompost + Lawine + Myzel, 3 min) ==\n");
    {
        prep (*proc, head, 3);
        proc->setParam (id::kompost, 1.0f);
        proc->setParam (id::feed, 0.7f);
        proc->setParam (id::spur, 0.2f);
        proc->setParam (id::myzel, 1.0f);
        for (int i = 0; i < numVoices; ++i) proc->setMyzel (i, (i + 1) % numVoices, 0.9f);
        double cpu = 0.0;
        auto out = render (*proc, head, 180.0, nullptr, &cpu);
        const float lastRms = rms (out, out.getNumSamples() - (int) (sr * 10), (int) (sr * 10));
        std::printf ("  peak %.3f, last 10 s rms %.4f, cpu %.1f%%\n", out.getMagnitude (0, out.getNumSamples()), lastRms, cpu * 100.0);
        check (finite (out), "finite after 3 minutes");
        check (lastRms > 0.01f && lastRms < 0.6f, "still alive, not exploding");
    }

    // ---------------------------------------------------------------------
    std::printf ("\n== screenshot ==\n");
    {
        for (int v = 0; v < numVoices; ++v) proc->resetVoiceSample (v);
        prep (*proc, head, (int) presets.size() - 1);   // Traunfall: shows every panel active
        proc->setParam (id::kompost, 0.3f);
        proc->randomize(); proc->randomize(); proc->scrubTrail (0.6f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setVisible (true);
        for (int i = 0; i < 70; ++i)
        {
            render (*proc, head, 0.05, nullptr, nullptr, false);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        // let the last hit's grains arrive without draining them
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        for (int i = 0; i < 4; ++i) { proc->processBlock (buf, midi); head.ppq += block * head.bpm / 60.0 / sr; }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        const auto f = outDir.getChildFile ("traunwerk.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat fmt;
        check (fmt.writeImageToStream (img, os), "screenshot written");
        ed.reset();
    }

    proc->setPlayHead (nullptr);
    proc.reset();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL OK" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
