#include "PluginEditor.h"
#include "Presets.h"

using namespace tw;

namespace
{
    juce::String u8 (const char* t) { return juce::String::fromUTF8 (t); }

    juce::Font font (float size, float kern = 0.2f)
    {
        return juce::Font (juce::FontOptions { size }).withExtraKerningFactor (kern);
    }

    const char* const shortNames[numVoices] { "KCK", "SNR", "CLP", "HAT", "OPN", "TOM", "HLZ", "GLK" };

    bool isAudioFile (const juce::String& f)
    {
        return f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3");
    }
}

//==============================================================================
Knob::Knob (const juce::String& l) : label (l)
{
    slider.setName (l);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.onValueChange = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& s, const juce::String& paramId)
{
    att.reset();
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    repaint();
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (16));
}

void Knob::paint (juce::Graphics& g)
{
    const bool touched = slider.isMouseOverOrDragging();
    g.setColour (touched ? twc::accent : twc::dim);
    g.setFont (font (9.5f, 0.25f));
    const auto txt = touched ? slider.getTextFromValue (slider.getValue()) : label;
    g.drawFittedText (txt, getLocalBounds().removeFromBottom (15), juce::Justification::centred, 1, 0.6f);
}

//==============================================================================
StepGrid::StepGrid (TraunwerkProcessor& p) : proc (p) {}

int StepGrid::rowAt (int y) const
{
    const int r = (y - headerH) / rowH;
    return y < headerH ? -1 : juce::jlimit (-1, numVoices - 1, r);
}

int StepGrid::colAt (int x) const
{
    const int c = (x - nameW - 4) / cellW;
    return x < nameW + 4 ? -1 : (c < numSteps ? c : -1);
}

juce::Rectangle<float> StepGrid::cellRect (int v, int c) const
{
    return { (float) (nameW + 4 + c * cellW), (float) (headerH + v * rowH + 3),
             (float) (cellW - 4), (float) (rowH - 6) };
}

juce::Rectangle<float> StepGrid::lockRect (int v) const
{
    return { (float) (nameW - 22), (float) (headerH + v * rowH + 6), 16.0f, 16.0f };
}

