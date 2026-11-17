#include "PianoRoll.h"

using namespace juce;

namespace
{
    double clipStartBeat (const te::MidiClip& c)   { return c.getStartBeat().inBeats(); }
    double clipEndBeat (const te::MidiClip& c)     { return c.getEndBeat().inBeats(); }
    double clipOffset (const te::MidiClip& c)      { return c.getOffsetInBeats().inBeats(); }

    double toEditBeat (const te::MidiClip& c, double noteBeat)   { return clipStartBeat (c) + noteBeat - clipOffset (c); }
    double toNoteBeat (const te::MidiClip& c, double editBeat)   { return editBeat - clipStartBeat (c) + clipOffset (c); }

    bool isBlackKey (int p)
    {
        const int pc = p % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    // Convenção do Logic/Cubase: Dó central (60) = C3
    String pitchName (int p)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return String (names[p % 12]) + String (p / 12 - 2);
    }

    int beatsPerBar (te::Edit& e)
    {
        if (auto ts = e.tempoSequence.getTimeSig (0))
            return jmax (1, ts->numerator.get() * 4 / jmax (1, ts->denominator.get()));
        return 4;
    }

    void drawBeatLines (Graphics& g, const RollState& st, te::Edit& edit, int width, float top, float bottom)
    {
        const int bpb = beatsPerBar (edit);
        const double first = std::floor (st.xToBeat (0.0f) / st.gridBeats) * st.gridBeats;
        const double last = st.xToBeat ((float) width);
        const bool showGrid = st.gridBeats * st.pixelsPerBeat >= 6.0;

        for (double b = std::max (0.0, first); b <= last; b += (showGrid ? st.gridBeats : 1.0))
        {
            const float x = st.beatToX (b);
            const bool isBar = std::abs (std::fmod (b, (double) bpb)) < 1.0e-6;
            const bool isBeat = std::abs (b - std::round (b)) < 1.0e-6;

            if (! isBeat && ! showGrid)
                continue;

            g.setColour (isBar ? Theme::gridBar.brighter (0.2f) : isBeat ? Theme::gridBar : Theme::gridBeat);
            g.drawVerticalLine (roundToInt (x), top, bottom);
        }
    }
}

//==============================================================================
// NoteGrid
//==============================================================================
juce::Rectangle<float> NoteGrid::noteRect (const RollNote& n) const
{
    auto& st = panel.state;
    const float x1 = st.beatToX (n.start);
    const float x2 = st.beatToX (n.end);
    return { x1, st.pitchToY (n.pitch) + 1.0f, jmax (3.0f, x2 - x1), st.rowHeight - 2.0f };
}

int NoteGrid::noteAt (Point<float> p) const
{
    // de trás para frente: as notas desenhadas por cima têm prioridade
    for (int i = (int) panel.notes.size(); --i >= 0;)
        if (noteRect (panel.notes[(size_t) i]).expanded (0.0f, 1.0f).contains (p))
            return i;

    return -1;
}

void NoteGrid::paint (Graphics& g)
{
    auto& st = panel.state;
    auto* edit = panel.getEdit();
    g.fillAll (Theme::lane);

    // linhas das notas (teclas pretas mais escuras)
    const int topPitch = st.yToPitch (0.0f);
    const int bottomPitch = st.yToPitch ((float) getHeight());

    for (int p = bottomPitch; p <= topPitch; ++p)
    {
        const float y = st.pitchToY (p);
        if (isBlackKey (p))
        {
            g.setColour (Theme::laneAlt.darker (0.25f));
            g.fillRect (0.0f, y, (float) getWidth(), st.rowHeight);
        }

        if (p % 12 == 0)
        {
            g.setColour (Theme::gridBar);
            g.drawHorizontalLine (roundToInt (y + st.rowHeight), 0.0f, (float) getWidth());
        }
    }

    if (edit == nullptr)
        return;

    drawBeatLines (g, st, *edit, getWidth(), 0.0f, (float) getHeight());

    // regiões (clipes) visíveis
    for (auto* c : panel.clips)
    {
        auto col = c->getTrack() != nullptr ? c->getTrack()->getColour() : Theme::accent;
        const float x1 = st.beatToX (clipStartBeat (*c));
        const float x2 = st.beatToX (clipEndBeat (*c));
        g.setColour (col.withAlpha (0.07f));
        g.fillRect (x1, 0.0f, x2 - x1, (float) getHeight());
        g.setColour (col.withAlpha (0.45f));
        g.drawVerticalLine (roundToInt (x1), 0.0f, (float) getHeight());
        g.drawVerticalLine (roundToInt (x2), 0.0f, (float) getHeight());
    }

    // notas
    auto* target = panel.getTargetTrack();

    for (auto& n : panel.notes)
    {
        auto r = noteRect (n);
        if (r.getRight() < 0 || r.getX() > (float) getWidth())
            continue;

        const bool sel = panel.isSelected (n);
        const bool otherTrack = target != nullptr && n.clip->getTrack() != target;
        auto base = n.colour.withMultipliedSaturation (otherTrack ? 0.5f : 1.0f);
        auto fill = base.withAlpha ((otherTrack ? 0.45f : 0.65f) + 0.35f * (float) n.velocity / 127.0f);

        g.setColour (sel ? fill.brighter (0.5f) : fill);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (sel ? Colours::white : base.darker (0.6f));
        g.drawRoundedRectangle (r, 2.5f, sel ? 1.6f : 1.0f);

        if (r.getWidth() > 34 && st.rowHeight >= 11)
        {
            g.setColour (Colour (0xff111111).withAlpha (0.8f));
            g.setFont (FontOptions (jmin (11.0f, st.rowHeight - 2.0f)));
            g.drawText (pitchName (n.pitch), r.reduced (3, 0), Justification::centredLeft, false);
        }
    }

    // seleção por retângulo
    if (drag == Drag::marquee)
    {
        g.setColour (Theme::accent.withAlpha (0.15f));
        g.fillRect (marquee);
        g.setColour (Theme::accent);
        g.drawRect (marquee, 1.0f);
    }

    // cursor de reprodução
    auto beat = edit->tempoSequence.toBeats (edit->getTransport().getPosition()).inBeats();
    g.setColour (edit->getTransport().isRecording() ? Theme::record : Theme::playhead);
    g.fillRect (st.beatToX (beat) - 0.5f, 0.0f, 1.5f, (float) getHeight());
}

