#pragma once

#include "Helpers.h"

//==============================================================================
/** Estado compartilhado do piano roll (zoom, rolagem, ferramenta, grade). */
struct RollState
{
    double pixelsPerBeat = 60.0;
    double viewStartBeat = 0.0;
    float rowHeight = 12.0f;
    int scrollY = 0;
    double gridBeats = 0.25;     // semicolcheia
    bool snap = true;

    enum class Tool { pointer, pencil, eraser };
    Tool tool = Tool::pointer;

    float beatToX (double b) const     { return (float) ((b - viewStartBeat) * pixelsPerBeat); }
    double xToBeat (float x) const     { return viewStartBeat + x / pixelsPerBeat; }
    float pitchToY (int p) const       { return (127 - p) * rowHeight - (float) scrollY; }
    int yToPitch (float y) const       { return juce::jlimit (0, 127, 127 - (int) std::floor ((y + (float) scrollY) / rowHeight)); }
    double snapBeat (double b) const   { return snap ? std::round (b / gridBeats) * gridBeats : b; }
    double snapDown (double b) const   { return snap ? std::floor (b / gridBeats) * gridBeats : b; }
};

/** Uma nota visível no piano roll, com sua posição em batidas do projeto. */
struct RollNote
{
    te::MidiClip* clip = nullptr;
    te::MidiNote* note = nullptr;
    double start = 0.0, end = 0.0;   // batidas do projeto
    int pitch = 60, velocity = 100;
    juce::Colour colour;
};

class PianoRollPanel;

//==============================================================================
class NoteGrid : public juce::Component
{
public:
    explicit NoteGrid (PianoRollPanel& p) : panel (p)   { setWantsKeyboardFocus (true); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, move, resizeEnd, resizeStart, marquee, create, erase };

    int noteAt (juce::Point<float>) const;
    juce::Rectangle<float> noteRect (const RollNote&) const;

    PianoRollPanel& panel;
    Drag drag = Drag::none;
    juce::Point<float> downPos;
    juce::Rectangle<float> marquee;
    int createdIndex = -1;

    struct Orig { juce::ValueTree state; double start, end; int pitch; };
    std::vector<Orig> originals;
};

//==============================================================================
class PianoKeyboard : public juce::Component
{
public:
    explicit PianoKeyboard (PianoRollPanel& p) : panel (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PianoRollPanel& panel;
    int playing = -1;
};

//==============================================================================
class RollRuler : public juce::Component
{
public:
    explicit RollRuler (PianoRollPanel& p) : panel (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override    { mouseDown (e); }

private:
    PianoRollPanel& panel;
};

//==============================================================================
class VelocityLane : public juce::Component
{
public:
    explicit VelocityLane (PianoRollPanel& p) : panel (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void setFromY (const juce::MouseEvent&);
    PianoRollPanel& panel;
    std::vector<juce::ValueTree> targets;
};

//==============================================================================
/** Editor de notas (piano roll) no estilo do Logic: faixas selecionadas ou global. */
class PianoRollPanel : public juce::Component,
                       private te::ValueTreeAllEventListener,
                       private juce::AsyncUpdater,
                       private juce::ChangeListener,
                       private juce::ScrollBar::Listener,
                       private juce::Timer
{
public:
    PianoRollPanel();
    ~PianoRollPanel() override;

    void setEdit (te::Edit*, te::SelectionManager*);
    void showClip (te::MidiClip*);           // abre focado num clipe
    void focusOnNotes();

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

    //==============================================================================
    // usados pelos subcomponentes
    RollState state;
    std::vector<RollNote> notes;
    std::vector<te::MidiClip*> clips;
    std::vector<juce::ValueTree> selection;

    te::Edit* getEdit() const                 { return edit; }
    te::AudioTrack* getTargetTrack() const;
    bool isSelected (const RollNote& n) const;
    void select (const RollNote&, bool add);
    void clearSelection();
    std::vector<RollNote*> getSelectedNotes();

    te::MidiNote* findNote (const juce::ValueTree&, te::MidiClip** clipOut = nullptr) const;
    void setNoteBeats (te::MidiClip&, te::MidiNote&, double start, double end, juce::UndoManager*);
    RollNote* createNoteAt (double beat, int pitch, double length);
    void deleteSelected();
    void quantizeSelected();
    void transposeSelected (int semitones);
    void nudgeSelected (double beats);
    void selectAll();

    void previewNote (int pitch, bool on);
    void rebuildNotes();
    void repaintAll();
    juce::UndoManager* getUndo() const        { return edit != nullptr ? &edit->getUndoManager() : nullptr; }

private:
    void valueTreeChanged() override          { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void rebuildClips();
    void updateTargetLabel();

    te::Edit* edit = nullptr;
    te::SelectionManager* selectionManager = nullptr;
    te::MidiClip::Ptr focusClip;
    bool clipsDirty = true;
    float lastPlayheadX = -1.0f;
    te::MPESourceID previewSource = te::createUniqueMPESourceID();
    te::AudioTrack::Ptr previewTrack;
    int previewPitch = -1;

    juce::ComboBox mode, grid;
    juce::TextButton pointerTool { "Ponteiro" }, pencilTool { "Lapis" }, eraserTool { "Borracha" };
    juce::TextButton snapButton { "Grade" }, quantizeButton { "Quantizar" }, closeButton { "X" };
    juce::TextButton zoomIn { "+" }, zoomOut { "-" };
    juce::Label targetLabel;

    RollRuler ruler { *this };
    PianoKeyboard keyboard { *this };
    NoteGrid noteGrid { *this };
    VelocityLane velocity { *this };
    juce::ScrollBar hScroll { false }, vScroll { true };

    friend class NoteGrid;
};