void StepGrid::paint (juce::Graphics& g)
{
    // header: step numbers and the ABDRUCK groove as small displaced ticks
    g.setFont (font (9.0f));
    for (int c = 0; c < numSteps; ++c)
    {
        const auto r = cellRect (0, c);
        g.setColour (c % 4 == 0 ? twc::text.withAlpha (0.7f) : twc::dim.withAlpha (0.6f));
        g.drawText (juce::String (c + 1), (int) r.getX(), 0, (int) r.getWidth(), 12, juce::Justification::centred);

        const float off = proc.getGrooveOffset (c) * proc.apvts.getRawParameterValue (id::groove)->load();
        const float x = r.getCentreX() + off * (float) cellW;
        g.setColour (twc::accent.withAlpha (std::abs (off) > 0.005f ? 0.8f : 0.2f));
        g.drawLine (x, 14.0f, x, 19.0f, 1.2f);
    }

    for (int v = 0; v < numVoices; ++v)
    {
        const int y = headerH + v * rowH;
        const bool sel = v == proc.selectedVoice;
        const bool locked = proc.isLocked (v);
        const int len = proc.getLength (v);
        const int play = proc.getPlayColumn (v);

        // name area
        auto nameR = juce::Rectangle<float> (0.0f, (float) y + 3.0f, (float) nameW - 4.0f, (float) rowH - 6.0f);
        g.setColour (sel ? twc::accentSft.withAlpha (0.25f) : twc::panel);
        g.fillRoundedRectangle (nameR, 2.0f);
        if (flash[(size_t) v] > 0.01f)
        {
            g.setColour (twc::accent.withAlpha (0.35f * flash[(size_t) v]));
            g.fillRoundedRectangle (nameR, 2.0f);
        }
        if (dropRow == v)
        {
            g.setColour (twc::accent);
            g.drawRoundedRectangle (nameR, 2.0f, 1.5f);
        }
        g.setColour (sel ? twc::accent : twc::text);
        g.setFont (font (12.0f, 0.25f));
        g.drawText (voiceNames[v], 8, y + 5, 80, 18, juce::Justification::centredLeft);

        // source name / wear meter
        const auto srcName = proc.getVoiceSourceName (v);
        g.setColour (twc::dim);
        g.setFont (font (8.5f, 0.1f));
        g.drawText (srcName.isEmpty() ? "intern" : srcName, 8, y + 21, nameW - 34, 10, juce::Justification::centredLeft, true);
        const float w = proc.engine.getWear (v);
        g.setColour (twc::line);
        g.fillRect (8.0f, (float) y + 33.0f, (float) nameW - 30.0f, 2.0f);
        g.setColour (twc::humus.interpolatedWith (twc::accent, 1.0f - w));
        g.fillRect (8.0f, (float) y + 33.0f, ((float) nameW - 30.0f) * w, 2.0f);

        // SCHUTZ lock
        const auto lr = lockRect (v);
        g.setColour (locked ? twc::sand : twc::dim.withAlpha (0.45f));
        g.drawRoundedRectangle (lr.withTrimmedTop (6.0f).reduced (2.0f, 0.0f), 1.5f, 1.0f);
        juce::Path shackle;
        shackle.addCentredArc (lr.getCentreX(), lr.getY() + 6.0f, 3.5f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.0f));
        if (locked) g.fillRoundedRectangle (lr.withTrimmedTop (6.0f).reduced (2.0f, 0.0f), 1.5f);

        // cells
        for (int c = 0; c < numSteps; ++c)
        {
            auto r = cellRect (v, c);
            if (c >= len)
            {
                g.setColour (twc::bg.brighter (0.03f));
                g.fillRoundedRectangle (r, 2.0f);
                continue;
            }
            const int st = proc.getStep (v, c);
            g.setColour (c % 4 == 0 ? twc::panel.brighter (0.06f) : twc::panel);
            g.fillRoundedRectangle (r, 2.0f);

            if (st == 1) { g.setColour (twc::accentSft); g.fillRoundedRectangle (r.reduced (3.0f), 2.0f); }
            if (st == 2) { g.setColour (twc::accent.withAlpha (0.85f)); g.fillRoundedRectangle (r.reduced (3.0f), 2.0f); }

            const float gh = proc.getGhost (v, c);
            if (gh > 0.0f)
            {
                g.setColour (twc::sand.withAlpha (0.35f + 0.5f * gh));
                g.drawEllipse (r.withSizeKeepingCentre (14.0f, 14.0f), 1.3f);
            }

            // sand grains
            const int sand = juce::jmin (9, proc.getSand (v, c));
            g.setColour (twc::sand.withAlpha (st == 2 ? 0.9f : 0.6f));
            for (int k = 0; k < sand; ++k)
                g.fillEllipse (r.getX() + 4.0f + (float) (k % 5) * 6.0f,
                               r.getBottom() - 6.0f - (float) (k / 5) * 4.0f, 2.4f, 2.4f);

            g.setColour (play == c ? twc::accent.withAlpha (0.9f) : twc::line);
            g.drawRoundedRectangle (r, 2.0f, play == c ? 1.4f : 0.8f);
        }

        // length marker
        const auto lastR = cellRect (v, len - 1);
        g.setColour (twc::accent.withAlpha (len < numSteps ? 0.8f : 0.25f));
        g.fillRect (lastR.getRight() + 1.0f, lastR.getY() + 4.0f, 2.0f, lastR.getHeight() - 8.0f);
        if (len < numSteps)
        {
            g.setFont (font (9.0f));
            g.drawText (juce::String (len), (int) cellRect (v, numSteps - 1).getRight() + 4, y, 30, rowH,
                        juce::Justification::centredLeft);
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int v = rowAt (e.y);
    if (v < 0) return;
    const int c = colAt (e.x);

    if (c < 0)
    {
        if (lockRect (v).expanded (3.0f).contains (e.position))
        {
            proc.setLocked (v, ! proc.isLocked (v));
            repaint();
            return;
        }
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Load sample...");
            m.addItem (2, "Built-in sound");
            m.addItem (3, proc.isLocked (v) ? "Unlock (SCHUTZ off)" : "Lock (SCHUTZ)");
            m.addItem (4, "Clear row");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, v] (int r)
            {
                if (r == 1 && onLoadSample) onLoadSample (v);
                if (r == 2) proc.resetVoiceSample (v);
                if (r == 3) proc.setLocked (v, ! proc.isLocked (v));
                if (r == 4) for (int s = 0; s < numSteps; ++s) proc.setStep (v, s, 0);
                repaint();
            });
            return;
        }
        if (onSelect) onSelect (v);
        proc.audition (v);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Length: " + juce::String (c + 1) + " steps");
        m.addItem (2, "Length: 16 steps");
        m.addItem (3, "Clear row");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, v, c] (int r)
        {
            if (r == 1) proc.setLength (v, c + 1);
            if (r == 2) proc.setLength (v, numSteps);
            if (r == 3) for (int s = 0; s < numSteps; ++s) proc.setStep (v, s, 0);
            repaint();
        });
        return;
    }
    if (e.mods.isShiftDown()) { proc.setLength (v, c + 1); repaint(); return; }

    const int cur = proc.getStep (v, c);
    dragValue = e.mods.isAltDown() ? 2 : (cur + 1) % 3;
    dragRow = v;
    proc.setStep (v, c, dragValue);
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0) return;
    const int c = colAt (e.x);
    if (c >= 0 && proc.getStep (dragRow, c) != dragValue) { proc.setStep (dragRow, c, dragValue); repaint(); }
}

void StepGrid::mouseUp (const juce::MouseEvent&) { dragRow = -1; }

bool StepGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void StepGrid::filesDropped (const juce::StringArray& files, int, int y)
{
    const int v = rowAt (y);
    dropRow = -1;
    if (v >= 0)
        for (auto& f : files)
            if (isAudioFile (f)) { proc.loadVoiceSample (v, juce::File (f)); break; }
    repaint();
}

