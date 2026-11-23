#include "DrumEngine.h"
#include <cmath>

namespace
{
    constexpr float halfPi = 1.5707963267948966f;

    inline void panGains (float pan, float& l, float& r)
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.5f * halfPi;
        l = std::cos (a) * 1.41421356f;
        r = std::sin (a) * 1.41421356f;
    }

    inline float smoothstep (float x)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
}

DrumEngine::DrumEngine()
{
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5f - 0.5f * std::cos (6.283185307f * (float) i / (float) (window.size() - 1));

    for (auto& w : wearVis) w.store (0.0f);
    for (auto& b : compostBins) b.store (0.0f);

    juce::Random r (0x7a0b);
    for (auto& d : driftDir) d = r.nextBool() ? 1.0f : -1.0f;
    compostLastAnchor.fill (-1);
}

void DrumEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = juce::jmax (1, maxBlock);
    voiceBuf.setSize (tw::numVoices * 2, maxBlockSize);
    compost.assign ((size_t) (sr * 4.0), 0.0f);
    reset();
}

void DrumEngine::reset()
{
    for (auto& vh : hits)
        for (auto& h : vh) { h.active = false; h.src.reset(); }
    wear.fill (0.0f);
    for (auto& f : filt) for (auto& s : f) s = {};
    std::fill (compost.begin(), compost.end(), 0.0f);
    compostWrite = 0;
    compostAbs = 0;
    compostFilled = 0;
    compostLastAnchor.fill (-1);
}

void DrumEngine::setSource (int voice, SourcePtr src)
{
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    pending[(size_t) voice] = std::move (src);
    pendingFlag[(size_t) voice] = true;
}

SourcePtr DrumEngine::getSource (int voice) const
{
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    if (pendingFlag[(size_t) voice]) return pending[(size_t) voice];
    return sources[(size_t) voice];
}

void DrumEngine::pushGrainDot (const GrainDot& d)
{
    const auto scope = grainFifo.write (1);
    if (scope.blockSize1 > 0) grainBuf[(size_t) scope.startIndex1] = d;
}

void DrumEngine::pushHit (const HitInfo& h)
{
    const auto scope = hitFifo.write (1);
    if (scope.blockSize1 > 0) hitBuf[(size_t) scope.startIndex1] = h;
}

float DrumEngine::readSource (const DrumSource& s, double pos) const noexcept
{
    const int n = s.data.getNumSamples();
    if (pos < 0.0 || pos >= (double) (n - 1)) return 0.0f;
    const int i = (int) pos;
    const float f = (float) (pos - i);
    const float* d = s.data.getReadPointer (0);
    return d[i] + f * (d[i + 1] - d[i]);
}

float DrumEngine::readCompost (double pos) const noexcept
{
    const int n = (int) compost.size();
    if (n < 2) return 0.0f;
    double p = std::fmod (pos, (double) n);
    if (p < 0.0) p += n;
    const int i = (int) p;
    const int j = (i + 1) % n;
    const float f = (float) (p - i);
    return compost[(size_t) i] + f * (compost[(size_t) j] - compost[(size_t) i]);
}

