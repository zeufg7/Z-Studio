#pragma once

#include "Helpers.h"

//==============================================================================
/** Estado de visualização da janela de arranjo (zoom, rolagem, grade). */
class ViewState : public juce::ChangeBroadcaster
{
public:
    ViewState (te::Edit& e, te::SelectionManager& s) : edit (e), selection (s) {}

    te::Edit& edit;
    te::SelectionManager& selection;

    double viewStart = 0.0;         // segundos no canto esquerdo
    double pixelsPerSecond = 40.0;
    int trackHeight = 76;
    int scrollY = 0;
    bool snapToGrid = true;

    float timeToX (te::TimePosition t) const     { return (float) ((t.inSeconds() - viewStart) * pixelsPerSecond); }
    te::TimePosition xToTime (float x) const     { return te::TimePosition::fromSeconds (std::max (0.0, viewStart + x / pixelsPerSecond)); }
    double secondsToPixels (double s) const      { return s * pixelsPerSecond; }

    /** Encaixa um tempo na grade (semínima, ou compasso quando o zoom está distante). */
    te::TimePosition snap (te::TimePosition t) const;

    /** Tamanho da grade em batidas, conforme o zoom. */
    double getGridBeats() const;

    void setViewStart (double s)                 { viewStart = std::max (0.0, s); sendChangeMessage(); }
    void setZoom (double pps, float anchorX);
    void setScrollY (int y)                      { scrollY = std::max (0, y); sendChangeMessage(); }
};

class ArrangeView;

//==============================================================================
class ClipComponent : public juce::Component
{
public:
    ClipComponent (ViewState&, te::Clip::Ptr, ArrangeView&);
    ~ClipComponent() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    te::Clip& getClip()     { return *clip; }

private:
    enum class DragMode { none, move, trimStart, trimEnd };

    DragMode getModeFor (int x) const;
    void paintMidi (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>);
    void paintAudio (juce::Graphics&, te::WaveAudioClip&, juce::Rectangle<int>);
    void updateThumbnail();

    ViewState& viewState;
    te::Clip::Ptr clip;
    ArrangeView& arrange;
    std::unique_ptr<te::SmartThumbnail> thumbnail;

    DragMode dragMode = DragMode::none;
    te::TimePosition dragStartClipStart, dragStartClipEnd, dragStartMouseTime;
};

//==============================================================================
class TrackHeaderComponent : public juce::Component,
                             private te::ValueTreeAllEventListener,
                             private juce::Timer
{
public:
    TrackHeaderComponent (ViewState&, te::AudioTrack::Ptr);
    ~TrackHeaderComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    te::AudioTrack& getTrack()      { return *track; }

private:
    void valueTreeChanged() override;
    void timerCallback() override;
    void showInputMenu();
    void updateButtons();

    ViewState& viewState;
    te::AudioTrack::Ptr track;

    juce::Label name;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, inputButton { "In" };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    float meterLevel = 0.0f;
    te::LevelMeasurer::Client meterClient;
    te::LevelMeterPlugin* meterPlugin = nullptr;
};

//==============================================================================
class TrackLaneComponent : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private te::ValueTreeAllEventListener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    TrackLaneComponent (ViewState&, te::AudioTrack::Ptr, ArrangeView&);
    ~TrackLaneComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    te::AudioTrack& getTrack()      { return *track; }
    void layoutClips();

private:
    void valueTreeChanged() override {}
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void buildClips();

    ViewState& viewState;
    te::AudioTrack::Ptr track;
    ArrangeView& arrange;
    juce::OwnedArray<ClipComponent> clips;
    bool needsRebuild = true;
    juce::Rectangle<int> recordingArea;
};

//==============================================================================
/** Régua de compassos: arraste em cima = região de loop (cycle), embaixo = posição. */
class RulerComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit RulerComponent (ViewState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override   { repaint(); }

    ViewState& viewState;
    bool draggingLoop = false;
    te::TimePosition loopAnchor;
};

//==============================================================================
class PlayheadOverlay : public juce::Component,
                        private juce::Timer
{
public:
    explicit PlayheadOverlay (ViewState&);
    void paint (juce::Graphics&) override;
    bool hitTest (int, int) override    { return false; }

private:
    void timerCallback() override;
    ViewState& viewState;
    float lastX = -1.0f;
};

//==============================================================================
/** Janela principal de arranjo (faixas + clipes + régua). */
class ArrangeView : public juce::Component,
                    private te::ValueTreeAllEventListener,
                    private juce::AsyncUpdater,
                    private juce::ChangeListener,
                    private juce::ScrollBar::Listener
{
public:
    ArrangeView (te::Edit&, te::SelectionManager&);
    ~ArrangeView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    ViewState& getViewState()           { return viewState; }

    /** Faixa sob uma posição de tela (usado para arrastar clipes entre faixas). */
    te::AudioTrack* getTrackAtScreenPosition (juce::Point<int> screenPos);

    void zoomIn()                       { viewState.setZoom (viewState.pixelsPerSecond * 1.4, (float) getLanesWidth() / 2.0f); }
    void zoomOut()                      { viewState.setZoom (viewState.pixelsPerSecond / 1.4, (float) getLanesWidth() / 2.0f); }
    void followPlayhead();

    /** Importa arquivos de áudio/MIDI numa faixa, a partir de um tempo. */
    void importFiles (te::AudioTrack&, const juce::StringArray& files, te::TimePosition at);

    static constexpr int headerWidth = 210;
    static constexpr int rulerHeight = 34;

private:
    void valueTreeChanged() override {}
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void buildTracks();
    void layoutTracks();
    void updateScrollBars();
    int getLanesWidth() const           { return juce::jmax (1, getWidth() - headerWidth - scrollBarSize); }
    double getTotalSeconds() const;

    static constexpr int scrollBarSize = 12;

    te::Edit& edit;
    ViewState viewState;

    RulerComponent ruler { viewState };
    juce::Component headerArea, laneArea;
    PlayheadOverlay playhead { viewState };
    juce::ScrollBar hScroll { false }, vScroll { true };

    juce::OwnedArray<TrackHeaderComponent> headers;
    juce::OwnedArray<TrackLaneComponent> lanes;
};