//==============================================================================
void GrainCloud::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (twc::bg);
    g.fillRoundedRectangle (r, 2.0f);

    // the source as faint horizontal bands: brighter where it is louder
    auto src = proc.engine.getSource (voice);
    if (src.get() != cacheSrc || cacheVoice != voice)
    {
        cacheSrc = src.get();
        cacheVoice = voice;
        profile.assign (48, 0.0f);
        if (src != nullptr)
        {
            const int n = src->data.getNumSamples();
            const float* d = src->data.getReadPointer (0);
            for (int i = 0; i < n; ++i)
            {
                auto& p = profile[(size_t) (i * 48 / juce::jmax (1, n))];
                p = juce::jmax (p, std::abs (d[i]));
            }
        }
    }
    const float bandH = r.getHeight() / 48.0f;
    for (int i = 0; i < 48; ++i)
    {
        g.setColour (twc::accent.withAlpha (0.03f + 0.12f * profile[(size_t) i]));
        g.fillRect (r.getX(), r.getBottom() - (float) (i + 1) * bandH, r.getWidth(), bandH - 0.5f);
    }

    const float decay = proc.apvts.getRawParameterValue (voiceParamId (voice, pDecay))->load();
    const float span = juce::jlimit (0.15f, 3.0f, decay * 3.0f);

    for (auto& d : dots)
    {
        const float x = r.getX() + r.getWidth() * juce::jmin (1.0f, d.t / span);
        const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, d.pos);
        const float s = 1.5f + juce::jmin (4.0f, d.amp * 5.0f);
        g.setColour ((d.compost ? twc::humus : twc::accent).withAlpha (juce::jlimit (0.15f, 0.9f, 0.25f + d.amp)));
        g.fillEllipse (x - s * 0.5f, y - s * 0.5f, s, s);
    }

    g.setColour (twc::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    g.setColour (twc::dim);
    g.setFont (font (8.5f));
    g.drawText (juce::String (span, 2) + " s", getLocalBounds().reduced (6, 3), juce::Justification::bottomRight);
    g.drawText (u8 ("K\xc3\x96RNER"), getLocalBounds().reduced (6, 3), juce::Justification::topLeft);
}

//==============================================================================
juce::Point<float> MyzelView::node (int v) const
{
    const auto c = getLocalBounds().toFloat().withTrimmedBottom (18.0f).getCentre();
    const float rad = juce::jmin ((float) getWidth(), (float) getHeight() - 18.0f) * 0.5f - 16.0f;
    const float a = juce::MathConstants<float>::twoPi * (float) v / (float) numVoices - juce::MathConstants<float>::halfPi;
    return { c.x + std::cos (a) * rad, c.y + std::sin (a) * rad };
}

juce::Path MyzelView::thread (int from, int to) const
{
    const auto a = node (from), b = node (to);
    const auto c = getLocalBounds().toFloat().withTrimmedBottom (18.0f).getCentre();
    auto mid = (a + b) * 0.5f;
    mid = mid + (c - mid) * 0.45f;
    const auto dir = (b - a);
    const float len = juce::jmax (1.0f, dir.getDistanceFromOrigin());
    const juce::Point<float> perp { -dir.y / len, dir.x / len };
    mid = mid + perp * 9.0f;
    juce::Path p;
    p.startNewSubPath (a);
    p.quadraticTo (mid, b);
    return p;
}

void MyzelView::paint (juce::Graphics& g)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < numVoices; ++i)
        for (int j = 0; j < numVoices; ++j)
        {
            const float w = proc.getMyzel (i, j);
            if (w <= 0.0f) continue;
            const auto p = thread (i, j);
            g.setColour ((i == selected ? twc::accent : twc::lichen2).withAlpha (0.25f + 0.6f * w));
            g.strokePath (p, juce::PathStrokeType (0.6f + 1.8f * w, juce::PathStrokeType::curved));
            // a small spore near the receiving end shows the direction
            const auto tip = p.getPointAlongPath (p.getLength() * 0.82f);
            g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
        }

    // spores travelling along their threads
    live.erase (std::remove_if (live.begin(), live.end(), [now] (const Live& l) { return now - l.t0 > 380.0; }), live.end());
    for (auto& l : live)
    {
        const auto p = thread (l.from, l.to);
        const float f = (float) ((now - l.t0) / 380.0);
        const auto pt = p.getPointAlongPath (p.getLength() * f);
        g.setColour (twc::sand.withAlpha (1.0f - f * 0.6f));
        g.fillEllipse (pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
    }

    for (int v = 0; v < numVoices; ++v)
    {
        const auto c = node (v);
        const float fl = flash[(size_t) v];
        g.setColour (twc::panel);
        g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f);
        if (fl > 0.01f)
        {
            g.setColour (twc::accent.withAlpha (0.5f * fl));
            g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f);
        }
        g.setColour (v == selected ? twc::accent : twc::line.brighter (0.3f));
        g.drawEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f, v == selected ? 1.6f : 1.0f);
        g.setColour (twc::text);
        g.setFont (font (8.5f, 0.15f));
        g.drawText (shortNames[v], juce::Rectangle<float> (c.x - 13.0f, c.y - 6.0f, 26.0f, 12.0f), juce::Justification::centred);
    }

    g.setColour (twc::dim);
    g.setFont (font (8.5f, 0.1f));
    g.drawText (selected < 0 ? "click a node, then another" : u8 ("click targets \xc2\xb7 right-click clears"),
                getLocalBounds().removeFromBottom (14), juce::Justification::centred);
}