void DrumEngine::trigger (int v, float velocity, const VoiceSettings& s, int kind)
{
    if (v < 0 || v >= tw::numVoices) return;
    auto src = sources[(size_t) v];
    if (src == nullptr || src->data.getNumSamples() < 16) return;

    const float vel = juce::jlimit (0.0f, 1.0f, velocity);
    if (vel <= 0.001f) return;

    // HAT chokes OPEN
    if (v == 3)
        for (auto& h : hits[4])
            if (h.active && h.fadeOut == 0) { h.fadeLen = h.fadeOut = juce::jmax (1, (int) (0.006 * sr)); }

    // keep two slots in reserve: once six hits ring, the oldest fades out
    // over 5 ms instead of being cut when a slot is finally needed
    {
        int active = 0; Hit* oldest = nullptr;
        for (auto& h : hits[(size_t) v])
            if (h.active && h.fadeOut == 0)
            {
                ++active;
                if (oldest == nullptr || h.serial < oldest->serial) oldest = &h;
            }
        if (active >= hitsPerVoice - 2 && oldest != nullptr)
            oldest->fadeLen = oldest->fadeOut = juce::jmax (1, (int) (0.005 * sr));
    }

    // free slot, else (rare) take the oldest
    Hit* slot = nullptr;
    for (auto& h : hits[(size_t) v]) if (! h.active) { slot = &h; break; }
    if (slot == nullptr)
    {
        slot = &hits[(size_t) v][0];
        for (auto& h : hits[(size_t) v]) if (h.serial < slot->serial) slot = &h;
    }

    Hit& h = *slot;
    const float w = wear[(size_t) v];
    const double srcRate = src->rate;
    const double srcLen = (double) src->data.getNumSamples();

    h.active    = true;
    h.fadeOut   = 0;
    h.voice     = v;
    h.t         = 0.0;
    h.serial    = ++serialCounter;
    h.pitchRatio = std::pow (2.0, (s.pitch - 1.5f * w) / 12.0);
    h.ratio     = h.pitchRatio * srcRate / sr;
    h.scanOut   = s.scan;
    h.scanRate  = s.scan * srcRate / sr;
    h.directPos = 0.0;
    h.attackSamples = s.attackMs * 0.001f * (float) sr;
    h.bite      = s.bite * (0.65f + 0.35f * vel);
    h.level     = s.level * (0.15f + 0.85f * std::pow (vel, 1.3f)) * (1.0f - 0.3f * w);
    h.pan       = s.pan;
    panGains (s.pan, h.panL, h.panR);
    h.decaySamples = juce::jmax (1.0f, s.decay * (float) sr);

    const float density = s.density * (0.6f + 0.4f * vel) * (1.0f - 0.5f * w);
    h.grainsPerSample = density / (float) sr;
    h.grainLen  = juce::jmax (32.0f, s.grainMs * 0.001f * (float) sr * (1.0f - 0.6f * w));
    h.sprayAmt  = juce::jmin (1.0f, s.spray + 0.4f * w);
    h.spraySamples = h.sprayAmt * 0.15f * (float) srcRate;
    h.drift     = driftDir[(size_t) v] * w * (float) juce::jmin (0.3 * srcRate, 0.25 * srcLen);

    const float overlap = density * h.grainLen / (float) sr;
    h.norm = 1.0f / juce::jmax (1.0f, 0.5f * overlap);

    h.nextGrain = h.attackSamples > 1.0f ? h.attackSamples * 0.35 : 0.0;
    h.bodyDone  = false;
    for (auto& g : h.grains) g.active = false;
    h.src = std::move (src);

    // compost: read from where this voice was last heard
    const int64_t anchor = compostLastAnchor[(size_t) v];
    const int64_t age = anchor >= 0 ? compostAbs - anchor : -1;
    if (anchor >= 0 && age > 0 && age < (int64_t) compost.size() - 1)
    {
        h.compostAnchor = anchor;
        h.compostSpan   = (int) age;
    }
    else
    {
        h.compostAnchor = -1;
        h.compostSpan   = 0;
    }
    h.compostAmt = kompostAmt;
    compostLastAnchor[(size_t) v] = compostAbs;

    // weathering
    wear[(size_t) v] = juce::jmin (1.0f, w + s.wear * (0.12f + 0.2f * vel) * (1.0f - w));

    pushHit ({ v, vel, kind, renderedSamples });
}

