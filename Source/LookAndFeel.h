#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace twc
{
    const juce::Colour bg        { 0xff0a0d08 };
    const juce::Colour panel     { 0xff11150e };
    const juce::Colour line      { 0xff2a3323 };
    const juce::Colour text      { 0xffd9e3c8 };
    const juce::Colour dim       { 0xff7d8a6b };
    const juce::Colour accent    { 0xffb9d94a };
    const juce::Colour accentSft { 0x66b9d94a };
    const juce::Colour sand      { 0xffc9b47c };
    const juce::Colour humus     { 0xffa0703e };
    const juce::Colour lichen2   { 0xff6f8f3a };
}

/** Thin rotary + slider styling: no chunky fills, just rings,
    a bright indicator dot and a scatter of particles around the arc. */
class TwLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TwLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    /** Buttons with this component ID draw without a box: used for the
        preset browser in the header, which is text on the background. */
    static constexpr const char* flatId = "flat";

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

private:
    juce::Random particleRng;
};