void MyzelView::mouseDown (const juce::MouseEvent& e)
{
    int hit = -1;
    for (int v = 0; v < numVoices; ++v)
        if (node (v).getDistanceFrom (e.position) < 15.0f) hit = v;

    if (hit < 0) { selected = -1; repaint(); return; }

    if (e.mods.isPopupMenu())
    {
        for (int j = 0; j < numVoices; ++j) proc.setMyzel (hit, j, 0.0f);
        repaint();
        return;
    }

    if (selected < 0 || selected == hit) selected = selected == hit ? -1 : hit;
    else
    {
        const float w = proc.getMyzel (selected, hit);
        const float next = w <= 0.0f ? 0.3f : w < 0.5f ? 0.6f : w < 0.8f ? 0.9f : 0.0f;
        proc.setMyzel (selected, hit, next);
    }
    repaint();
}

//==============================================================================
void TrailView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int n = proc.getTrailSize();
    const float y = r.getCentreY() - 6.0f;
    const float x0 = r.getX() + 8.0f, x1 = r.getRight() - 8.0f;

    g.setColour (twc::line);
    g.drawLine (x0, y, x1, y, 1.0f);

    juce::Random jitter (77);
    for (int i = 0; i < n; ++i)
    {
        const float x = n > 1 ? x0 + (x1 - x0) * (float) i / (float) (n - 1) : x0;
        const float dy = (jitter.nextFloat() - 0.5f) * 6.0f;
        g.setColour (twc::sand.withAlpha (0.75f));
        g.fillEllipse (x - 2.0f, y + dy - 2.0f, 4.0f, 4.0f);
    }

    if (n > 0)
    {
        const float pos = proc.getTrailPos();
        const float x = n > 1 ? x0 + (x1 - x0) * pos / (float) (n - 1) : x0;
        g.setColour (twc::accent);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
    }

    g.setColour (twc::dim);
    g.setFont (font (8.5f, 0.1f));
    g.drawText (n == 0 ? u8 ("noch keine Kr\xc3\xbcmel") : juce::String (n) + u8 (" Kr\xc3\xbcmel \xc2\xb7 ziehen zum Morphen"),
                getLocalBounds().removeFromBottom (14), juce::Justification::centred);
}

void TrailView::scrub (const juce::MouseEvent& e)
{
    const int n = proc.getTrailSize();
    if (n < 1) return;
    const float x0 = 8.0f, x1 = (float) getWidth() - 8.0f;
    const float f = juce::jlimit (0.0f, 1.0f, (e.position.x - x0) / (x1 - x0));
    proc.scrubTrail (f * (float) (n - 1));
    repaint();
}

//==============================================================================
TraunwerkEditor::TraunwerkEditor (TraunwerkProcessor& p)
    : AudioProcessorEditor (&p), proc (p), grid (p), cloud (p), myzelView (p), trailView (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (grid);
    addAndMakeVisible (cloud);
    addAndMakeVisible (myzelView);
    addAndMakeVisible (trailView);
    grid.onSelect = [this] (int v) { selectVoice (v); };
    grid.onLoadSample = [this] (int v) { chooseVoiceSample (v); };

    for (int q = 0; q < numVoiceParams; ++q)
    {
        auto k = std::make_unique<Knob> (voiceSpecs[q].label);
        addAndMakeVisible (*k);
        voiceKnobs.push_back (std::move (k));
    }

    auto& s = proc.apvts;
    for (auto [k, pid] : std::initializer_list<std::pair<Knob*, const char*>> {
             { &feed, id::feed }, { &thresh, id::thresh }, { &spur, id::spur }, { &ghostK, id::ghost },
             { &myzelAmt, id::myzel }, { &kompost, id::kompost }, { &recover, id::recover },
             { &groove, id::groove }, { &swing, id::swing }, { &rndAmt, id::rndAmt },
             { &drive, id::drive }, { &master, id::master },
             { &tfWarp, id::tfWarp }, { &tfStutter, id::tfStutter }, { &tfShuffle, id::tfShuffle }, { &tfFray, id::tfFray } })
    {
        addAndMakeVisible (*k);
        k->attach (s, pid);
    }

    myzelDiv.addItemList (myzelDivNames, 1);
    rndTarget.addItemList (rndTargetNames, 1);
    addAndMakeVisible (myzelDiv);
    addAndMakeVisible (rndTarget);
    myzelDivAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id::myzelDiv, myzelDiv);
    rndTargetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id::rndTarget, rndTarget);

    for (auto* b : { &prevPreset, &nextPreset, &presetName })
    {
        b->setComponentID (TwLookAndFeel::flatId);
        addAndMakeVisible (*b);
    }
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { showPresetMenu(); };

    tfButton.setClickingTogglesState (true);
    tfButton.setTooltip ("TRAUNFALL: elastic time, ratchets, reshuffled steps and frayed hits");
    addAndMakeVisible (tfButton);
    tfAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id::tfOn, tfButton);
    tfButton.onStateChange = [this] { repaint (tfR); };

    playButton.setClickingTogglesState (true);
    addAndMakeVisible (playButton);
    playAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id::play, playButton);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.onValueChange = [this] { repaint (900, 0, 300, 54); };
    addAndMakeVisible (bpmSlider);
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id::bpm, bpmSlider);

    for (auto* b : { &lawineButton, &freshButton, &abdruckButton, &rndButton, &voiceLoad, &voiceReset })
        addAndMakeVisible (*b);
    lawineButton.onClick  = [this] { proc.triggerLawine(); };
    freshButton.onClick   = [this] { proc.freshCompost(); };
    abdruckButton.onClick = [this] { chooseAbdruck(); };
    rndButton.onClick     = [this] { proc.randomize(); grid.repaint(); trailView.repaint(); myzelView.repaint(); };
    voiceLoad.onClick     = [this] { chooseVoiceSample (proc.selectedVoice); };
    voiceReset.onClick    = [this] { proc.resetVoiceSample (proc.selectedVoice); };

    lawineButton.setTooltip ("Drop a pile of sand: forces an avalanche at the next step");
    rndButton.setTooltip ("Randomise. Every throw leaves a crumb on the trail below");

    setSize (1200, 800);
    selectVoice (proc.selectedVoice);
    refreshLabels();
    startTimerHz (30);
}