void DrumEngine::spawnGrain (Hit& h)
{
    Grain* g = nullptr;
    for (auto& gr : h.grains) if (! gr.active) { g = &gr; break; }
    if (g == nullptr) return;

    const float env = std::exp (-(float) h.t / h.decaySamples);
    const float jitterR = rng.nextFloat() * 2.0f - 1.0f;
    const double pitchJ = std::pow (2.0, (double) (rng.nextFloat() * 2.0f - 1.0f) * h.sprayAmt * 0.3 / 12.0);

    bool useCompost = h.compostAnchor >= 0 && rng.nextFloat() < h.compostAmt;
    double pos = 0.0;
    float  dotPos = 0.0f;

    if (useCompost)
    {
        const double off = h.t * h.scanOut + jitterR * h.sprayAmt * 0.15 * sr;
        const double maxOff = (double) h.compostSpan - h.grainLen * h.pitchRatio * 1.5 - 2.0;
        if (maxOff < h.grainLen) useCompost = false;
        else
        {
            const double o = juce::jlimit (0.0, maxOff, off);
            pos = (double) (h.compostAnchor % (int64_t) compost.size()) + o;
            dotPos = (float) (o / juce::jmax (1.0, maxOff));
        }
    }

    if (! useCompost)
    {
        const double len = (double) h.src->data.getNumSamples();
        const double centre = h.t * h.scanRate + h.drift + jitterR * h.spraySamples;
        if (h.t * h.scanRate > len + h.spraySamples) { h.bodyDone = true; return; }
        pos = juce::jmax (0.0, centre);
        if (pos >= len - 2.0) return;
        dotPos = (float) (pos / len);
    }

    // compost already carries the level and velocity it was played with
    float amp = (useCompost ? 1.0f : h.level) * h.norm * std::sqrt (env);
    if (h.attackSamples > 1.0f)
    {
        const float x = ((float) h.t + h.grainLen * 0.5f - 0.5f * h.attackSamples) / (0.5f * h.attackSamples);
        amp *= smoothstep (x);
    }
    if (amp < 1.0e-5f) return;

    float l, r;
    panGains (h.pan + h.sprayAmt * 0.6f * (rng.nextFloat() * 2.0f - 1.0f), l, r);

    g->active  = true;
    g->compost = useCompost;
    g->pos     = pos;
    g->inc     = (useCompost ? h.pitchRatio : h.ratio) * pitchJ;
    g->age     = 0;
    g->len     = (int) h.grainLen;
    g->gL      = amp * l;
    g->gR      = amp * r;

    pushGrainDot ({ h.voice, (float) (h.t / sr), dotPos, amp, useCompost });
}

void DrumEngine::renderHit (Hit& h, float* L, float* R, int n)
{
    const auto& src = *h.src;
    const float winScale = (float) (window.size() - 1);

    for (int i = 0; i < n; ++i)
    {
        // grain scheduling
        while (! h.bodyDone && h.nextGrain <= 0.0)
        {
            const float env = std::exp (-(float) h.t / h.decaySamples);
            if (env < 0.003f) { h.bodyDone = true; break; }
            spawnGrain (h);
            const float rate = h.grainsPerSample * env;
            double interval = 1.0 / juce::jmax (1.0e-7f, rate);
            const float j = juce::jmin (0.9f, 0.3f + 0.6f * h.sprayAmt);
            interval *= (1.0 - j + 2.0 * j * rng.nextFloat());
            h.nextGrain += juce::jmax (1.0, interval);
        }
        h.nextGrain -= 1.0;

        float outL = 0.0f, outR = 0.0f;

        // ANSCHLAG: straight playback of the start
        if (h.t < h.attackSamples)
        {
            const float A = h.attackSamples;
            const float t = (float) h.t;
            const float fade = t < 0.6f * A ? 1.0f
                             : 0.5f * (1.0f + std::cos (3.14159265f * (t - 0.6f * A) / (0.4f * A)));
            const float s = readSource (src, h.directPos) * h.bite * h.level * fade;
            outL += s * h.panL;
            outR += s * h.panR;
            h.directPos += h.ratio;
        }

        // KÖRPER: the grain cloud
        for (auto& g : h.grains)
        {
            if (! g.active) continue;
            const float w = window[(size_t) ((float) g.age * winScale / (float) g.len)];
            const float s = (g.compost ? readCompost (g.pos) : readSource (src, g.pos)) * w;
            outL += s * g.gL;
            outR += s * g.gR;
            g.pos += g.inc;
            if (++g.age >= g.len) g.active = false;
        }

        if (h.fadeOut > 0)
        {
            const float f = (float) h.fadeOut / (float) h.fadeLen;
            outL *= f; outR *= f;
            if (--h.fadeOut == 0) { h.active = false; }
        }

        L[i] += outL;
        R[i] += outR;
        h.t += 1.0;

        if (! h.active) break;
    }

    if (h.active && h.bodyDone && h.t >= h.attackSamples)
    {
        bool any = false;
        for (auto& g : h.grains) if (g.active) { any = true; break; }
        if (! any) h.active = false;
    }

    if (! h.active) h.src.reset();
}

