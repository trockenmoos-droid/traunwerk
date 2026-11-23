#include "Abdruck.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace
{
    struct Onset
    {
        int    sample;
        double time;
        float  strength;
        float  centroid = 0.0f;
        float  bands[3] {};      // low < 180 Hz, mid 180..2500 Hz, high > 5 kHz (energy)
        float  peak = 0.0f;
        int    end = 0;
    };

    float analyseSpectrum (const float* x, int n, double rate, float* bands)
    {
        constexpr int order = 10, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2, 0.0f);
        for (int i = 0; i < juce::jmin (n, size); ++i)
            buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (size - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double num = 0.0, den = 0.0;
        bands[0] = bands[1] = bands[2] = 0.0f;
        for (int k = 1; k < size / 2; ++k)
        {
            const double f = k * rate / size;
            const float m = buf[(size_t) k];
            num += f * m;
            den += m;
            if (f < 180.0)        bands[0] += m * m;
            else if (f < 2500.0)  bands[1] += m * m;
            else if (f > 5000.0)  bands[2] += m * m;
        }
        for (int b = 0; b < 3; ++b) bands[b] = std::sqrt (bands[b]);
        return den > 0.0 ? (float) (num / den) : 0.0f;
    }

    // which brightness quantile each voice wants: KICK SNARE CLAP HAT OPEN TOM HOLZ GLOCKE
    const float voiceQuantile[tw::numVoices] { 0.02f, 0.45f, 0.6f, 0.97f, 0.85f, 0.16f, 0.32f, 0.72f };
}