TraunwerkEditor::~TraunwerkEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TraunwerkEditor::selectVoice (int v)
{
    proc.selectedVoice = juce::jlimit (0, numVoices - 1, v);
    for (int q = 0; q < numVoiceParams; ++q)
        voiceKnobs[(size_t) q]->attach (proc.apvts, voiceParamId (proc.selectedVoice, q));
    cloud.setVoice (proc.selectedVoice);
    grid.repaint();
    repaint (voiceR);
}

void TraunwerkEditor::chooseVoiceSample (int v)
{
    chooser = std::make_unique<juce::FileChooser> ("Sample for " + juce::String (voiceNames[v]),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, v] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) proc.loadVoiceSample (v, f);
        cloud.setVoice (proc.selectedVoice);
        repaint();
    });
}

void TraunwerkEditor::chooseAbdruck()
{
    chooser = std::make_unique<juce::FileChooser> ("ABDRUCK: a recording to take kit, groove and pattern from",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() && ! proc.loadAbdruck (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ABDRUCK",
                                                    "Could not find enough hits in that recording.");
        cloud.setVoice (proc.selectedVoice);
        refreshLabels();
        repaint();
    });
}

void TraunwerkEditor::showPresetMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        m.addItem (i + 1, proc.getProgramName (i), true, i == proc.getCurrentProgram());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [this] (int r)
    {
        if (r > 0) { proc.applyPreset (r - 1); refreshLabels(); repaint(); }
    });
}

void TraunwerkEditor::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    proc.applyPreset ((proc.getCurrentProgram() + delta + n) % n);
    refreshLabels();
    repaint();
}

void TraunwerkEditor::refreshLabels()
{
    presetName.setButtonText (proc.getProgramName (proc.getCurrentProgram()).toUpperCase());
    lastPresetCount = proc.getPresetChangeCount();
}

void TraunwerkEditor::timerCallback()
{
    // hits
    {
        const auto scope = proc.engine.hitFifo.read (proc.engine.hitFifo.getNumReady());
        auto handle = [this] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& h = proc.engine.hitBuf[(size_t) i];
                grid.flash[(size_t) h.voice] = 1.0f;
                myzelView.flash[(size_t) h.voice] = 1.0f;
                if (h.voice == proc.selectedVoice) cloud.newHit();
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }
    // grains
    {
        const auto scope = proc.engine.grainFifo.read (proc.engine.grainFifo.getNumReady());
        auto handle = [this] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& d = proc.engine.grainBuf[(size_t) i];
                if (d.voice == proc.selectedVoice) cloud.add (d);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }
    // spores
    {
        const auto scope = proc.sporeFifo.read (proc.sporeFifo.getNumReady());
        for (int i = scope.startIndex1; i < scope.startIndex1 + scope.blockSize1; ++i) myzelView.addSpore (proc.sporeBuf[(size_t) i]);
        for (int i = scope.startIndex2; i < scope.startIndex2 + scope.blockSize2; ++i) myzelView.addSpore (proc.sporeBuf[(size_t) i]);
    }

    for (auto& f : grid.flash) f *= 0.82f;
    for (auto& f : myzelView.flash) f *= 0.82f;

    if (proc.getAvalancheCount() != lastAvalancheCount)
    {
        lastAvalancheCount = proc.getAvalancheCount();
        avalancheFlash = juce::jmin (1.0f, 0.3f + (float) proc.getLastAvalanche() / 20.0f);
    }
    avalancheFlash *= 0.9f;

    if (proc.getRatchetCount() != lastRatchetCount) { lastRatchetCount = proc.getRatchetCount(); ratchetFlash = 1.0f; }
    ratchetFlash *= 0.85f;

    if (proc.getPresetChangeCount() != lastPresetCount)
    {
        refreshLabels();
        selectVoice (proc.selectedVoice);
    }

    grid.repaint();
    cloud.repaint();
    myzelView.repaint();
    trailView.repaint();
    repaint (lawineR);
    repaint (tfR);
    repaint (kompostR);
    repaint (abdruckR);
    repaint (voiceR.withHeight (30));
    repaint (1100, 0, 100, 54);
}