void NoteGrid::mouseMove (const MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i >= 0 && panel.state.tool != RollState::Tool::eraser)
    {
        auto r = noteRect (panel.notes[(size_t) i]);
        const bool edge = e.position.x > r.getRight() - 6.0f || e.position.x < r.getX() + 4.0f;
        setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor (panel.state.tool == RollState::Tool::pencil ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor);
    }
}

void NoteGrid::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    auto& st = panel.state;
    downPos = e.position;
    originals.clear();
    drag = Drag::none;

    if (panel.getEdit() == nullptr)
        return;

    panel.getEdit()->getUndoManager().beginNewTransaction ("Editar notas");
    const int i = noteAt (e.position);

    if (st.tool == RollState::Tool::eraser || (e.mods.isPopupMenu() && i >= 0))
    {
        drag = Drag::erase;
        if (i >= 0)
        {
            auto& n = panel.notes[(size_t) i];
            n.clip->getSequence().removeNote (*n.note, panel.getUndo());
            panel.rebuildNotes();
            repaint();
        }
        return;
    }

    if (i >= 0)
    {
        auto& n = panel.notes[(size_t) i];

        if (! panel.isSelected (n))
            panel.select (n, e.mods.isShiftDown());
        else if (e.mods.isShiftDown())
        {
            // shift em nota selecionada: tira da seleção
            auto it = std::find (panel.selection.begin(), panel.selection.end(), n.note->state);
            if (it != panel.selection.end()) panel.selection.erase (it);
            repaint();
            return;
        }

        auto r = noteRect (n);
        drag = e.position.x > r.getRight() - 6.0f ? Drag::resizeEnd
             : (e.position.x < r.getX() + 4.0f && r.getWidth() > 12.0f) ? Drag::resizeStart
             : Drag::move;

        for (auto* s : panel.getSelectedNotes())
            originals.push_back ({ s->note->state, s->start, s->end, s->pitch });

        panel.previewNote (n.pitch, true);
        panel.repaintAll();
        return;
    }

    if (st.tool == RollState::Tool::pencil)
    {
        const double beat = st.snapDown (st.xToBeat (e.position.x));
        if (auto* created = panel.createNoteAt (beat, st.yToPitch (e.position.y), st.gridBeats))
        {
            drag = Drag::create;
            originals.push_back ({ created->note->state, created->start, created->end, created->pitch });
            panel.previewNote (created->pitch, true);
        }
        return;
    }

    // ponteiro no vazio: seleção por retângulo
    if (! e.mods.isShiftDown())
        panel.clearSelection();

    drag = Drag::marquee;
    marquee = {};
    panel.repaintAll();
}

