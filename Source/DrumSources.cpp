#include "DrumSources.h"
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** Minimal TPT state-variable filter, used to shape the noise parts. */
    struct Svf
    {
        double g = 0, k = 1, s1 = 0, s2 = 0;
        void set (double fc, double q, double sr) { g = std::tan (3.141592653589793 * fc / sr); k = 1.0 / q; }
        void process (double x, double& lp, double& bp, double& hp)
        {
            const double h = (x - (g + k) * s1 - s2) / (1.0 + g * (g + k));
            bp = g * h + s1;   s1 = g * h + bp;
            lp = g * bp + s2;  s2 = g * bp + lp;
            hp = h;
        }
    };

    juce::AudioBuffer<float> alloc (double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * DrumSources::rate));
        b.clear();
        return b;
    }

    void normalise (juce::AudioBuffer<float>& b, float peakTarget)
    {
        const float m = b.getMagnitude (0, 0, b.getNumSamples());
        if (m > 0.0f) b.applyGain (peakTarget / m);

        // 1 ms fade in guard and a short fade out at the very end
        auto* d = b.getWritePointer (0);
        const int n = b.getNumSamples();
        const int fo = juce::jmin (n / 4, 480);
        for (int i = 0; i < fo; ++i) d[n - 1 - i] *= (float) i / (float) fo;
    }

    juce::AudioBuffer<float> kick()
    {
        auto b = alloc (1.4);
        auto* d = b.getWritePointer (0);
        juce::Random rng (101);
        Svf click; click.set (3500.0, 0.7, DrumSources::rate);
        double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            const double f = 46.0 + 120.0 * std::exp (-t / 0.038) + 30.0 * std::exp (-t / 0.006);
            ph += twoPi * f / DrumSources::rate;
            double body = std::sin (ph) * std::exp (-t / 0.42);
            double lp, bp, hp;
            click.process (rng.nextFloat() * 2.0 - 1.0, lp, bp, hp);
            const double c = lp * std::exp (-t / 0.0025) * 0.6;
            d[i] = (float) std::tanh (1.6 * (body + c));
        }
        normalise (b, 0.95f);
        return b;
    }

    juce::AudioBuffer<float> snare()
    {
        auto b = alloc (0.9);
        auto* d = b.getWritePointer (0);
        juce::Random rng (202);
        Svf nz; nz.set (5200.0, 0.6, DrumSources::rate);
        double p1 = 0, p2 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            p1 += twoPi * (182.0 + 40.0 * std::exp (-t / 0.01)) / DrumSources::rate;
            p2 += twoPi * 331.0 / DrumSources::rate;
            const double tone = (std::sin (p1) * 0.6 + std::sin (p2) * 0.35) * std::exp (-t / 0.075);
            double lp, bp, hp;
            nz.process (rng.nextFloat() * 2.0 - 1.0, lp, bp, hp);
            const double noise = (bp * 1.2 + hp * 0.5) * std::exp (-t / 0.16);
            d[i] = (float) std::tanh (1.3 * (tone + noise));
        }
        normalise (b, 0.9f);
        return b;
    }

    juce::AudioBuffer<float> clap()
    {
        auto b = alloc (0.8);
        auto* d = b.getWritePointer (0);
        juce::Random rng (303);
        Svf bpf; bpf.set (1150.0, 1.6, DrumSources::rate);
        const double bursts[] { 0.0, 0.009, 0.019, 0.031 };
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            double env = 0.0;
            for (int k = 0; k < 4; ++k)
                if (t >= bursts[k] && k < 3)
                    env += std::exp (-(t - bursts[k]) / 0.0045) * (k == 0 ? 0.8 : 1.0);
            if (t >= bursts[3]) env += std::exp (-(t - bursts[3]) / 0.13) * 0.9;
            double lp, bp, hp;
            bpf.process (rng.nextFloat() * 2.0 - 1.0, lp, bp, hp);
            d[i] = (float) (bp * env);
        }
        normalise (b, 0.9f);
        return b;
    }

    /** 808-style metallic cluster: six detuned squares through a high-pass. */
    juce::AudioBuffer<float> metal (double seconds, double decay, int seed)
    {
        auto b = alloc (seconds);
        auto* d = b.getWritePointer (0);
        juce::Random rng (seed);
        const double freqs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        double ph[6] {};
        Svf hp1, hp2; hp1.set (7200.0, 0.7, DrumSources::rate); hp2.set (9800.0, 0.9, DrumSources::rate);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            double s = 0.0;
            for (int k = 0; k < 6; ++k)
            {
                ph[k] += freqs[k] * 1.47 / DrumSources::rate;
                ph[k] -= std::floor (ph[k]);
                s += ph[k] < 0.5 ? 1.0 : -1.0;
            }
            s = s / 6.0 + (rng.nextFloat() * 2.0 - 1.0) * 0.25;
            double lp, bp, hp, lp2, bp2, hpo;
            hp1.process (s, lp, bp, hp);
            hp2.process (hp, lp2, bp2, hpo);
            const double env = std::exp (-t / decay) * (1.0 - std::exp (-t / 0.0004));
            d[i] = (float) ((hpo * 0.7 + bp2 * 0.6) * env);
        }
        normalise (b, 0.85f);
        return b;
    }

    juce::AudioBuffer<float> tom()
    {
        auto b = alloc (1.0);
        auto* d = b.getWritePointer (0);
        juce::Random rng (606);
        Svf nz; nz.set (900.0, 0.8, DrumSources::rate);
        double ph = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            ph += twoPi * (98.0 + 70.0 * std::exp (-t / 0.07)) / DrumSources::rate;
            double lp, bp, hp;
            nz.process (rng.nextFloat() * 2.0 - 1.0, lp, bp, hp);
            const double s = std::sin (ph) * std::exp (-t / 0.32) + bp * 0.25 * std::exp (-t / 0.03);
            d[i] = (float) std::tanh (1.4 * s);
        }
        normalise (b, 0.9f);
        return b;
    }

    /** "Holz": a struck log / axe on wood. Inharmonic modes, very short. */
    juce::AudioBuffer<float> holz()
    {
        auto b = alloc (0.5);
        auto* d = b.getWritePointer (0);
        juce::Random rng (707);
        const double f[] { 612.0, 1130.0, 1747.0, 2630.0 };
        const double dc[] { 0.05, 0.03, 0.018, 0.01 };
        const double a[] { 1.0, 0.55, 0.35, 0.2 };
        Svf nz; nz.set (2400.0, 0.9, DrumSources::rate);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[k] * std::sin (twoPi * f[k] * t) * std::exp (-t / dc[k]);
            double lp, bp, hp;
            nz.process (rng.nextFloat() * 2.0 - 1.0, lp, bp, hp);
            s += bp * std::exp (-t / 0.004) * 0.8;
            d[i] = (float) s;
        }
        normalise (b, 0.9f);
        return b;
    }

    /** "Glocke": a small cow/Glöckler bell, additive with bell-like ratios. */
    juce::AudioBuffer<float> glocke()
    {
        auto b = alloc (1.8);
        auto* d = b.getWritePointer (0);
        const double base = 587.0;
        const double r[]  { 1.0, 1.51, 2.0, 2.76, 4.07, 5.43 };
        const double dc[] { 1.1, 0.55, 0.8, 0.35, 0.22, 0.12 };
        const double a[]  { 1.0, 0.6, 0.5, 0.45, 0.3, 0.2 };
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / DrumSources::rate;
            double s = 0;
            for (int k = 0; k < 6; ++k)
                s += a[k] * std::sin (twoPi * base * r[k] * t + k) * std::exp (-t / dc[k]);
            d[i] = (float) (s * (1.0 - std::exp (-t / 0.0006)));
        }
        normalise (b, 0.85f);
        return b;
    }
}

juce::AudioBuffer<float> DrumSources::make (int voice)
{
    switch (voice)
    {
        case 0:  return kick();
        case 1:  return snare();
        case 2:  return clap();
        case 3:  return metal (0.35, 0.045, 404);
        case 4:  return metal (1.1, 0.33, 505);
        case 5:  return tom();
        case 6:  return holz();
        default: return glocke();
    }
}