//==============================================================================
void TraunwerkEditor::panel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& sub)
{
    g.setColour (twc::panel);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
    g.setColour (twc::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (twc::accent);
    g.setFont (font (11.0f, 0.3f));
    g.drawText (title, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14, juce::Justification::centredLeft);
    if (sub.isNotEmpty())
    {
        g.setColour (twc::dim);
        g.setFont (font (8.5f, 0.15f));
        g.drawText (sub, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14, juce::Justification::centredRight);
    }
}

void TraunwerkEditor::paint (juce::Graphics& g)
{
    g.fillAll (twc::bg);

    // faint moss texture in the background
    juce::Random rr (1234);
    for (int i = 0; i < 900; ++i)
    {
        g.setColour (twc::accent.withAlpha (0.015f + rr.nextFloat() * 0.03f));
        const float x = rr.nextFloat() * (float) getWidth(), y = rr.nextFloat() * (float) getHeight();
        g.fillEllipse (x, y, 1.6f, 1.6f);
    }

    // header
    g.setColour (twc::text);
    g.setFont (font (26.0f, 0.42f));
    g.drawText ("TRAUNWERK", 16, 8, 300, 30, juce::Justification::centredLeft);
    g.setColour (twc::dim);
    g.setFont (font (9.5f, 0.3f));
    g.drawText (u8 ("GRANULAR DRUM MACHINE  \xc2\xb7  TROCKENMOOS  \xc2\xb7  v" JucePlugin_VersionString), 18, 36, 400, 12,
                juce::Justification::centredLeft);

    g.setColour (twc::dim);
    g.setFont (font (9.5f, 0.2f));
    g.drawText ("BPM " + juce::String ((int) std::round (bpmSlider.getValue())), 1000, 40, 120, 12, juce::Justification::centred);
    g.setColour (proc.isHostPlaying() ? twc::accent : twc::dim.withAlpha (0.5f));
    g.drawText (proc.isHostPlaying() ? "HOST SYNC" : "HOST", 1124, 22, 72, 14, juce::Justification::centredRight);

    // voice panel
    panel (g, voiceR, juce::String (voiceNames[proc.selectedVoice]));
    {
        const auto name = proc.getVoiceSourceName (proc.selectedVoice);
        g.setColour (twc::dim);
        g.setFont (font (9.0f, 0.12f));
        g.drawText (name.isEmpty() ? "intern" : name, voiceR.getX() + 80, voiceR.getY() + 6, 160, 14,
                    juce::Justification::centredLeft, true);
        g.setColour (twc::dim.withAlpha (0.8f));
        g.setFont (font (8.5f, 0.2f));
        g.drawText ("ANSCHLAG", voiceR.getX() + 10, voiceR.getY() + 240, 120, 10, juce::Justification::centredLeft);
        g.drawText (u8 ("K\xc3\x96RPER"), voiceR.getX() + 10, voiceR.getY() + 154, 120, 10, juce::Justification::centredLeft);
    }

    // LAWINE
    panel (g, lawineR, "LAWINE", "sandpile randomizer");
    {
        const float fill = proc.getSandFill();
        const int level = juce::jlimit (1, 5, 1 + (int) (fill * 5.0f));
        const juce::Colour lc[] { twc::accent, twc::accent.interpolatedWith (twc::sand, 0.5f), twc::sand,
                                  twc::sand.interpolatedWith (twc::humus, 0.5f), twc::humus.brighter (0.2f) };
        const int bx = lawineR.getX() + 12, by = lawineR.getY() + 126;
        for (int i = 0; i < 5; ++i)
        {
            auto b = juce::Rectangle<float> ((float) (bx + i * 26), (float) by, 22.0f, 22.0f);
            g.setColour (i < level ? lc[i].withAlpha (0.85f) : twc::line);
            g.fillRoundedRectangle (b, 2.0f);
            g.setColour (i < level ? twc::bg : twc::dim);
            g.setFont (font (10.0f, 0.0f));
            g.drawText (juce::String (i + 1), b, juce::Justification::centred);
        }
        g.setColour (twc::dim);
        g.setFont (font (8.5f, 0.2f));
        g.drawText ("WARNSTUFE", bx + 136, by + 1, 100, 10, juce::Justification::centredLeft);
        g.setColour (twc::text);
        g.setFont (font (11.0f, 0.2f));
        g.drawText (juce::String (level), bx + 136, by + 11, 60, 12, juce::Justification::centredLeft);

        g.setColour (twc::sand.withAlpha (0.4f + 0.6f * avalancheFlash));
        g.setFont (font (9.0f, 0.15f));
        g.drawText ("letzte Lawine: " + juce::String (proc.getLastAvalanche()) + " Kippungen",
                    bx, by + 34, 230, 12, juce::Justification::centredLeft);
        if (avalancheFlash > 0.05f)
        {
            g.setColour (twc::sand.withAlpha (0.25f * avalancheFlash));
            g.fillRoundedRectangle (lawineR.toFloat().reduced (2.0f), 3.0f);
        }
    }

    panel (g, myzelR, "MYZEL", "spore network");

    // KOMPOST + VERWITTERUNG
    panel (g, kompostR, "KOMPOST");
    {
        const auto c = juce::Point<float> ((float) kompostR.getX() + 54.0f, (float) kompostR.getY() + 82.0f);
        const float amt = proc.apvts.getRawParameterValue (id::kompost)->load();
        for (int i = 0; i < DrumEngine::numCompostBins; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) DrumEngine::numCompostBins;
            const float v = proc.engine.getCompostBin (i);
            const float r0 = 22.0f, r1 = r0 + 4.0f + v * 20.0f;
            g.setColour (twc::humus.withAlpha (0.25f + 0.65f * amt));
            g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, 1.6f);
        }
        g.setColour (twc::text);
        g.setFont (font (10.0f, 0.1f));
        g.drawText (juce::String (juce::roundToInt (amt * 100.0f)) + "%", juce::Rectangle<float> (c.x - 20.0f, c.y - 7.0f, 40.0f, 14.0f),
                    juce::Justification::centred);

        // VERWITTERUNG: wear of every voice
        g.setColour (twc::accent);
        g.setFont (font (10.0f, 0.3f));
        g.drawText ("VERWITTERUNG", kompostR.getX() + 10, kompostR.getY() + 142, 110, 12, juce::Justification::centredLeft);
        for (int v = 0; v < numVoices; ++v)
        {
            const float w = proc.engine.getWear (v);
            const float x = (float) kompostR.getX() + 12.0f + (float) v * 11.0f;
            const float y0 = (float) kompostR.getY() + 164.0f, h = 60.0f;
            g.setColour (twc::line);
            g.fillRect (x, y0, 6.0f, h);
            g.setColour (twc::humus.interpolatedWith (twc::accent, 1.0f - w));
            g.fillRect (x, y0 + h * (1.0f - w), 6.0f, h * w);
            g.setColour (twc::dim);
            g.setFont (font (7.5f, 0.0f));
            g.drawText (juce::String::charToString (voiceNames[v][0]), (int) x - 2, (int) (y0 + h + 2), 10, 9, juce::Justification::centred);
        }
    }

    // ABDRUCK
    panel (g, abdruckR, "ABDRUCK", "kit + groove");
    {
        const auto name = proc.getAbdruckName();
        g.setColour (name.isEmpty() ? twc::dim : twc::text);
        g.setFont (font (9.5f, 0.1f));
        g.drawText (name.isEmpty() ? "drop in any recording" : name, abdruckR.getX() + 10, abdruckR.getY() + 62,
                    abdruckR.getWidth() - 20, 12, juce::Justification::centredLeft, true);
        g.setColour (twc::dim);
        g.setFont (font (8.5f, 0.1f));
        g.drawText (proc.getAbdruckInfo(), abdruckR.getX() + 10, abdruckR.getY() + 76, abdruckR.getWidth() - 20, 12,
                    juce::Justification::centredLeft, true);

        // groove ticks
        const float gx = (float) abdruckR.getX() + 12.0f, gw = (float) abdruckR.getWidth() - 24.0f;
        const float gy = (float) abdruckR.getY() + 98.0f;
        g.setColour (twc::line);
        g.drawLine (gx, gy + 12.0f, gx + gw, gy + 12.0f, 1.0f);
        for (int s = 0; s < numSteps; ++s)
        {
            const float slot = gw / numSteps;
            const float x = gx + slot * ((float) s + 0.5f);
            const float off = proc.getGrooveOffset (s);
            g.setColour (twc::dim.withAlpha (0.5f));
            g.drawLine (x, gy + 9.0f, x, gy + 15.0f, 1.0f);
            g.setColour (twc::accent);
            g.fillEllipse (x + off * slot - 2.0f, gy + 2.0f + (s % 4 == 0 ? 0.0f : 3.0f), 4.0f, 4.0f);
        }
    }

    panel (g, trailR, "BROTKRUMEN", "randomizer");

    // TRAUNFALL
    {
        const bool on = proc.isTraunfallOn();
        panel (g, tfR, "", "");
        g.setColour (twc::dim);
        g.setFont (font (8.5f, 0.15f));
        g.drawText ("Zeit, die bricht", tfR.getX() + 12, tfR.getY() + 50, 92, 14, juce::Justification::centred);

        // the current bar: straight 16ths on top, where TRAUNFALL puts them below
        const float x0 = (float) tfR.getX() + 480.0f, x1 = (float) tfR.getRight() - 20.0f;
        const float yTop = (float) tfR.getY() + 22.0f, yBot = (float) tfR.getBottom() - 22.0f;
        g.setColour (twc::line);
        g.drawLine (x0, yTop, x1, yTop, 1.0f);
        g.drawLine (x0, yBot, x1, yBot, 1.0f);
        for (int st = 0; st <= 16; ++st)
        {
            const float xs = x0 + (x1 - x0) * (float) st / 16.0f;
            const float xw = x0 + (x1 - x0) * proc.getWarpedPosition ((float) st);
            g.setColour (twc::dim.withAlpha (st % 4 == 0 ? 0.8f : 0.4f));
            g.drawLine (xs, yTop - 4.0f, xs, yTop + 4.0f, 1.0f);
            g.setColour ((on ? twc::accent : twc::dim).withAlpha (on ? 0.35f : 0.15f));
            g.drawLine (xs, yTop + 4.0f, xw, yBot - 4.0f, 1.0f);
            g.setColour (on ? twc::accent : twc::dim.withAlpha (0.4f));
            g.fillEllipse (xw - 2.5f, yBot - 2.5f, 5.0f, 5.0f);
        }
        g.setColour (twc::dim);
        g.setFont (font (8.0f, 0.15f));
        g.drawText ("GERADE", (int) x0 - 52, (int) yTop - 6, 46, 12, juce::Justification::centredRight);
        g.drawText ("TRAUNFALL", (int) x0 - 62, (int) yBot - 6, 56, 12, juce::Justification::centredRight);
        if (on && ratchetFlash > 0.05f)
        {
            g.setColour (twc::sand.withAlpha (ratchetFlash));
            g.setFont (font (9.0f, 0.2f));
            g.drawText ("STOTTERN", (int) x1 - 70, (int) yBot + 4, 70, 12, juce::Justification::centredRight);
        }
        if (! on)
        {
            g.setColour (twc::bg.withAlpha (0.45f));
            g.fillRoundedRectangle (tfR.toFloat().withTrimmedLeft (110.0f).reduced (2.0f), 3.0f);
        }
    }
    panel (g, masterR, "MASTER");
}

