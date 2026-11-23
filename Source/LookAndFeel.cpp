#include "LookAndFeel.h"

TwLookAndFeel::TwLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, twc::bg);
    setColour (juce::Label::textColourId,                 twc::text);
    setColour (juce::Slider::textBoxTextColourId,         twc::text);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId,         twc::dim);
    setColour (juce::TextButton::textColourOnId,          twc::accent);
    setColour (juce::PopupMenu::backgroundColourId,       twc::panel);
    setColour (juce::PopupMenu::textColourId,             twc::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, twc::accentSft);
    setColour (juce::ComboBox::textColourId,              twc::text);
}

void TwLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);

    // faint outer ring
    g.setColour (twc::line);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);

    // value arc
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (twc::accent.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // spore scatter: density of dots follows the value
    juce::Random rng ((int) (s.getName().hashCode() ^ 0x5eed));
    const int dots = 26;
    for (int i = 0; i < dots; ++i)
    {
        const float t = rng.nextFloat();
        const float a = startAngle + t * (endAngle - startAngle);
        const float rr = radius * (0.35f + rng.nextFloat() * 0.55f);
        const float alpha = (t <= sliderPos ? 0.55f : 0.10f) * (0.4f + rng.nextFloat() * 0.6f);

        g.setColour (twc::accent.withAlpha (alpha));
        g.fillEllipse (centre.x + std::sin (a) * rr - 1.0f,
                       centre.y - std::cos (a) * rr - 1.0f,
                       2.0f, 2.0f);
    }

    // indicator
    g.setColour (twc::accent);
    g.fillEllipse (centre.x + std::sin (angle) * radius - 2.5f,
                   centre.y - std::cos (angle) * radius - 2.5f,
                   5.0f, 5.0f);
}

void TwLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = (float) y + (float) height * 0.5f;

    g.setColour (twc::line);
    g.drawLine ((float) x, cy, (float) (x + width), cy, 1.0f);

    g.setColour (twc::accent.withAlpha (0.7f));
    g.drawLine ((float) x, cy, sliderPos, cy, 1.4f);

    g.setColour (twc::accent);
    g.fillEllipse (sliderPos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
}

void TwLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                       bool highlighted, bool)
{
    const bool flat = b.getComponentID() == flatId;

    g.setColour (b.getToggleState() ? twc::accent
               : highlighted        ? twc::text
                                    : twc::text.withAlpha (flat ? 0.85f : 0.7f));

    g.setFont (juce::Font (juce::FontOptions { flat ? 14.0f : 12.0f })
                   .withExtraKerningFactor (flat ? 0.22f : 0.08f));

    g.drawText (b.getButtonText(), b.getLocalBounds(),
                juce::Justification::centred, false);
}

void TwLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&,
                                             bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);

    if (b.getComponentID() == flatId)
    {
        // Text on the background: only a faint underline on hover, so the
        // header stays as quiet as it is in the design.
        if (highlighted || down)
        {
            g.setColour (twc::accent.withAlpha (0.45f));
            g.drawLine (r.getX() + 6.0f, r.getBottom() - 3.0f,
                        r.getRight() - 6.0f, r.getBottom() - 3.0f, 1.0f);
        }
        return;
    }

    g.setColour (b.getToggleState() ? twc::accentSft : twc::panel);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (highlighted || down ? twc::accent : twc::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
}

void TwLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (twc::panel);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (box.isMouseOver() ? twc::accent : twc::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 9.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 3.0f, cy - 1.5f, cx + 3.0f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (twc::dim);
    g.fillPath (arrow);
}

juce::Font TwLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions { 11.0f }).withExtraKerningFactor (0.12f);
}

void TwLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}