void DrumEngine::render (float* L, float* R, int numSamples,
                         const std::array<VoiceSettings, tw::numVoices>& settings)
{
    {
        const juce::SpinLock::ScopedTryLockType tl (pendingLock);
        if (tl.isLocked())
            for (size_t v = 0; v < pending.size(); ++v)
                if (pendingFlag[v]) { sources[v] = pending[v]; pending[v].reset(); pendingFlag[v] = false; }
    }

    if (freshRequested.exchange (false))
    {
        std::fill (compost.begin(), compost.end(), 0.0f);
        compostFilled = 0;
        compostLastAnchor.fill (-1);
        for (auto& vh : hits) for (auto& h : vh) h.compostAnchor = -1;
        for (auto& b : compostBins) b.store (0.0f);
    }

    const float dec = std::exp (-(float) numSamples / (recoverySec * (float) sr));
    for (size_t v = 0; v < wear.size(); ++v)
    {
        wear[v] *= dec;
        wearVis[v].store (wear[v]);
    }

    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - done);
        voiceBuf.clear (0, n);

        for (int v = 0; v < tw::numVoices; ++v)
        {
            float* vl = voiceBuf.getWritePointer (v * 2);
            float* vr = voiceBuf.getWritePointer (v * 2 + 1);
            bool any = false;
            for (auto& h : hits[(size_t) v])
                if (h.active) { renderHit (h, vl, vr, n); any = true; }

            // tone + wear filter, run always so the state decays smoothly
            const auto& s = settings[(size_t) v];
            const float lpHz = juce::jlimit (40.0f, (float) sr * 0.45f,
                                             20000.0f * std::pow (0.01f, juce::jmax (0.0f, -s.tone))
                                                      * std::pow (0.12f, wear[(size_t) v]));
            const float hpHz = s.tone > 0.0f ? 20.0f * std::pow (200.0f, s.tone) : 0.0f;
            if (! any && std::abs (filt[(size_t) v][0].s2) < 1.0e-6f && std::abs (filt[(size_t) v][2].s1) < 1.0e-6f)
                continue;

            auto svfCoeffs = [this] (float fc, float& a1, float& a2, float& a3, float& g)
            {
                g = std::tan (3.14159265f * fc / (float) sr);
                const float k = 1.41421356f;
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            };

            float la1, la2, la3, lg, ha1, ha2, ha3, hg;
            svfCoeffs (lpHz, la1, la2, la3, lg);
            svfCoeffs (juce::jmax (10.0f, hpHz), ha1, ha2, ha3, hg);
            juce::ignoreUnused (lg, hg);

            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = ch == 0 ? vl : vr;
                auto& lp = filt[(size_t) v][(size_t) ch];
                auto& hp = filt[(size_t) v][(size_t) ch + 2];
                for (int i = 0; i < n; ++i)
                {
                    float x = d[i];
                    {
                        const float v3 = x - lp.s2;
                        const float v1 = la1 * lp.s1 + la2 * v3;
                        const float v2 = lp.s2 + la2 * lp.s1 + la3 * v3;
                        lp.s1 = 2.0f * v1 - lp.s1;
                        lp.s2 = 2.0f * v2 - lp.s2;
                        x = v2;
                    }
                    if (hpHz > 0.0f)
                    {
                        const float v3 = x - hp.s2;
                        const float v1 = ha1 * hp.s1 + ha2 * v3;
                        const float v2 = hp.s2 + ha2 * hp.s1 + ha3 * v3;
                        hp.s1 = 2.0f * v1 - hp.s1;
                        hp.s2 = 2.0f * v2 - hp.s2;
                        x = x - 1.41421356f * v1 - v2;
                    }
                    d[i] = x;
                }
            }

            juce::FloatVectorOperations::add (L + done, vl, n);
            juce::FloatVectorOperations::add (R + done, vr, n);
        }

        // write this block's output into the compost
        const int size = (int) compost.size();
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[done + i] + R[done + i]);
            compost[(size_t) compostWrite] = std::tanh (m * 0.9f);
            binPeak = juce::jmax (binPeak, std::abs (m));
            const int bin = (int) ((int64_t) compostWrite * numCompostBins / size);
            if (bin != lastBin)
            {
                if (lastBin >= 0) compostBins[(size_t) lastBin].store (juce::jmin (1.0f, binPeak));
                binPeak = 0.0f;
                lastBin = bin;
            }
            if (++compostWrite >= size) compostWrite = 0;
            ++compostAbs;
        }
        compostFilled = juce::jmin (size, compostFilled + n);
        compostFillVis.store ((float) compostFilled / (float) size);

        done += n;
    }
    renderedSamples += numSamples;
}