void TraunwerkEditor::resized()
{
    gridR    = { 16, 62, 752, StepGrid::headerH + numVoices * StepGrid::rowH };
    voiceR   = { 784, 62, 400, gridR.getHeight() };
    const int by = 436, bh = 248;
    lawineR  = { 16, by, 250, bh };
    myzelR   = { 274, by, 240, bh };
    kompostR = { 522, by, 180, bh };
    abdruckR = { 710, by, 190, bh };
    trailR   = { 908, by, 180, bh };
    masterR  = { 1096, by, 88, bh };
    tfR      = { 16, by + bh + 8, 1168, 92 };

    grid.setBounds (gridR);

    // header
    prevPreset.setBounds (430, 16, 24, 24);
    presetName.setBounds (456, 16, 240, 24);
    nextPreset.setBounds (698, 16, 24, 24);
    playButton.setBounds (928, 16, 64, 22);
    bpmSlider.setBounds (1000, 16, 120, 22);

    // voice panel
    voiceLoad.setBounds  (voiceR.getRight() - 136, voiceR.getY() + 5, 62, 20);
    voiceReset.setBounds (voiceR.getRight() - 70, voiceR.getY() + 5, 60, 20);
    cloud.setBounds (voiceR.getX() + 10, voiceR.getY() + 32, voiceR.getWidth() - 20, 112);
    // KÖRPER row: PITCH DECAY DENSITY GRAIN SPRAY SCAN; ANSCHLAG row: ATTACK BITE TONE WEAR LEVEL PAN
    const int rowA[] { pPitch, pDecay, pDensity, pGrain, pSpray, pScan };
    const int rowB[] { pAttack, pBite, pTone, pWear, pLevel, pPan };
    for (int i = 0; i < 6; ++i)
    {
        voiceKnobs[(size_t) rowA[i]]->setBounds (voiceR.getX() + 8 + i * 64, voiceR.getY() + 166, 64, 72);
        voiceKnobs[(size_t) rowB[i]]->setBounds (voiceR.getX() + 8 + i * 64, voiceR.getY() + 252, 64, 72);
    }

    // LAWINE
    Knob* lk[] { &feed, &thresh, &spur, &ghostK };
    for (int i = 0; i < 4; ++i) lk[i]->setBounds (lawineR.getX() + 6 + i * 60, lawineR.getY() + 30, 60, 74);
    lawineButton.setBounds (lawineR.getX() + 12, lawineR.getBottom() - 40, 120, 26);

    // MYZEL
    myzelView.setBounds (myzelR.getX() + 4, myzelR.getY() + 24, 160, myzelR.getHeight() - 28);
    myzelAmt.setBounds (myzelR.getX() + 168, myzelR.getY() + 30, 64, 74);
    myzelDiv.setBounds (myzelR.getX() + 170, myzelR.getY() + 118, 60, 22);

    // KOMPOST
    kompost.setBounds (kompostR.getX() + 108, kompostR.getY() + 26, 64, 74);
    freshButton.setBounds (kompostR.getX() + 110, kompostR.getY() + 106, 60, 22);
    recover.setBounds (kompostR.getX() + 108, kompostR.getY() + 160, 64, 74);

    // ABDRUCK
    abdruckButton.setBounds (abdruckR.getX() + 10, abdruckR.getY() + 30, abdruckR.getWidth() - 20, 24);
    groove.setBounds (abdruckR.getX() + 22, abdruckR.getY() + 136, 64, 74);
    swing.setBounds (abdruckR.getX() + 104, abdruckR.getY() + 136, 64, 74);

    // BROTKRUMEN
    rndButton.setBounds (trailR.getX() + 10, trailR.getY() + 30, trailR.getWidth() - 20, 28);
    rndAmt.setBounds (trailR.getX() + 8, trailR.getY() + 66, 64, 74);
    rndTarget.setBounds (trailR.getX() + 80, trailR.getY() + 92, 90, 22);
    trailView.setBounds (trailR.getX() + 6, trailR.getY() + 156, trailR.getWidth() - 12, 80);

    // TRAUNFALL
    tfButton.setBounds (tfR.getX() + 12, tfR.getY() + 14, 92, 30);
    Knob* tk[] { &tfWarp, &tfStutter, &tfShuffle, &tfFray };
    for (int i = 0; i < 4; ++i) tk[i]->setBounds (tfR.getX() + 116 + i * 70, tfR.getY() + 10, 68, 74);

    // MASTER
    drive.setBounds (masterR.getX() + 12, masterR.getY() + 30, 64, 74);
    master.setBounds (masterR.getX() + 12, masterR.getY() + 124, 64, 74);
}
