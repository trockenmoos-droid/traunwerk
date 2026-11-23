#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

/** Rotary knob with its caption underneath; shows the value while touched. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& label);
    void attach (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
/** The pattern: eight rows of sixteen steps, with LAWINE sand and ghost
    notes drawn into the cells, per-voice length and SCHUTZ locks. */
class StepGrid : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit StepGrid (TraunwerkProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int y) override { dropRow = rowAt (y); repaint(); }
    void fileDragMove (const juce::StringArray&, int, int y) override  { dropRow = rowAt (y); repaint(); }
    void fileDragExit (const juce::StringArray&) override               { dropRow = -1; repaint(); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    std::function<void (int)> onSelect;
    std::function<void (int)> onLoadSample;
    std::array<float, tw::numVoices> flash {};

    static constexpr int headerH = 22, nameW = 118, cellW = 36, rowH = 42;

private:
    int rowAt (int y) const;
    int colAt (int x) const;
    juce::Rectangle<float> cellRect (int v, int c) const;
    juce::Rectangle<float> lockRect (int v) const;

    TraunwerkProcessor& proc;
    int dragValue = -1, dragRow = -1;
    int dropRow = -1;
};

//==============================================================================
/** The grains of the selected voice's last hit: time across, read position
    up, size by amplitude. Compost grains are drawn in humus brown. */
class GrainCloud : public juce::Component
{
public:
    explicit GrainCloud (TraunwerkProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void newHit()  { dots.clear(); hitTime = juce::Time::getMillisecondCounterHiRes(); }
    void add (const GrainDot& d) { if (dots.size() < 3000) dots.push_back (d); }
    void setVoice (int v) { voice = v; dots.clear(); cacheVoice = -1; }

private:
    TraunwerkProcessor& proc;
    std::vector<GrainDot> dots;
    double hitTime = 0.0;
    int voice = 0;
    int cacheVoice = -1;
    const DrumSource* cacheSrc = nullptr;
    std::vector<float> profile;
};

//==============================================================================
/** MYZEL: eight nodes on a circle, threads between them. */
class MyzelView : public juce::Component
{
public:
    explicit MyzelView (TraunwerkProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void addSpore (SporeVis s) { if (live.size() < 64) live.push_back ({ s.from, s.to, juce::Time::getMillisecondCounterHiRes() }); }
    std::array<float, tw::numVoices> flash {};

private:
    juce::Point<float> node (int v) const;
    juce::Path thread (int from, int to) const;
    TraunwerkProcessor& proc;
    int selected = -1;
    struct Live { int from, to; double t0; };
    std::vector<Live> live;
};

//==============================================================================
/** BROTKRUMEN: every randomisation leaves a crumb; drag along the trail to
    morph between them. */
class TrailView : public juce::Component
{
public:
    explicit TrailView (TraunwerkProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { scrub (e); }
    void mouseDrag (const juce::MouseEvent& e) override { scrub (e); }

private:
    void scrub (const juce::MouseEvent&);
    TraunwerkProcessor& proc;
};

//==============================================================================
class TraunwerkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TraunwerkEditor (TraunwerkProcessor&);
    ~TraunwerkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectVoice (int v);
    void chooseVoiceSample (int v);
    void chooseAbdruck();
    void showPresetMenu();
    void stepPreset (int delta);
    void refreshLabels();
    void panel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const juce::String& sub = {});

    TraunwerkProcessor& proc;
    TwLookAndFeel lnf;

    StepGrid grid;
    GrainCloud cloud;
    MyzelView myzelView;
    TrailView trailView;

    std::vector<std::unique_ptr<Knob>> voiceKnobs;
    Knob feed { "FEED" }, thresh { "SCHWELLE" }, spur { "EROSION" }, ghostK { "GHOSTS" };
    Knob myzelAmt { "MENGE" };
    Knob kompost { "KOMPOST" }, recover { "ERHOLUNG" };
    Knob groove { "GROOVE" }, swing { "SWING" };
    Knob rndAmt { "MENGE" };
    Knob drive { "DRIVE" }, master { "MASTER" };
    Knob tfWarp { "WARP" }, tfStutter { "STOTTERN" }, tfShuffle { "UMSCHICHTEN" }, tfFray { "ZERFASERN" };
    juce::TextButton tfButton { "TRAUNFALL" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tfAtt;

    juce::ComboBox myzelDiv, rndTarget;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> myzelDivAtt, rndTargetAtt;

    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetName;
    juce::TextButton playButton { "PLAY" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAtt;
    juce::Slider bpmSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;

    juce::TextButton lawineButton { "LAWINE!" }, freshButton { "FRISCH" }, abdruckButton { "ABDRUCK LADEN" },
                     rndButton { juce::String::fromUTF8 ("W\xc3\x9cRFELN") }, voiceLoad { "SAMPLE" }, voiceReset { "INTERN" };

    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> tfR, lawineR, myzelR, kompostR, abdruckR, trailR, masterR, voiceR, gridR;
    int lastPresetCount = -1;
    int lastAvalancheCount = 0;
    float avalancheFlash = 0.0f;
    int lastRatchetCount = 0;
    float ratchetFlash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TraunwerkEditor)
};