void NoteGrid::mouseDrag (const MouseEvent& e)
{
    auto& st = panel.state;
    auto* um = panel.getUndo();

    if (drag == Drag::erase)
    {
        const int i = noteAt (e.position);
        if (i >= 0)
        {
            auto& n = panel.notes[(size_t) i];
            n.clip->getSequence().removeNote (*n.note, um);
            panel.rebuildNotes();
            repaint();
        }
        return;
    }

    if (drag == Drag::marquee)
    {
        marquee = Rectangle<float> (downPos, e.position);
        repaint();
        return;
    }

    if (originals.empty())
        return;

    double deltaBeats = (e.position.x - downPos.x) / st.pixelsPerBeat;
    if (st.snap) deltaBeats = std::round (deltaBeats / st.gridBeats) * st.gridBeats;
    const int deltaPitch = (int) std::round ((downPos.y - e.position.y) / st.rowHeight);

    for (auto& o : originals)
    {
        te::MidiClip* clip = nullptr;
        auto* note = panel.findNote (o.state, &clip);
        if (note == nullptr || clip == nullptr)
            continue;

        const double minLen = st.snap ? st.gridBeats : 1.0 / 32.0;

        if (drag == Drag::move)
        {
            const double len = o.end - o.start;
            const double s = jmax (clipStartBeat (*clip), o.start + deltaBeats);
            panel.setNoteBeats (*clip, *note, s, s + len, um);
            const int newPitch = jlimit (0, 127, o.pitch + deltaPitch);
            if (note->getNoteNumber() != newPitch)
            {
                note->setNoteNumber (newPitch, um);
                if (&o == &originals.front())
                {
                    panel.previewNote (-1, false);
                    panel.previewNote (newPitch, true);
                }
            }
        }
        else if (drag == Drag::resizeEnd || drag == Drag::create)
        {
            const double end = jmax (o.start + minLen, o.end + deltaBeats);
            panel.setNoteBeats (*clip, *note, o.start, end, um);
        }
        else if (drag == Drag::resizeStart)
        {
            const double s = jlimit (clipStartBeat (*clip), o.end - minLen, o.start + deltaBeats);
            panel.setNoteBeats (*clip, *note, s, o.end, um);
        }
    }

    panel.rebuildNotes();
    panel.repaintAll();
}

void NoteGrid::mouseUp (const MouseEvent& e)
{
    panel.previewNote (-1, false);

    if (drag == Drag::marquee)
    {
        auto r = Rectangle<float> (downPos, e.position);
        for (auto& n : panel.notes)
            if (noteRect (n).intersects (r))
                panel.select (n, true);
    }

    drag = Drag::none;
    originals.clear();
    panel.repaintAll();
}

void NoteGrid::mouseDoubleClick (const MouseEvent& e)
{
    // duplo clique no vazio cria nota (com qualquer ferramenta); numa nota, apaga
    auto& st = panel.state;
    const int i = noteAt (e.position);

    if (i >= 0)
    {
        auto& n = panel.notes[(size_t) i];
        n.clip->getSequence().removeNote (*n.note, panel.getUndo());
        panel.clearSelection();
        panel.rebuildNotes();
    }
    else if (st.tool == RollState::Tool::pointer)
    {
        panel.createNoteAt (st.snapDown (st.xToBeat (e.position.x)), st.yToPitch (e.position.y), st.gridBeats);
    }

    panel.repaintAll();
}

void NoteGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    auto& st = panel.state;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = st.xToBeat (e.position.x);
        st.pixelsPerBeat = jlimit (8.0, 600.0, st.pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
        st.viewStartBeat = jmax (0.0, anchor - e.position.x / st.pixelsPerBeat);
    }
    else if (e.mods.isAltDown())
    {
        st.rowHeight = jlimit (6.0f, 28.0f, st.rowHeight + (w.deltaY > 0 ? 1.0f : -1.0f));
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        st.viewStartBeat = jmax (0.0, st.viewStartBeat - d * 300.0 / st.pixelsPerBeat);
    }
    else
    {
        st.scrollY = jlimit (0, jmax (0, (int) (128 * st.rowHeight) - getHeight()), st.scrollY - roundToInt (w.deltaY * 250.0f));
    }

    panel.resized();
    panel.repaintAll();
}

bool NoteGrid::keyPressed (const KeyPress& k)
{
    const auto code = k.getKeyCode();
    const auto mods = k.getModifiers();

    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey)   { panel.deleteSelected(); return true; }
    if (code == 'A' && mods.isCommandDown())                              { panel.selectAll(); return true; }
    if (code == 'Q' && ! mods.isCommandDown())                           { panel.quantizeSelected(); return true; }
    if (code == KeyPress::upKey)    { panel.transposeSelected (mods.isShiftDown() ? 12 : 1); return true; }
    if (code == KeyPress::downKey)  { panel.transposeSelected (mods.isShiftDown() ? -12 : -1); return true; }
    if (code == KeyPress::leftKey)  { panel.nudgeSelected (-panel.state.gridBeats); return true; }
    if (code == KeyPress::rightKey) { panel.nudgeSelected (panel.state.gridBeats); return true; }
    if (code == KeyPress::escapeKey){ panel.clearSelection(); panel.repaintAll(); return true; }

    return false;   // o resto (Espaço, R, Ctrl+Z...) vai para os atalhos globais
}

