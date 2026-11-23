# TRAUNWERK — development log

How v1.0 was built in one session (23 Nov 2026): the concept came from a
conversation, the code was written by Claude, and every feature was checked by
measuring it against a control in `tests/RenderTest.cpp`. Below is what went
wrong along the way and which measurement decided each fix.

## 1. Concept

Starting point: a granular drum machine in the trockenmoos style with features
other drum machines don't have, including a randomizer. Eight ideas were
proposed. The set chosen for v1.0: ANSCHLAG/KÖRPER split, density envelope,
VERWITTERUNG, KOMPOST, LAWINE (sandpile), MYZEL (spore network), ABDRUCK
(kit + groove from a recording) and BROTKRUMEN (randomizer with a morphable
history), plus SCHUTZ locks and per-row lengths. Name: TRAUNWERK. Format: VST3
like SPORALIA, cross-platform, in its own repo.

## 2. First build: everything compiled, two features were wrong

The first full test run passed most checks but failed two:

- **Lawine was supercritical.** In *Lawinenhang*, 729 ghost notes for 267
  pattern hits in 8 bars, and over half of all avalanches tipped ≥ 12 cells.
  The cause is in the physics. Grains only leave a sandpile at its edges, and
  the grid wrapped around in time, so it only had edges at the top and bottom
  rows. Every grain dropped had to travel a long way, which means many
  topples. Fixes: open edges in time; a little "bulk dissipation" (each
  sliding grain has a 12 % chance to blow away); lower feed scaling (max 3
  grains per 16th); a 40 % chance per topple to leave a ghost.
  Result: 49 % of avalanches tip ≤ 2 cells, 7 % tip ≥ 12, the largest 29.
  Calm stretches with occasional fills, which is the behaviour we wanted.

- **A snare roll got brighter without any wear.** That pointed to clicks.
  At 8 hits/s with a 1.4 s body, a voice needs more than its 6 slots, and the
  oldest hit was being cut off. Now there are 8 slots per voice, and once 6
  are ringing the oldest fades over 5 ms *before* a slot is needed. After
  that: wear 0 stays flat (0.52 → 0.47), wear 1 is 17× darker.

## 3. Kompost made things quieter, then louder

Compost grains first came out at about half level (0.12 vs 0.23 rms). The
compost recording already contains each voice's level and velocity, and the
grains applied them a second time. With that removed and an extra gain of 1.3,
the level rose to 0.38, because the compost feeds back into itself. At unity
gain it settles at the same level as without compost (0.20 vs 0.23) while
clearly changing the sound (difference signal 1.3× the original rms).

## 4. Abdruck: the hardest part

Test: render *Moos Minimal* (124 bpm, four-on-the-floor), load the WAV as
Abdruck, check that the tempo and the kick come back.

1. **Linear energy flux found onsets everywhere.** Folding the onset strength
   onto the 16th grid gave a flat histogram. Granular bodies crackle all the
   time, and in linear energy that crackle looks just like hits. Measuring
   the onset as a **jump in level (dB over two frames)**, weighted by
   loudness, concentrates the strength on the grid (histogram peak ~5–10×
   the floor).
2. **The tempo drifted.** The autocorrelation is only good to ~1 %, and over
   248 sixteenths that is 0.4 steps of drift, which smeared every row. Two
   fixes: a joint period-and-phase grid search (±3 %), then a weighted
   least-squares fit of time = phase + k · period. Now 124.0–124.1 bpm.
3. **Low-end detection followed the kick's tail.** An FFT frame of 21 ms is
   about one period of a 46 Hz kick tail, so the low band measured the
   tail's phase. A 150 Hz low-pass with 30 ms RMS windows (after minus
   before) finds the quarters.
4. **Swing came out with the wrong sign half the time.** The size was right
   (0.20), but the grid sometimes chose odd 16ths as the downbeat. The low
   band was too weak to decide that on its own. What does decide it: after
   centring the grid, swung off-beats are *late*, so the late class is the
   odd one. The quarters are then anchored at zero offset.
5. **Noisy offsets.** On very granular material a slot's hits can disagree by
   ±0.3 of a 16th. Per slot we now take the weighted median, and shrink it
   towards zero by how much the hits disagree. The first version of that
   shrink was too strict (a clean 0.20 swing came back as 0.05–0.14); with a
   looser tolerance it returns 0.12–0.20 across six runs.

Abdruck's pattern is a skeleton (KICK/SNARE/HAT by band energy), not a
transcription. Assigning every voice from a mix is beyond v1.0.

## 5. Small things

- Umlauts and "·" in the UI are now explicit UTF-8 byte escapes. A plain
  `const char*` is read as ASCII by JUCE, and MSVC would encode `\u` escapes
  in the system code page.
- Knob captions shrink to fit ("SCHWELLE", "ERHOLUNG").

## 6. TRAUNFALL (added the same evening)

The request was an "Autechre-style" switch. It is built as its own mode
with its own name, not as an imitation of an artist. What it adds is what the
sequencer was missing: control over time itself.

- **WARP** puts each step x (0..1 of the bar) at
  w(x) = x + a1·sin(2πx)/2π + a2·sin(4πx)/4π, with |a1|+|a2| ≤ 0.85·WARP.
  The slope stays positive, so steps never swap places, and w(0)=0, w(1)=1,
  so the bar never drifts against the host. a1 and a2 take a random walk
  from bar to bar.
- The first timing test detected onsets in the audio and failed in confusing
  ways: hat crackle produced extra onsets, and warped steps came closer than
  the detector could separate. It also exposed a real bug: **ghost notes left
  over from an earlier session survived `prepareToPlay`**. Now they are
  cleared. The test now reads trigger times from the engine's own sample
  clock. Result: off = exactly straight; WARP 1 = intervals vary by 1.3
  sixteenths, downbeats within 0.02 ms, 64 of 64 hats.
- The engine's random generator now has a fixed seed, so a test run is
  reproducible. Before that, one run in five or six failed somewhere on
  chance alone, which would have made CI red for nothing.

## Numbers at release (one run of the render test)

| Check | Result |
|---|---|
| Presets | peak 0.72–0.97, −9.8 … −14.1 dBFS rms, 1–4 % CPU |
| ANSCHLAG crest (first 3 ms) | 3.4 vs 0.4 |
| VERWITTERUNG | worn roll 17× darker; ~8× brighter again after 3 s rest |
| LAWINE | feed 0: 0 avalanches; feed 0.6: 45 in 16 s; 49 % ≤ 2 cells |
| MYZEL | 30 pattern hits → 21 spores at 0.85 |
| KOMPOST | difference 1.3× signal rms, level stable |
| BROTKRUMEN | 6 crumbs after 5 throws, exact return to crumb 0 |
| ABDRUCK | 124.1 bpm (true 124); swing 0.20 → 0.12–0.20 over six runs |
| 3 min torture run | finite, level constant, 7 % CPU |
| TRAUNFALL | downbeats ±0.02 ms, 64/64 steps; STOTTERN 1: 47 ratchet hits on 32 steps; ZERFASERN: attack difference 0.00 → 0.2–0.55 |