AbdruckResult Abdruck::analyse (const juce::AudioBuffer<float>& monoIn, double rate)
{
    AbdruckResult r;
    for (auto& a : r.accents) a = 1.0f;

    const int n = juce::jmin (monoIn.getNumSamples(), (int) (rate * 60.0));
    if (n < (int) (rate * 0.2)) return r;
    const float* x = monoIn.getReadPointer (0);

    // ---- onset strength --------------------------------------------------
    const int hop = juce::jmax (16, (int) std::round (rate * 0.005));
    const int frames = n / hop;
    std::vector<float> env ((size_t) frames, 0.0f), flux ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float d = i > 0 ? x[i] - x[i - 1] : 0.0f;
            e += d * d + 0.25 * x[i] * x[i];
        }
        env[(size_t) f] = (float) std::sqrt (e / hop);
    }
    // Onset strength as a jump in *level* (dB over two frames). A plain
    // energy difference is useless here: granular bodies crackle all the
    // time, and in linear terms that crackle looks like onsets everywhere.
    std::vector<float> le ((size_t) frames, -100.0f);
    float maxLe = -100.0f;
    for (int f = 0; f < frames; ++f)
    {
        le[(size_t) f] = 20.0f * std::log10 (env[(size_t) f] + 1.0e-5f);
        maxLe = juce::jmax (maxLe, le[(size_t) f]);
    }
    std::vector<float> les ((size_t) frames, -100.0f);
    for (int f = 1; f < frames; ++f) les[(size_t) f] = 0.5f * (le[(size_t) f] + le[(size_t) f - 1]);

    float maxFlux = 0.0f;
    for (int f = 2; f < frames; ++f)
    {
        if (le[(size_t) f] < maxLe - 45.0f) continue;
        const float jump = juce::jmax (0.0f, les[(size_t) f] - les[(size_t) f - 2]);
        const float loud = std::sqrt (env[(size_t) f] / juce::jmax (1.0e-6f, juce::Decibels::decibelsToGain (maxLe)));
        flux[(size_t) f] = jump * loud;
        maxFlux = juce::jmax (maxFlux, flux[(size_t) f]);
    }
    if (maxFlux <= 1.0e-6f) return r;

    // ---- peak picking ----------------------------------------------------
    std::vector<Onset> onsets;
    int lastOnset = -1000;
    for (int f = 3; f < frames - 3; ++f)
    {
        double mean = 0.0; int cnt = 0;
        for (int k = juce::jmax (0, f - 12); k <= juce::jmin (frames - 1, f + 12); ++k) { mean += flux[(size_t) k]; ++cnt; }
        const float thr = (float) (mean / cnt) * 1.4f + 0.025f * maxFlux;
        const float v = flux[(size_t) f];
        if (v <= thr) continue;
        bool isMax = true;
        for (int k = f - 3; k <= f + 3; ++k) if (flux[(size_t) k] > v) { isMax = false; break; }
        if (! isMax || f - lastOnset < 10) continue;
        lastOnset = f;
        const int s = juce::jmax (0, (f - 2) * hop);
        onsets.push_back ({ s, s / rate, v });
    }
    r.numOnsets = (int) onsets.size();
    if (onsets.empty()) return r;

    // ---- slices ----------------------------------------------------------
    std::vector<float> lowpassed ((size_t) n, 0.0f);
    {
        const float c = std::exp (-6.2831853f * 150.0f / (float) rate);
        float a = 0.0f, b = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            a = (1.0f - c) * x[i] + c * a;
            b = (1.0f - c) * a + c * b;
            lowpassed[(size_t) i] = b;
        }
    }
    const float globalPeak = monoIn.getMagnitude (0, 0, n);
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        auto& o = onsets[i];
        const int next = i + 1 < onsets.size() ? onsets[i + 1].sample : n;
        o.end = juce::jmin (next, o.sample + (int) (rate * 0.8));
        const int len = o.end - o.sample;
        float pk = 0.0f;
        for (int k = o.sample; k < o.end; ++k) pk = juce::jmax (pk, std::abs (x[k]));
        o.peak = pk;
        o.centroid = analyseSpectrum (x + o.sample, len, rate, o.bands);

        // what is *new* at this onset: subtract the spectrum just before it,
        // so a long kick tail does not make every later hit look like a kick
        const int pre = juce::jmin (1024, o.sample);
        if (pre > 64)
        {
            float before[3];
            analyseSpectrum (x + o.sample - pre, pre, rate, before);
            for (int b = 0; b < 3; ++b) o.bands[b] = juce::jmax (0.0f, o.bands[b] - before[b]);
        }

        // The low band needs longer windows: a 21 ms FFT frame is about one
        // period of a kick's tail, so its value just follows the tail's phase.
        // 30 ms RMS of a 150 Hz low-pass, after minus before.
        {
            const int W = (int) (rate * 0.03);
            auto lrms = [&] (int a, int b)
            {
                a = juce::jlimit (0, n, a); b = juce::jlimit (0, n, b);
                double e = 0.0;
                for (int k = a; k < b; ++k) e += (double) lowpassed[(size_t) k] * lowpassed[(size_t) k];
                return b > a ? (float) std::sqrt (e / (b - a)) : 0.0f;
            };
            o.bands[0] = juce::jmax (0.0f, lrms (o.sample, o.sample + W) - lrms (o.sample - W, o.sample));
        }
    }

    std::vector<int> usable;
    for (int i = 0; i < (int) onsets.size(); ++i)
        if (onsets[(size_t) i].end - onsets[(size_t) i].sample > (int) (rate * 0.03)
            && onsets[(size_t) i].peak > 0.03f * globalPeak)
            usable.push_back (i);
    if (usable.empty()) return r;

    std::sort (usable.begin(), usable.end(),
               [&] (int a, int b) { return onsets[(size_t) a].centroid < onsets[(size_t) b].centroid; });

    std::vector<bool> used (onsets.size(), false);
    const int m = (int) usable.size();
    for (int v = 0; v < tw::numVoices; ++v)
    {
        const int idx = juce::roundToInt (voiceQuantile[v] * (float) (m - 1));
        const int w = juce::jmax (1, (int) (m * 0.08f));
        int best = -1;
        for (int k = juce::jmax (0, idx - w); k <= juce::jmin (m - 1, idx + w); ++k)
        {
            const int oi = usable[(size_t) k];
            if (used[(size_t) oi]) continue;
            if (best < 0 || onsets[(size_t) oi].peak > onsets[(size_t) best].peak) best = oi;
        }
        if (best < 0) best = usable[(size_t) idx];
        used[(size_t) best] = true;

        const auto& o = onsets[(size_t) best];
        const int len = o.end - o.sample;
        juce::AudioBuffer<float> b (1, len);
        b.copyFrom (0, 0, x + o.sample, len);
        auto* d = b.getWritePointer (0);
        const int fi = juce::jmin (len / 8, 24);
        for (int k = 0; k < fi; ++k) d[k] *= (float) k / (float) fi;
        const int fo = juce::jmin (len / 3, (int) (rate * 0.012));
        for (int k = 0; k < fo; ++k) d[len - 1 - k] *= (float) k / (float) fo;
        if (o.peak > 0.0f) b.applyGain (0.9f / o.peak);

        r.slices[(size_t) v] = std::move (b);
        r.hasSlice[(size_t) v] = true;
    }
    r.valid = true;

    // ---- groove ----------------------------------------------------------
    if (onsets.size() < 8) return r;

    const double fr = rate / hop;
    double bestScore = 0.0; int bestLag = 0;
    for (int lag = (int) (60.0 * fr / 170.0); lag <= (int) (60.0 * fr / 70.0); ++lag)
    {
        double acf = 0.0;
        for (int f = 0; f + lag < frames; ++f) acf += flux[(size_t) f] * flux[(size_t) (f + lag)];
        const double bpm = 60.0 * fr / lag;
        const double l = std::log2 (bpm / 120.0) / 0.6;
        const double score = acf * std::exp (-0.5 * l * l);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag == 0) return r;

    // The autocorrelation lag is only good to ~1 %, which over a long loop
    // drifts by whole 16ths. Refine period and phase together by fitting a
    // 16th grid to the onsets themselves.
    const double p16Coarse = (bestLag / fr) / 4.0;
    double p16 = p16Coarse, bestPhase = 0.0, bestCost = 1.0e30;
    for (int pk = -60; pk <= 60; ++pk)
    {
        const double period = p16Coarse * (1.0 + pk * 0.0005);
        for (int k = 0; k < 48; ++k)
        {
            const double ph = period * k / 48.0;
            double cost = 0.0;
            for (auto& o : onsets)
            {
                const double g = (o.time - ph) / period;
                const double d = g - std::round (g);
                cost += o.strength * d * d;
            }
            if (cost < bestCost) { bestCost = cost; bestPhase = ph; p16 = period; }
        }
    }
    // fine phase pass
    {
        const double step = p16 / 48.0;
        const double centre = bestPhase;
        for (int k = -24; k <= 24; ++k)
        {
            const double ph = centre + step * k / 24.0;
            double cost = 0.0;
            for (auto& o : onsets)
            {
                const double g = (o.time - ph) / p16;
                const double d = g - std::round (g);
                cost += o.strength * d * d;
            }
            if (cost < bestCost) { bestCost = cost; bestPhase = ph; }
        }
    }
    // least-squares polish: with every onset assigned to its nearest grid
    // line, fit time = phase + k * period. Removes the last bit of drift.
    for (int it = 0; it < 4; ++it)
    {
        double sw = 0, sk = 0, st = 0, skk = 0, skt = 0;
        for (auto& o : onsets)
        {
            const double k = std::round ((o.time - bestPhase) / p16);
            const double d = (o.time - bestPhase) / p16 - k;
            if (std::abs (d) > 0.3) continue;           // off-grid hits (ghosts, flams) don't vote
            const double w = o.strength;
            sw += w; sk += w * k; st += w * o.time; skk += w * k * k; skt += w * k * o.time;
        }
        const double den = sw * skk - sk * sk;
        if (sw <= 0.0 || std::abs (den) < 1.0e-12) break;
        const double period = (sw * skt - sk * st) / den;
        if (period < p16Coarse * 0.9 || period > p16Coarse * 1.1) break;
        bestPhase = (st - period * sk) / sw;
        p16 = period;
    }
    bestPhase = std::fmod (bestPhase, p16);
    if (bestPhase < 0.0) bestPhase += p16;
    r.bpm = 60.0 / (p16 * 4.0);

    // place every onset on the refined 16th grid
    struct Placed { int slot0; int k; float dev; float strength; float bands[3]; };
    std::vector<Placed> placed;
    std::array<float, tw::numSteps> lowBySlot {};
    int kMin = 1 << 30, kMax = -(1 << 30);
    for (auto& o : onsets)
    {
        const double g = (o.time - bestPhase) / p16;
        const int k = (int) std::round (g);
        const int slot0 = ((k % 16) + 16) % 16;
        placed.push_back ({ slot0, k, (float) (g - k), o.strength, { o.bands[0], o.bands[1], o.bands[2] } });
        lowBySlot[(size_t) slot0] += o.bands[0];
        kMin = juce::jmin (kMin, k); kMax = juce::jmax (kMax, k);
    }

    // the downbeat is where the low end is strongest (bar and half-bar first)
    // parity first (kicks sit on even 16ths far more often than on odd ones),
    // then the best downbeat within that parity
    float lowEven = 0.0f, lowOdd = 0.0f;
    for (int q = 0; q < 16; ++q) (q & 1 ? lowOdd : lowEven) += lowBySlot[(size_t) q];
    int parity = lowOdd > lowEven ? 1 : 0;

    // With swing the off-beats come late, so after centring the grid the
    // late class is the odd one. That is a much clearer signal than the low
    // end, so it decides whenever the material is noticeably swung.
    {
        double ev = 0.0, od = 0.0, we = 0.0, wo = 0.0;
        for (auto& p : placed)
            if (p.slot0 & 1) { od += p.dev * p.strength; wo += p.strength; }
            else             { ev += p.dev * p.strength; we += p.strength; }
        if (we > 0.0 && wo > 0.0)
        {
            const double d = od / wo - ev / we;
            if (std::abs (d) > 0.06) parity = d > 0.0 ? 0 : 1;
        }
    }

    int rot = parity; float rotBest = -1.0f;
    for (int q = parity; q < 16; q += 2)
    {
        const float sc = 2.0f * lowBySlot[(size_t) q] + lowBySlot[(size_t) ((q + 8) % 16)]
                       + 0.5f * (lowBySlot[(size_t) ((q + 4) % 16)] + lowBySlot[(size_t) ((q + 12) % 16)]);
        if (sc > rotBest) { rotBest = sc; rot = q; }
    }

    const int bars = juce::jmax (1, (kMax - kMin) / 16 + 1);
    std::array<double, tw::numSteps> devSum {}, wSum {};
    std::array<int, tw::numSteps> cnt {};
    std::array<std::vector<std::pair<float, float>>, tw::numSteps> devs;   // (dev, weight)
    std::array<std::array<double, 3>, tw::numSteps> band {};
    for (auto& p : placed)
    {
        const int slot = ((p.slot0 - rot) % 16 + 16) % 16;
        devSum[(size_t) slot] += p.dev * p.strength;
        devs[(size_t) slot].push_back ({ p.dev, p.strength });
        wSum[(size_t) slot]   += p.strength;
        cnt[(size_t) slot]++;
        for (int b = 0; b < 3; ++b) band[(size_t) slot][(size_t) b] += p.bands[b];
    }

    float maxMean = 0.0f;
    std::array<float, tw::numSteps> mean {};
    for (int s = 0; s < 16; ++s)
    {
        if (cnt[(size_t) s] >= juce::jmax (2, bars / 3))
        {
            // weighted median: stray detections (flams, grain crackle) don't drag it
            auto& d = devs[(size_t) s];
            std::sort (d.begin(), d.end());
            double acc = 0.0, med = 0.0;
            for (auto& [dv, w] : d) { acc += w; if (acc >= 0.5 * wSum[(size_t) s]) { med = dv; break; } }
            // trust it only as far as the hits agree with each other: a loose
            // cloud of deviations (granular crackle, flams) is shrunk to zero
            double spread = 0.0;
            for (auto& [dv, w] : d) spread += w * std::abs (dv - med);
            spread /= wSum[(size_t) s];
            const double trust = juce::jlimit (0.0, 1.0, 1.0 - (spread - 0.08) / 0.15);
            r.offsets[(size_t) s] = juce::jlimit (-0.4f, 0.4f, (float) (med * trust));
            mean[(size_t) s] = (float) wSum[(size_t) s] / (float) cnt[(size_t) s];
            maxMean = juce::jmax (maxMean, mean[(size_t) s]);
        }
    }
    // quarters define the grid: shift so slots 0/4/8/12 average zero
    {
        float q = 0.0f; int qn = 0;
        for (int s = 0; s < 16; s += 4) if (cnt[(size_t) s] >= juce::jmax (2, bars / 3)) { q += r.offsets[(size_t) s]; ++qn; }
        if (qn > 0)
            for (int s = 0; s < 16; ++s)
                if (cnt[(size_t) s] >= juce::jmax (2, bars / 3))
                    r.offsets[(size_t) s] = juce::jlimit (-0.4f, 0.4f, r.offsets[(size_t) s] - q / (float) qn);
    }

    for (int s = 0; s < 16; ++s)
        r.accents[(size_t) s] = cnt[(size_t) s] > 0 && maxMean > 0.0f
                              ? 0.6f + 0.6f * mean[(size_t) s] / maxMean : 0.85f;

    // pattern skeleton from band energy per slot, averaged over the bars:
    // low end -> KICK, mid without matching low -> SNARE, high -> HAT
    std::array<double, 3> bmax {};
    for (int s = 0; s < 16; ++s)
        for (int b = 0; b < 3; ++b)
        {
            band[(size_t) s][(size_t) b] /= bars;
            bmax[(size_t) b] = juce::jmax (bmax[(size_t) b], band[(size_t) s][(size_t) b]);
        }
    for (int s = 0; s < 16; ++s)
    {
        const float lo = bmax[0] > 0 ? (float) (band[(size_t) s][0] / bmax[0]) : 0.0f;
        const float mi = bmax[1] > 0 ? (float) (band[(size_t) s][1] / bmax[1]) : 0.0f;
        const float hi = bmax[2] > 0 ? (float) (band[(size_t) s][2] / bmax[2]) : 0.0f;
        if (lo >= 0.42f) r.steps[0][(size_t) s] = lo >= 0.85f ? 2 : 1;
        const float sn = mi - 0.6f * lo;
        if (sn >= 0.25f) r.steps[1][(size_t) s] = sn >= 0.6f ? 2 : 1;
        if (hi >= 0.3f && sn < 0.25f) r.steps[3][(size_t) s] = hi >= 0.8f ? 2 : 1;
    }
    r.hasGroove = true;
    return r;
}