//==============================================================================
// PianoKeyboard
//==============================================================================
void PianoKeyboard::paint (Graphics& g)
{
    auto& st = panel.state;
    g.fillAll (Theme::panel);

    const int topPitch = st.yToPitch (0.0f);
    const int bottomPitch = st.yToPitch ((float) getHeight());
    const float w = (float) getWidth();

    for (int p = bottomPitch; p <= topPitch; ++p)
    {
        const float y = st.pitchToY (p);
        const bool black = isBlackKey (p);

        g.setColour (black ? Colour (0xff1a1a1a) : Colour (0xffeeeeee));
        g.fillRect (black ? 0.0f : 0.0f, y, black ? w * 0.62f : w, st.rowHeight);

        if (! black)
        {
            g.setColour (Colour (0xffeeeeee));
            g.fillRect (w * 0.62f, y, w * 0.38f, st.rowHeight);
        }

        g.setColour (Colour (0xffb0b0b0));
        g.drawHorizontalLine (roundToInt (y + st.rowHeight), black ? w * 0.62f : 0.0f, w);

        if (p == playing)
        {
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.fillRect (0.0f, y, w, st.rowHeight);
        }

        if (p % 12 == 0)
        {
            g.setColour (Colour (0xff333333));
            g.setFont (FontOptions (jmin (11.0f, st.rowHeight)));
            g.drawText (pitchName (p), Rectangle<float> (w * 0.45f, y, w * 0.52f, st.rowHeight), Justification::centredRight);
        }
    }

    g.setColour (Theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PianoKeyboard::mouseDown (const MouseEvent& e)
{
    playing = panel.state.yToPitch (e.position.y);
    panel.previewNote (playing, true);
    repaint();
}

void PianoKeyboard::mouseUp (const MouseEvent&)
{
    panel.previewNote (-1, false);
    playing = -1;
    repaint();
}

void PianoKeyboard::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    auto& st = panel.state;
    st.scrollY = jlimit (0, jmax (0, (int) (128 * st.rowHeight) - getHeight()), st.scrollY - roundToInt (w.deltaY * 250.0f));
    ignoreUnused (e);
    panel.resized();
    panel.repaintAll();
}

//==============================================================================
// RollRuler
//==============================================================================
void RollRuler::paint (Graphics& g)
{
    auto& st = panel.state;
    g.fillAll (Theme::panel);

    auto* edit = panel.getEdit();
    if (edit == nullptr)
        return;

    const int bpb = beatsPerBar (*edit);
    const double first = std::floor (st.xToBeat (0.0f));
    const double last = st.xToBeat ((float) getWidth());
    int barStep = 1;
    while (st.pixelsPerBeat * bpb * barStep < 40.0) barStep *= 2;

    g.setFont (FontOptions (11.0f));

    for (double b = std::max (0.0, first); b <= last; b += 1.0)
    {
        const float x = st.beatToX (b);
        const int bi = (int) std::lround (b);

        if (bi % bpb == 0)
        {
            const int bar = bi / bpb;
            g.setColour (Theme::textDim);
            g.drawVerticalLine (roundToInt (x), 0.0f, (float) getHeight());

            if (bar % barStep == 0)
            {
                g.setColour (Theme::text);
                g.drawText (String (bar + 1), Rectangle<float> (x + 3.0f, 0.0f, 40.0f, (float) getHeight()), Justification::centredLeft);
            }
        }
        else if (st.pixelsPerBeat >= 14.0)
        {
            g.setColour (Theme::outline);
            g.drawVerticalLine (roundToInt (x), (float) getHeight() - 6.0f, (float) getHeight());
        }
    }

    auto beat = edit->tempoSequence.toBeats (edit->getTransport().getPosition()).inBeats();
    const float px = st.beatToX (beat);
    Path tri;
    tri.addTriangle (px - 5.0f, (float) getHeight() - 9.0f, px + 5.0f, (float) getHeight() - 9.0f, px, (float) getHeight());
    g.setColour (Theme::playhead);
    g.fillPath (tri);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void RollRuler::mouseDown (const MouseEvent& e)
{
    if (auto* edit = panel.getEdit())
    {
        auto beat = panel.state.snapBeat (jmax (0.0, panel.state.xToBeat (e.position.x)));
        edit->getTransport().setPosition (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));
        panel.repaintAll();
    }
}

//==============================================================================
// VelocityLane
//==============================================================================
void VelocityLane::paint (Graphics& g)
{
    auto& st = panel.state;
    g.fillAll (Theme::panel.darker (0.15f));

    g.setColour (Theme::textDim);
    g.setFont (FontOptions (10.0f));
    g.drawText ("VELOCITY", 4, 2, 80, 12, Justification::left);

    const float h = (float) getHeight() - 6.0f;

    for (auto& n : panel.notes)
    {
        const float x = st.beatToX (n.start);
        if (x < -4.0f || x > (float) getWidth())
            continue;

        const float top = 3.0f + h * (1.0f - (float) n.velocity / 127.0f);
        const bool sel = panel.isSelected (n);
        g.setColour (sel ? Colours::white : n.colour.brighter (0.2f));
        g.fillRect (x, top, 2.0f, (float) getHeight() - 3.0f - top);
        g.fillEllipse (x - 2.5f, top - 2.5f, 7.0f, 7.0f);
    }

    g.setColour (Theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void VelocityLane::mouseDown (const MouseEvent& e)
{
    targets.clear();
    if (panel.getEdit() == nullptr)
        return;

    panel.getEdit()->getUndoManager().beginNewTransaction ("Velocity");

    // nota mais próxima do clique
    const RollNote* nearest = nullptr;
    float best = 8.0f;
    for (auto& n : panel.notes)
    {
        const float d = std::abs (panel.state.beatToX (n.start) - e.position.x);
        if (d < best) { best = d; nearest = &n; }
    }

    if (nearest == nullptr)
        return;

    if (panel.isSelected (*nearest))
        for (auto* s : panel.getSelectedNotes())
            targets.push_back (s->note->state);
    else
        targets.push_back (nearest->note->state);

    setFromY (e);
}

void VelocityLane::mouseDrag (const MouseEvent& e)
{
    setFromY (e);
}

void VelocityLane::setFromY (const MouseEvent& e)
{
    const float h = (float) getHeight() - 6.0f;
    const int v = jlimit (1, 127, roundToInt ((1.0f - (e.position.y - 3.0f) / h) * 127.0f));

    for (auto& t : targets)
        if (auto* note = panel.findNote (t))
            note->setVelocity (v, panel.getUndo());

    panel.rebuildNotes();
    panel.repaintAll();
}

//==============================================================================
// PianoRollPanel
//==============================================================================
PianoRollPanel::PianoRollPanel()
{
    mode.addItemList ({ "Faixas selecionadas", "Global (todas as faixas)" }, 1);
    mode.setSelectedItemIndex (0, dontSendNotification);
    mode.onChange = [this] { rebuildClips(); rebuildNotes(); focusOnNotes(); repaintAll(); };
    addAndMakeVisible (mode);

    grid.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8 tercina", "1/16 tercina" }, 1);
    grid.setSelectedItemIndex (2, dontSendNotification);
    grid.onChange = [this]
    {
        const double values[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        state.gridBeats = values[jlimit (0, 5, grid.getSelectedItemIndex())];
        repaintAll();
    };
    addAndMakeVisible (grid);

    pencilTool.setButtonText ("Lápis"_t);

    auto setTool = [this] (RollState::Tool t)
    {
        state.tool = t;
        pointerTool.setToggleState (t == RollState::Tool::pointer, dontSendNotification);
        pencilTool.setToggleState (t == RollState::Tool::pencil, dontSendNotification);
        eraserTool.setToggleState (t == RollState::Tool::eraser, dontSendNotification);
    };

    pointerTool.onClick = [setTool] { setTool (RollState::Tool::pointer); };
    pencilTool.onClick  = [setTool] { setTool (RollState::Tool::pencil); };
    eraserTool.onClick  = [setTool] { setTool (RollState::Tool::eraser); };
    setTool (RollState::Tool::pointer);

    pointerTool.setTooltip ("Selecionar, mover e redimensionar notas (duplo clique cria/apaga)");
    pencilTool.setTooltip (String::fromUTF8 ("Desenhar notas (arraste para definir a duração)"));
    eraserTool.setTooltip ("Apagar notas (clique ou arraste por cima)");

    snapButton.setToggleState (true, dontSendNotification);
    snapButton.onClick = [this] { state.snap = ! state.snap; snapButton.setToggleState (state.snap, dontSendNotification); };
    snapButton.setTooltip ("Encaixar na grade");

    quantizeButton.onClick = [this] { quantizeSelected(); };
    quantizeButton.setTooltip ("Quantizar notas selecionadas (ou todas) pela grade  [Q]");

    zoomIn.onClick  = [this] { state.pixelsPerBeat = jmin (600.0, state.pixelsPerBeat * 1.4); resized(); repaintAll(); };
    zoomOut.onClick = [this] { state.pixelsPerBeat = jmax (8.0, state.pixelsPerBeat / 1.4); resized(); repaintAll(); };

    closeButton.onClick = [this] { if (onClose) onClose(); };
    closeButton.setTooltip ("Fechar o piano roll  [E]");

    for (auto* b : { &pointerTool, &pencilTool, &eraserTool, &snapButton })
        b->setColour (TextButton::buttonOnColourId, Theme::accent);

    for (auto* c : std::initializer_list<Component*> { &pointerTool, &pencilTool, &eraserTool, &snapButton, &quantizeButton,
                                                       &zoomIn, &zoomOut, &closeButton, &targetLabel })
        addAndMakeVisible (c);

    targetLabel.setColour (Label::textColourId, Theme::textDim);
    targetLabel.setFont (FontOptions (12.0f));

    addAndMakeVisible (ruler);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (noteGrid);
    addAndMakeVisible (velocity);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    state.scrollY = (int) ((127 - 84) * state.rowHeight);

    ZS::disableFocus (*this);
    noteGrid.setWantsKeyboardFocus (true);
    noteGrid.setMouseClickGrabsKeyboardFocus (true);

    startTimerHz (30);
}

PianoRollPanel::~PianoRollPanel()
{
    hScroll.removeListener (this);
    vScroll.removeListener (this);
    setEdit (nullptr, nullptr);
}

void PianoRollPanel::scrollBarMoved (ScrollBar* sb, double start)
{
    if (sb == &hScroll) state.viewStartBeat = start;
    else                state.scrollY = roundToInt (start);
    repaintAll();
}

//==============================================================================
void PianoRollPanel::setEdit (te::Edit* e, te::SelectionManager* sm)
{
    if (selectionManager != nullptr)
        selectionManager->removeChangeListener (this);
    if (edit != nullptr)
        edit->state.removeListener (this);

    previewNote (-1, false);
    edit = e;
    selectionManager = sm;
    focusClip = nullptr;
    selection.clear();
    clips.clear();
    notes.clear();

    if (edit != nullptr)
        edit->state.addListener (this);
    if (selectionManager != nullptr)
        selectionManager->addChangeListener (this);

    rebuildClips();
    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::showClip (te::MidiClip* clip)
{
    focusClip = clip;
    if (mode.getSelectedItemIndex() != 0)
        mode.setSelectedItemIndex (0, dontSendNotification);

    rebuildClips();
    rebuildNotes();

    if (clip != nullptr)
        state.viewStartBeat = jmax (0.0, clipStartBeat (*clip) - 1.0);

    focusOnNotes();
    repaintAll();
}

void PianoRollPanel::focusOnNotes()
{
    if (notes.empty())
        return;

    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }

    const int centre = (lo + hi) / 2;
    const int visibleRows = (int) (noteGrid.getHeight() / state.rowHeight);
    state.scrollY = jlimit (0, jmax (0, (int) (128 * state.rowHeight) - noteGrid.getHeight()),
                            (int) ((127 - centre - visibleRows / 2) * state.rowHeight));
    resized();
}

te::AudioTrack* PianoRollPanel::getTargetTrack() const
{
    if (edit == nullptr)
        return nullptr;

    if (selectionManager != nullptr)
        for (auto obj : selectionManager->getSelectedObjects())
        {
            if (auto t = dynamic_cast<te::AudioTrack*> (obj))
                return t;
            if (auto c = dynamic_cast<te::Clip*> (obj))
                if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                    return t;
        }

    if (focusClip != nullptr)
        return dynamic_cast<te::AudioTrack*> (focusClip->getTrack());

    for (auto t : ZS::getUserTracks (*edit))
        if (ZS::isInstrumentTrack (*t))
            return t;

    return nullptr;
}

void PianoRollPanel::rebuildClips()
{
    clips.clear();
    if (edit == nullptr)
        return;

    auto addClip = [this] (te::Clip* c)
    {
        if (auto mc = dynamic_cast<te::MidiClip*> (c))
            if (std::find (clips.begin(), clips.end(), mc) == clips.end())
                clips.push_back (mc);
    };

    if (mode.getSelectedItemIndex() == 1)
    {
        // Global: todas as regiões MIDI de todas as faixas
        for (auto t : ZS::getUserTracks (*edit))
            for (auto c : t->getClips())
                addClip (c);
    }
    else
    {
        // Faixas selecionadas: regiões selecionadas, ou todas as regiões da faixa selecionada
        juce::Array<te::AudioTrack*> tracks;

        if (selectionManager != nullptr)
            for (auto obj : selectionManager->getSelectedObjects())
            {
                if (auto c = dynamic_cast<te::Clip*> (obj))
                    addClip (c);
                else if (auto t = dynamic_cast<te::AudioTrack*> (obj))
                    tracks.addIfNotAlreadyThere (t);
            }

        if (clips.empty() && tracks.isEmpty())
            if (auto t = getTargetTrack())
                tracks.add (t);

        for (auto t : tracks)
            for (auto c : t->getClips())
                addClip (c);
    }

    std::sort (clips.begin(), clips.end(), [] (auto a, auto b) { return clipStartBeat (*a) < clipStartBeat (*b); });
    updateTargetLabel();
}

void PianoRollPanel::rebuildNotes()
{
    notes.clear();

    for (auto* c : clips)
    {
        auto col = c->getTrack() != nullptr ? c->getTrack()->getColour() : Theme::accent;
        const double offset = clipOffset (*c);
        const double length = c->getLengthInBeats().inBeats();

        for (auto n : c->getSequence().getNotes())
        {
            const double s = n->getStartBeat().inBeats();
            if (s < offset - 1.0e-6 || s >= offset + length)
                continue;

            RollNote rn;
            rn.clip = c;
            rn.note = n;
            rn.start = toEditBeat (*c, s);
            rn.end = toEditBeat (*c, n->getEndBeat().inBeats());
            rn.pitch = n->getNoteNumber();
            rn.velocity = n->getVelocity();
            rn.colour = col;
            notes.push_back (rn);
        }
    }

    // remove da seleção o que não existe mais
    selection.erase (std::remove_if (selection.begin(), selection.end(), [this] (const ValueTree& v)
    {
        return findNote (v) == nullptr;
    }), selection.end());
}

te::MidiNote* PianoRollPanel::findNote (const ValueTree& v, te::MidiClip** clipOut) const
{
    for (auto* c : clips)
        for (auto n : c->getSequence().getNotes())
            if (n->state == v)
            {
                if (clipOut != nullptr) *clipOut = c;
                return n;
            }

    return nullptr;
}

void PianoRollPanel::setNoteBeats (te::MidiClip& c, te::MidiNote& n, double start, double end, UndoManager* um)
{
    n.setStartAndLength (te::BeatPosition::fromBeats (toNoteBeat (c, start)),
                         te::BeatDuration::fromBeats (jmax (1.0 / 64.0, end - start)), um);
}

bool PianoRollPanel::isSelected (const RollNote& n) const
{
    return std::find (selection.begin(), selection.end(), n.note->state) != selection.end();
}

void PianoRollPanel::select (const RollNote& n, bool add)
{
    if (! add)
        selection.clear();

    if (! isSelected (n))
        selection.push_back (n.note->state);

    repaintAll();
}

void PianoRollPanel::clearSelection()
{
    selection.clear();
}

std::vector<RollNote*> PianoRollPanel::getSelectedNotes()
{
    std::vector<RollNote*> result;
    for (auto& n : notes)
        if (isSelected (n))
            result.push_back (&n);
    return result;
}

void PianoRollPanel::selectAll()
{
    selection.clear();
    for (auto& n : notes)
        selection.push_back (n.note->state);
    repaintAll();
}

RollNote* PianoRollPanel::createNoteAt (double beat, int pitch, double length)
{
    auto* track = getTargetTrack();
    if (edit == nullptr || track == nullptr)
        return nullptr;

    beat = jmax (0.0, beat);
    te::MidiClip* target = nullptr;

    for (auto c : track->getClips())
        if (auto mc = dynamic_cast<te::MidiClip*> (c))
            if (beat >= clipStartBeat (*mc) - 1.0e-6 && beat < clipEndBeat (*mc))
                target = mc;

    if (target == nullptr)
    {
        // fora de qualquer região: cria uma região de 1 compasso na faixa ativa
        const int bpb = beatsPerBar (*edit);
        const double barStart = std::floor (beat / bpb) * bpb;
        auto& ts = edit->tempoSequence;
        te::TimeRange range (ts.toTime (te::BeatPosition::fromBeats (barStart)),
                             ts.toTime (te::BeatPosition::fromBeats (barStart + bpb)));

        if (auto mc = track->insertMIDIClip (range, nullptr))
        {
            mc->setName (track->getName());
            target = mc.get();
        }
    }

    if (target == nullptr)
        return nullptr;

    auto* n = target->getSequence().addNote (pitch, te::BeatPosition::fromBeats (toNoteBeat (*target, beat)),
                                             te::BeatDuration::fromBeats (length), 100, 0, getUndo());
    if (n == nullptr)
        return nullptr;

    rebuildClips();
    rebuildNotes();
    selection = { n->state };

    for (auto& rn : notes)
        if (rn.note == n)
            return &rn;

    return nullptr;
}

void PianoRollPanel::deleteSelected()
{
    if (edit == nullptr || selection.empty())
        return;

    edit->getUndoManager().beginNewTransaction ("Apagar notas");
    auto sel = selection;

    for (auto& v : sel)
    {
        te::MidiClip* clip = nullptr;
        if (auto* n = findNote (v, &clip))
            clip->getSequence().removeNote (*n, getUndo());
    }

    selection.clear();
    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::quantizeSelected()
{
    if (edit == nullptr)
        return;

    edit->getUndoManager().beginNewTransaction ("Quantizar notas");
    const bool all = selection.empty();
    const double g = state.gridBeats;

    for (auto& rn : notes)
    {
        if (! all && ! isSelected (rn))
            continue;

        const double len = rn.end - rn.start;
        const double s = jmax (clipStartBeat (*rn.clip), std::round (rn.start / g) * g);
        const double l = jmax (g, std::round (len / g) * g);
        setNoteBeats (*rn.clip, *rn.note, s, s + l, getUndo());
    }

    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::transposeSelected (int semitones)
{
    if (edit == nullptr || selection.empty())
        return;

    edit->getUndoManager().beginNewTransaction ("Transpor notas");

    for (auto* rn : getSelectedNotes())
        rn->note->setNoteNumber (jlimit (0, 127, rn->pitch + semitones), getUndo());

    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::nudgeSelected (double beats)
{
    if (edit == nullptr || selection.empty())
        return;

    edit->getUndoManager().beginNewTransaction ("Mover notas");

    for (auto* rn : getSelectedNotes())
    {
        const double s = jmax (clipStartBeat (*rn->clip), rn->start + beats);
        setNoteBeats (*rn->clip, *rn->note, s, s + (rn->end - rn->start), getUndo());
    }

    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::previewNote (int pitch, bool on)
{
    if (on)
    {
        previewNote (-1, false);
        previewTrack = getTargetTrack();

        if (previewTrack != nullptr && pitch >= 0)
        {
            previewTrack->injectLiveMidiMessage (MidiMessage::noteOn (1, pitch, (uint8) 100), previewSource);
            previewPitch = pitch;
        }
        return;
    }

    if (previewTrack != nullptr && previewPitch >= 0)
        previewTrack->injectLiveMidiMessage (MidiMessage::noteOff (1, previewPitch), previewSource);

    previewPitch = -1;
    previewTrack = nullptr;
}

void PianoRollPanel::updateTargetLabel()
{
    auto* t = getTargetTrack();
    targetLabel.setText (t != nullptr ? "Escrevendo em: "_t + t->getName() : String ("Nenhuma faixa de instrumento"),
                         dontSendNotification);
}

void PianoRollPanel::handleAsyncUpdate()
{
    rebuildClips();
    rebuildNotes();
    repaintAll();
}

void PianoRollPanel::changeListenerCallback (ChangeBroadcaster*)
{
    // a seleção mudou no arranjo
    if (mode.getSelectedItemIndex() == 0)
    {
        rebuildClips();
        rebuildNotes();
    }

    updateTargetLabel();
    repaintAll();
}

void PianoRollPanel::timerCallback()
{
    if (edit == nullptr || ! isShowing())
        return;

    auto& transport = edit->getTransport();
    auto beat = edit->tempoSequence.toBeats (transport.getPosition()).inBeats();
    const float x = state.beatToX (beat);

    // acompanha o cursor durante a reprodução
    if (transport.isPlaying() && (x > (float) noteGrid.getWidth() * 0.92f || x < 0.0f))
    {
        state.viewStartBeat = jmax (0.0, beat - 1.0);
        resized();
        repaintAll();
        return;
    }

    if (std::abs (x - lastPlayheadX) > 0.5f)
    {
        noteGrid.repaint (Rectangle<float> (lastPlayheadX - 2.0f, 0.0f, 5.0f, (float) noteGrid.getHeight()).getSmallestIntegerContainer());
        noteGrid.repaint (Rectangle<float> (x - 2.0f, 0.0f, 5.0f, (float) noteGrid.getHeight()).getSmallestIntegerContainer());
        ruler.repaint();
        lastPlayheadX = x;
    }
}

void PianoRollPanel::repaintAll()
{
    noteGrid.repaint();
    keyboard.repaint();
    ruler.repaint();
    velocity.repaint();
}

void PianoRollPanel::paint (Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    g.setColour (Theme::accent);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText ("PIANO ROLL", 10, 6, 90, 24, Justification::centredLeft);
}

void PianoRollPanel::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (36).reduced (6, 5);
    bar.removeFromLeft (92);

    closeButton.setBounds (bar.removeFromRight (30));
    bar.removeFromRight (8);

    mode.setBounds (bar.removeFromLeft (200));                bar.removeFromLeft (10);
    pointerTool.setBounds (bar.removeFromLeft (70));          bar.removeFromLeft (3);
    pencilTool.setBounds (bar.removeFromLeft (60));           bar.removeFromLeft (3);
    eraserTool.setBounds (bar.removeFromLeft (76));           bar.removeFromLeft (10);
    grid.setBounds (bar.removeFromLeft (120));                bar.removeFromLeft (3);
    snapButton.setBounds (bar.removeFromLeft (56));           bar.removeFromLeft (3);
    quantizeButton.setBounds (bar.removeFromLeft (80));       bar.removeFromLeft (10);
    zoomOut.setBounds (bar.removeFromLeft (26));              bar.removeFromLeft (2);
    zoomIn.setBounds (bar.removeFromLeft (26));               bar.removeFromLeft (10);
    targetLabel.setBounds (bar);

    const int keyW = 64, sb = 10, velH = 70, rulerH = 22;
    auto rulerRow = r.removeFromTop (rulerH);
    rulerRow.removeFromLeft (keyW);
    ruler.setBounds (rulerRow.withTrimmedRight (sb));

    auto hRow = r.removeFromBottom (sb);
    hRow.removeFromLeft (keyW);
    hScroll.setBounds (hRow.withTrimmedRight (sb));

    auto velRow = r.removeFromBottom (velH);
    velRow.removeFromLeft (keyW);
    velocity.setBounds (velRow.withTrimmedRight (sb));

    vScroll.setBounds (r.removeFromRight (sb));
    keyboard.setBounds (r.removeFromLeft (keyW));
    noteGrid.setBounds (r);

    // barras de rolagem
    double lastBeat = 64.0;
    for (auto* c : clips) lastBeat = jmax (lastBeat, clipEndBeat (*c) + 16.0);
    const double visibleBeats = noteGrid.getWidth() / state.pixelsPerBeat;
    hScroll.setRangeLimits (0.0, jmax (lastBeat, state.viewStartBeat + visibleBeats));
    hScroll.setCurrentRange (state.viewStartBeat, visibleBeats, dontSendNotification);

    vScroll.setRangeLimits (0.0, 128.0 * state.rowHeight);
    vScroll.setCurrentRange (state.scrollY, noteGrid.getHeight(), dontSendNotification);
}
