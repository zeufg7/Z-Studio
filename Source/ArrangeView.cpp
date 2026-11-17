#include "ArrangeView.h"

using namespace juce;

//==============================================================================
// ViewState
//==============================================================================
double ViewState::getGridBeats() const
{
    const auto bpm = edit.tempoSequence.getBpmAt (te::TimePosition());
    const auto pxPerBeat = pixelsPerSecond * 60.0 / bpm;
    const int beatsPerBar = jmax (1, edit.tempoSequence.getTimeSig (0)->numerator.get());

    if (pxPerBeat >= 80.0)  return 0.25;
    if (pxPerBeat >= 40.0)  return 0.5;
    if (pxPerBeat >= 14.0)  return 1.0;
    return (double) beatsPerBar;
}

te::TimePosition ViewState::snap (te::TimePosition t) const
{
    if (! snapToGrid)
        return t;

    const auto grid = getGridBeats();
    auto beats = edit.tempoSequence.toBeats (t).inBeats();
    beats = std::round (beats / grid) * grid;
    return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (std::max (0.0, beats)));
}

void ViewState::setZoom (double pps, float anchorX)
{
    const auto anchorTime = viewStart + anchorX / pixelsPerSecond;
    pixelsPerSecond = jlimit (2.0, 2000.0, pps);
    viewStart = std::max (0.0, anchorTime - anchorX / pixelsPerSecond);
    sendChangeMessage();
}

//==============================================================================
// Desenho da grade de compassos/batidas (compartilhado entre régua e faixas)
namespace
{
    template <typename Fn>
    void forEachGridLine (const ViewState& vs, int width, Fn&& fn)
    {
        auto& ts = vs.edit.tempoSequence;
        const auto bpm = ts.getBpmAt (te::TimePosition());
        const auto pxPerBeat = vs.pixelsPerSecond * 60.0 / bpm;
        const int beatsPerBar = jmax (1, ts.getTimeSig (0)->numerator.get());
        const auto pxPerBar = pxPerBeat * beatsPerBar;

        // Quantos compassos pular entre linhas numeradas, conforme o zoom
        int barStep = 1;
        while (pxPerBar * barStep < 48.0 && barStep < 1024)
            barStep *= 2;

        const bool drawBeats = pxPerBeat >= 10.0;

        const auto startBeat = std::floor (ts.toBeats (vs.xToTime (0.0f)).inBeats());
        const auto endBeat   = std::ceil  (ts.toBeats (vs.xToTime ((float) width)).inBeats()) + 1.0;

        for (double b = std::max (0.0, startBeat); b <= endBeat; b += 1.0)
        {
            const auto t  = ts.toTime (te::BeatPosition::fromBeats (b));
            const auto bb = ts.toBarsAndBeats (t);
            const bool isBar = bb.beats.inBeats() < 1.0e-6;

            if (isBar && (bb.bars % barStep) == 0)
                fn (vs.timeToX (t), true, bb.bars + 1);
            else if (drawBeats && ! (isBar && barStep > 1))
                fn (vs.timeToX (t), false, 0);
        }
    }
}

//==============================================================================
// ClipComponent
//==============================================================================
ClipComponent::ClipComponent (ViewState& vs, te::Clip::Ptr c, ArrangeView& a)
    : viewState (vs), clip (c), arrange (a)
{
    updateThumbnail();
}

ClipComponent::~ClipComponent() = default;

void ClipComponent::updateThumbnail()
{
    if (auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
    {
        te::AudioFile af (wac->getAudioFile());

        if (af.isValid())
        {
            const te::AudioFile proxy (wac->getPlaybackFile());

            if (thumbnail == nullptr)
                thumbnail = std::make_unique<te::SmartThumbnail> (wac->edit.engine, proxy, *this, &wac->edit);
            else
                thumbnail->setNewFile (proxy);
        }
    }
}

void ClipComponent::paint (Graphics& g)
{
    auto r = getLocalBounds();
    const bool selected = viewState.selection.isSelected (clip.get());
    auto colour = clip->getTrack() != nullptr ? clip->getTrack()->getColour() : Theme::accent;

    if (colour.isTransparent())
        colour = Theme::accent;

    // Corpo
    g.setColour (colour.withAlpha (0.28f).overlaidWith (Theme::lane.withAlpha (0.4f)));
    g.fillRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f);

    // Faixa de título
    auto titleArea = r.removeFromTop (16);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (titleArea.toFloat().reduced (0.5f), 4.0f);
    g.setColour (Colour (0xff111111));
    g.setFont (FontOptions (12.0f));
    g.drawText (clip->getName(), titleArea.reduced (5, 0), Justification::centredLeft, true);

    if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        paintMidi (g, *mc, r.reduced (1, 3));
    else if (auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        paintAudio (g, *wac, r.reduced (1, 2));

    g.setColour (selected ? Theme::text : colour.darker (0.3f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);
}

void ClipComponent::paintMidi (Graphics& g, te::MidiClip& mc, Rectangle<int> r)
{
    auto& notes = mc.getSequence().getNotes();

    if (notes.isEmpty() || r.getHeight() <= 2)
        return;

    int lo = 127, hi = 0;
    for (auto n : notes)
    {
        lo = jmin (lo, n->getNoteNumber());
        hi = jmax (hi, n->getNoteNumber());
    }

    lo = jmax (0, lo - 2);
    hi = jmin (127, hi + 2);
    const auto range = (float) jmax (12, hi - lo);
    const auto noteH = jlimit (2.0f, 6.0f, (float) r.getHeight() / range);

    auto colour = mc.getTrack() != nullptr ? mc.getTrack()->getColour() : Theme::accent;
    auto& ts = viewState.edit.tempoSequence;
    const auto clipStartX = viewState.timeToX (mc.getPosition().getStart());

    for (auto n : notes)
    {
        auto s = ts.toTime (mc.getStartBeat() + toDuration (n->getStartBeat()) - mc.getOffsetInBeats());
        auto e = ts.toTime (mc.getStartBeat() + toDuration (n->getEndBeat())   - mc.getOffsetInBeats());

        const auto x1 = viewState.timeToX (s) - clipStartX;
        const auto x2 = viewState.timeToX (e) - clipStartX;
        const auto y = (float) r.getY() + (1.0f - (float) (n->getNoteNumber() - lo) / range) * ((float) r.getHeight() - noteH);

        g.setColour (colour.brighter (0.5f).withAlpha (0.5f + 0.5f * (float) n->getVelocity() / 127.0f));
        g.fillRect (x1, y, jmax (1.5f, x2 - x1), noteH);
    }
}

void ClipComponent::paintAudio (Graphics& g, te::WaveAudioClip& wac, Rectangle<int> r)
{
    if (thumbnail == nullptr)
        return;

    if (thumbnail->isGeneratingProxy())
    {
        g.setColour (Theme::textDim);
        g.drawText ("Processando...", r, Justification::centred);
        return;
    }

    const auto pos = wac.getPosition();
    const auto speed = wac.getSpeedRatio();
    const auto t1 = te::TimePosition::fromSeconds (pos.getOffset().inSeconds() * speed);
    const auto t2 = t1 + te::TimeDuration::fromSeconds (pos.getLength().inSeconds() * speed);

    auto colour = wac.getTrack() != nullptr ? wac.getTrack()->getColour() : Theme::accent;
    g.setColour (colour.brighter (0.6f).withAlpha (0.9f));
    thumbnail->drawChannels (g, r, { t1, t2 }, wac.getGain());
}

ClipComponent::DragMode ClipComponent::getModeFor (int x) const
{
    const int edge = jmin (8, getWidth() / 4);

    if (x < edge)                 return DragMode::trimStart;
    if (x > getWidth() - edge)    return DragMode::trimEnd;
    return DragMode::move;
}

void ClipComponent::mouseMove (const MouseEvent& e)
{
    auto mode = getModeFor (e.x);
    setMouseCursor (mode == DragMode::move ? MouseCursor::NormalCursor : MouseCursor::LeftRightResizeCursor);
}

void ClipComponent::mouseDown (const MouseEvent& e)
{
    if (e.mods.isShiftDown())
        viewState.selection.addToSelection (clip.get());
    else
        viewState.selection.selectOnly (clip.get());

    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem ("Apagar clipe", [c = clip] { c->removeFromParent(); });
        m.addItem ("Renomear...", [this]
        {
            auto* w = new AlertWindow ("Renomear clipe", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", clip->getName());
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([w, c = clip] (int r)
            {
                if (r == 1)
                    c->setName (w->getTextEditorContents ("name"));
            }), true);
        });
        m.showMenuAsync ({});
        return;
    }

    viewState.edit.getUndoManager().beginNewTransaction ("Editar clipe");
    dragMode = getModeFor (e.x);
    dragStartClipStart = clip->getPosition().getStart();
    dragStartClipEnd   = clip->getPosition().getEnd();
    dragStartMouseTime = viewState.xToTime ((float) e.getEventRelativeTo (getParentComponent()).x);
}

void ClipComponent::mouseDrag (const MouseEvent& e)
{
    if (dragMode == DragMode::none || e.mods.isPopupMenu())
        return;

    const auto mouseTime = viewState.xToTime ((float) e.getEventRelativeTo (getParentComponent()).x);
    const auto delta = mouseTime - dragStartMouseTime;

    if (dragMode == DragMode::move)
    {
        auto newStart = viewState.snap (te::TimePosition::fromSeconds (std::max (0.0, (dragStartClipStart + delta).inSeconds())));
        clip->setStart (newStart, false, true);
    }
    else if (dragMode == DragMode::trimEnd)
    {
        auto newEnd = viewState.snap (dragStartClipEnd + delta);
        if (newEnd > dragStartClipStart + 0.01s)
            clip->setEnd (newEnd, true);
    }
    else if (dragMode == DragMode::trimStart)
    {
        auto newStart = viewState.snap (te::TimePosition::fromSeconds (std::max (0.0, (dragStartClipStart + delta).inSeconds())));
        if (newStart < dragStartClipEnd - 0.01s)
            clip->setStart (newStart, true, false);
    }
}

void ClipComponent::mouseUp (const MouseEvent& e)
{
    if (dragMode == DragMode::move && e.mouseWasDraggedSinceMouseDown())
    {
        // Arrastou para outra faixa?
        if (auto target = arrange.getTrackAtScreenPosition (e.getScreenPosition()))
            if (target != clip->getTrack())
                clip->moveTo (*target);
    }

    dragMode = DragMode::none;
}

void ClipComponent::mouseDoubleClick (const MouseEvent&)
{
    if (dynamic_cast<te::MidiClip*> (clip.get()) != nullptr)
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Piano roll",
                                          "O editor de notas (piano roll) chega na versão 0.2."_t);
}

//==============================================================================
// TrackHeaderComponent
//==============================================================================
TrackHeaderComponent::TrackHeaderComponent (ViewState& vs, te::AudioTrack::Ptr t)
    : viewState (vs), track (t)
{
    name.setText (track->getName(), dontSendNotification);
    name.setEditable (false, true, false);
    name.setFont (FontOptions (14.0f, Font::bold));
    name.setColour (Label::textColourId, Theme::text);
    name.onTextChange = [this] { track->setName (name.getText()); };
    addAndMakeVisible (name);

    for (auto* b : { &muteButton, &soloButton, &armButton, &inputButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    muteButton.setColour (TextButton::buttonOnColourId, Theme::mute);
    soloButton.setColour (TextButton::buttonOnColourId, Theme::solo);
    armButton.setColour (TextButton::buttonOnColourId, Theme::record);

    muteButton.onClick = [this] { track->setMute (! track->isMuted (false)); };
    soloButton.onClick = [this] { track->setSolo (! track->isSolo (false)); };
    armButton.onClick  = [this]
    {
        if (ZS::getInputsTargeting (*track).isEmpty())
        {
            if (ZS::isInstrumentTrack (*track))
                ZS::routeMidiInputsTo (*track);
            else
                showInputMenu();
            return;
        }

        ZS::armTrack (*track, ! ZS::isTrackArmed (*track));
        updateButtons();
    };
    inputButton.onClick = [this] { showInputMenu(); };
    inputButton.setVisible (! ZS::isInstrumentTrack (*track));

    volume.setRange (-60.0, 6.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTooltip ("Volume (dB) - duplo clique volta a 0 dB");
    if (auto vp = track->getVolumePlugin())
        volume.setValue (vp->getVolumeDb(), dontSendNotification);
    volume.onValueChange = [this]
    {
        if (auto vp = track->getVolumePlugin())
            vp->setVolumeDb ((float) volume.getValue());
    };
    addAndMakeVisible (volume);

    if (auto lm = track->getLevelMeterPlugin())
    {
        meterPlugin = lm;
        meterPlugin->measurer.addClient (meterClient);
    }

    ZS::disableFocus (*this);

    track->state.addListener (this);
    track->edit.state.getChildWithName (te::IDs::INPUTDEVICES).addListener (this);
    updateButtons();
    startTimerHz (24);
}

TrackHeaderComponent::~TrackHeaderComponent()
{
    if (meterPlugin != nullptr)
        meterPlugin->measurer.removeClient (meterClient);

    track->edit.state.getChildWithName (te::IDs::INPUTDEVICES).removeListener (this);
    track->state.removeListener (this);
}

void TrackHeaderComponent::valueTreeChanged()
{
    name.setText (track->getName(), dontSendNotification);
    updateButtons();
    repaint();
}

void TrackHeaderComponent::updateButtons()
{
    muteButton.setToggleState (track->isMuted (false), dontSendNotification);
    soloButton.setToggleState (track->isSolo (false), dontSendNotification);
    armButton.setToggleState (ZS::isTrackArmed (*track), dontSendNotification);
}

void TrackHeaderComponent::timerCallback()
{
    // Medidor de nível
    float db = -100.0f;
    for (int ch = 0; ch < jmax (1, meterClient.getNumChannelsUsed()); ++ch)
        db = jmax (db, meterClient.getAndClearAudioLevel (ch).dB);

    auto level = jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    meterLevel = level > meterLevel ? level : meterLevel * 0.85f;

    if (! volume.isMouseButtonDown())
        if (auto vp = track->getVolumePlugin())
            if (std::abs (vp->getVolumeDb() - (float) volume.getValue()) > 0.05f)
                volume.setValue (vp->getVolumeDb(), dontSendNotification);

    armButton.setToggleState (ZS::isTrackArmed (*track), dontSendNotification);
    repaint (getLocalBounds().removeFromRight (8));
}

void TrackHeaderComponent::paint (Graphics& g)
{
    const bool selected = viewState.selection.isSelected (track.get());
    auto r = getLocalBounds();

    g.setColour (selected ? Theme::panelLight.brighter (0.06f) : Theme::panel);
    g.fillRect (r);

    // Faixa de cor da trilha
    g.setColour (track->getColour());
    g.fillRect (r.removeFromLeft (5));

    // Ícone do tipo
    g.setColour (Theme::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText (ZS::isInstrumentTrack (*track) ? "INSTRUMENTO" : "ÁUDIO"_t,
                getLocalBounds().withTrimmedLeft (12).withTrimmedTop (getHeight() - 16), Justification::centredLeft);

    // Medidor
    auto meter = getLocalBounds().removeFromRight (8).reduced (2, 6);
    g.setColour (Theme::lcd);
    g.fillRect (meter);
    auto filled = meter.withTrimmedTop (roundToInt ((1.0f - meterLevel) * (float) meter.getHeight()));
    g.setGradientFill (ColourGradient (Theme::record, (float) meter.getX(), (float) meter.getY(),
                                       Colour (0xff47c75e), (float) meter.getX(), (float) meter.getBottom(), false));
    g.fillRect (filled);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackHeaderComponent::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (10).withTrimmedRight (12).reduced (0, 4);
    auto top = r.removeFromTop (22);
    name.setBounds (top);

    auto buttons = r.removeFromTop (22);
    const int bw = 26;
    muteButton.setBounds (buttons.removeFromLeft (bw));   buttons.removeFromLeft (3);
    soloButton.setBounds (buttons.removeFromLeft (bw));   buttons.removeFromLeft (3);
    armButton.setBounds (buttons.removeFromLeft (bw));    buttons.removeFromLeft (3);
    inputButton.setBounds (buttons.removeFromLeft (34));

    r.removeFromTop (2);
    volume.setBounds (r.removeFromTop (16));
}

void TrackHeaderComponent::mouseDown (const MouseEvent& e)
{
    viewState.selection.selectOnly (track.get());

    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem ("Renomear", [this] { name.showEditor(); });
        m.addItem ("Apagar faixa", [this] { track->edit.deleteTrack (track.get()); });
        m.showMenuAsync ({});
    }
}

void TrackHeaderComponent::showInputMenu()
{
    PopupMenu m;
    auto& edit = track->edit;
    m.addSectionHeader ("Entrada de áudio"_t);

    int id = 1;
    std::vector<te::InputDeviceInstance*> waveInputs;

    for (auto instance : edit.getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
            continue;

        waveInputs.push_back (instance);
        const bool ticked = te::isOnTargetTrack (*instance, *track, 0);
        m.addItem (id++, instance->getInputDevice().getName(), true, ticked);
    }

    if (waveInputs.empty())
        m.addItem (-1, "Nenhuma entrada ativa (Opções > Áudio e MIDI)"_t, false);

    m.addSeparator();
    m.addItem (1000, "Sem entrada");

    auto inputs = ZS::getInputsTargeting (*track);
    if (! inputs.isEmpty())
    {
        const bool monitoring = inputs.getFirst()->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on;
        m.addItem (1001, "Monitorar entrada sempre", true, monitoring);
    }

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&inputButton),
                     [this, waveInputs] (int res)
    {
        if (res >= 1 && res <= (int) waveInputs.size())
        {
            [[maybe_unused]] auto r = waveInputs[(size_t) res - 1]->setTarget (track->itemID, true, &track->edit.getUndoManager(), 0);
            ZS::armTrack (*track, true);
        }
        else if (res == 1000)
        {
            for (auto instance : ZS::getInputsTargeting (*track))
                [[maybe_unused]] auto r = instance->removeTarget (track->itemID, &track->edit.getUndoManager());
        }
        else if (res == 1001)
        {
            for (auto instance : ZS::getInputsTargeting (*track))
            {
                auto& dev = instance->getInputDevice();
                dev.setMonitorMode (dev.getMonitorMode() == te::InputDevice::MonitorMode::on
                                        ? te::InputDevice::MonitorMode::automatic
                                        : te::InputDevice::MonitorMode::on);
            }
        }

        updateButtons();
    });
}

//==============================================================================
// TrackLaneComponent
//==============================================================================
TrackLaneComponent::TrackLaneComponent (ViewState& vs, te::AudioTrack::Ptr t, ArrangeView& a)
    : viewState (vs), track (t), arrange (a)
{
    track->state.addListener (this);
    triggerAsyncUpdate();
    startTimerHz (15);
}

TrackLaneComponent::~TrackLaneComponent()
{
    track->state.removeListener (this);
}

void TrackLaneComponent::valueTreePropertyChanged (ValueTree& v, const Identifier& i)
{
    if (te::Clip::isClipState (v))
    {
        if (i == te::IDs::start || i == te::IDs::length || i == te::IDs::offset)
            layoutClips();

        if (i == te::IDs::name)
            for (auto c : clips)
                c->repaint();
    }
    else if (v == track->state && i == te::IDs::colour)
    {
        for (auto c : clips)
            c->repaint();
    }
}

void TrackLaneComponent::valueTreeChildAdded (ValueTree&, ValueTree& c)
{
    if (te::Clip::isClipState (c))
    {
        needsRebuild = true;
        triggerAsyncUpdate();
    }
    else if (c.hasType (te::IDs::NOTE))
    {
        for (auto cc : clips) cc->repaint();
    }
}

void TrackLaneComponent::valueTreeChildRemoved (ValueTree&, ValueTree& c, int)
{
    if (te::Clip::isClipState (c))
    {
        needsRebuild = true;
        triggerAsyncUpdate();
    }
}

void TrackLaneComponent::handleAsyncUpdate()
{
    if (needsRebuild)
        buildClips();
}

void TrackLaneComponent::buildClips()
{
    needsRebuild = false;
    clips.clear();

    for (auto c : track->getClips())
    {
        auto cc = clips.add (new ClipComponent (viewState, c, arrange));
        addAndMakeVisible (cc);
    }

    layoutClips();
}

void TrackLaneComponent::layoutClips()
{
    for (auto cc : clips)
    {
        auto pos = cc->getClip().getPosition();
        auto x1 = viewState.timeToX (pos.getStart());
        auto x2 = viewState.timeToX (pos.getEnd());
        cc->setBounds (roundToInt (x1), 2, jmax (4, roundToInt (x2 - x1)), getHeight() - 4);
    }
}

void TrackLaneComponent::resized()
{
    layoutClips();
}

void TrackLaneComponent::paint (Graphics& g)
{
    const bool selected = viewState.selection.isSelected (track.get());
    g.fillAll (selected ? Theme::lane.brighter (0.04f) : Theme::lane);

    forEachGridLine (viewState, getWidth(), [&] (float x, bool isBar, int)
    {
        g.setColour (isBar ? Theme::gridBar : Theme::gridBeat);
        g.drawVerticalLine (roundToInt (x), 0.0f, (float) getHeight());
    });

    g.setColour (Theme::outline.withAlpha (0.6f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackLaneComponent::paintOverChildren (Graphics& g)
{
    if (! recordingArea.isEmpty())
    {
        g.setColour (Theme::record.withAlpha (0.35f));
        g.fillRoundedRectangle (recordingArea.toFloat(), 4.0f);
        g.setColour (Theme::record);
        g.drawRoundedRectangle (recordingArea.toFloat(), 4.0f, 1.5f);
        g.setColour (Theme::text);
        g.setFont (FontOptions (12.0f));
        g.drawText ("Gravando...", recordingArea.reduced (6, 2), Justification::topLeft);
    }
}

void TrackLaneComponent::timerCallback()
{
    Rectangle<int> newArea;
    auto& transport = track->edit.getTransport();

    if (transport.isRecording() && ZS::isTrackArmed (*track))
    {
        auto start = transport.getTimeWhenStarted();
        auto now   = transport.getPosition();

        if (now > start)
        {
            auto x1 = roundToInt (viewState.timeToX (start));
            auto x2 = roundToInt (viewState.timeToX (now));
            newArea = Rectangle<int> (x1, 2, jmax (2, x2 - x1), getHeight() - 4);
        }
    }

    if (newArea != recordingArea)
    {
        repaint (recordingArea.getUnion (newArea).expanded (2));
        recordingArea = newArea;
    }
}

void TrackLaneComponent::mouseDown (const MouseEvent& e)
{
    viewState.selection.selectOnly (track.get());

    if (e.mods.isPopupMenu())
        return;

    // Clique no espaço vazio posiciona o cursor (como no Cubase)
    if (! track->edit.getTransport().isPlaying())
        track->edit.getTransport().setPosition (viewState.snap (viewState.xToTime ((float) e.x)));
}

void TrackLaneComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (! ZS::isInstrumentTrack (*track))
        return;

    // Duplo clique cria um clipe MIDI vazio de 4 compassos (como no Cubase)
    auto& ts = track->edit.tempoSequence;
    auto clickTime = viewState.xToTime ((float) e.x);
    auto bars = ts.toBarsAndBeats (clickTime).bars;
    auto start = ts.toTime (te::tempo::BarsAndBeats { bars });
    auto end   = ts.toTime (te::tempo::BarsAndBeats { bars + 4 });

    track->edit.getUndoManager().beginNewTransaction ("Novo clipe MIDI");

    if (auto mc = track->insertMIDIClip ({ start, end }, &viewState.selection))
        mc->setName (track->getName());
}

bool TrackLaneComponent::isInterestedInFileDrag (const StringArray& files)
{
    auto& formats = track->edit.engine.getAudioFileFormatManager().readFormatManager;

    for (auto& f : files)
    {
        File file (f);
        if (file.hasFileExtension ("mid;midi") || formats.findFormatForFileExtension (file.getFileExtension()) != nullptr)
            return true;
    }

    return false;
}

void TrackLaneComponent::filesDropped (const StringArray& files, int x, int)
{
    auto& edit = track->edit;
    auto pos = viewState.snap (viewState.xToTime ((float) x));
    edit.getUndoManager().beginNewTransaction ("Importar arquivos");

    for (auto& f : files)
    {
        File file (f);

        if (file.hasFileExtension ("mid;midi"))
        {
            FileInputStream in (file);
            MidiFile mf;

            if (! in.openedOk() || ! mf.readFrom (in))
                continue;

            const auto tpq = (double) jmax ((short) 1, mf.getTimeFormat());
            MidiMessageSequence merged;

            for (int i = 0; i < mf.getNumTracks(); ++i)
                merged.addSequence (*mf.getTrack (i), 0.0);

            for (auto* ev : merged)
            {
                ev->message.setTimeStamp (ev->message.getTimeStamp() / tpq);   // ticks -> batidas
                if (ev->message.getChannel() > 0)
                    ev->message.setChannel (1);
            }

            merged.sort();
            merged.updateMatchedPairs();

            const auto lengthBeats = jmax (4.0, std::ceil (merged.getEndTime() / 4.0) * 4.0);
            auto& ts = edit.tempoSequence;
            auto startBeat = ts.toBeats (pos);
            auto end = ts.toTime (startBeat + te::BeatDuration::fromBeats (lengthBeats));

            if (auto mc = track->insertMIDIClip (file.getFileNameWithoutExtension(), { pos, end }, nullptr))
            {
                mc->getSequence().importMidiSequence (merged, nullptr, {}, &edit.getUndoManager());
                pos = end;
            }
        }
        else
        {
            te::AudioFile audioFile (edit.engine, file);

            if (! audioFile.isValid())
                continue;

            auto length = te::TimeDuration::fromSeconds (audioFile.getLength());

            if (auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file,
                                                   { { pos, length }, {} }, false))
                pos = clip->getPosition().getEnd();
        }
    }
}

//==============================================================================
// RulerComponent
//==============================================================================
RulerComponent::RulerComponent (ViewState& vs) : viewState (vs)
{
    startTimerHz (10);
}

void RulerComponent::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (Theme::panel);

    auto& transport = viewState.edit.getTransport();
    auto cycleArea = r.removeFromTop (12);

    // Região de loop (cycle)
    auto loop = transport.getLoopRange();
    if (loop.getLength() > 0s)
    {
        auto x1 = viewState.timeToX (loop.getStart());
        auto x2 = viewState.timeToX (loop.getEnd());
        auto col = transport.looping ? Theme::loopRegion : Theme::outline;
        g.setColour (col.withAlpha (transport.looping ? 0.85f : 0.6f));
        g.fillRect (Rectangle<float> (x1, (float) cycleArea.getY() + 1.0f, x2 - x1, (float) cycleArea.getHeight() - 2.0f));
    }

    g.setFont (FontOptions (11.0f));

    forEachGridLine (viewState, getWidth(), [&] (float x, bool isBar, int barNumber)
    {
        if (isBar)
        {
            g.setColour (Theme::textDim);
            g.drawVerticalLine (roundToInt (x), (float) r.getY(), (float) getHeight());
            g.setColour (Theme::text);
            g.drawText (String (barNumber), Rectangle<float> (x + 3.0f, (float) r.getY(), 40.0f, (float) r.getHeight()),
                        Justification::centredLeft);
        }
        else
        {
            g.setColour (Theme::outline);
            g.drawVerticalLine (roundToInt (x), (float) getHeight() - 6.0f, (float) getHeight());
        }
    });

    // Marcador da posição atual
    auto px = viewState.timeToX (transport.getPosition());
    Path tri;
    tri.addTriangle (px - 6.0f, (float) getHeight() - 10.0f, px + 6.0f, (float) getHeight() - 10.0f, px, (float) getHeight());
    g.setColour (Theme::playhead);
    g.fillPath (tri);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void RulerComponent::mouseDown (const MouseEvent& e)
{
    auto& transport = viewState.edit.getTransport();
    draggingLoop = e.y < 12;

    if (draggingLoop)
    {
        loopAnchor = viewState.snap (viewState.xToTime ((float) e.x));
    }
    else
    {
        transport.setUserDragging (true);
        transport.setPosition (viewState.snap (viewState.xToTime ((float) e.x)));
    }
}

void RulerComponent::mouseDrag (const MouseEvent& e)
{
    auto& transport = viewState.edit.getTransport();
    auto t = viewState.snap (viewState.xToTime ((float) e.x));

    if (draggingLoop)
    {
        if (t != loopAnchor)
        {
            transport.setLoopRange ({ std::min (t, loopAnchor), std::max (t, loopAnchor) });
            transport.looping = true;
        }
    }
    else
    {
        transport.setPosition (t);
    }

    repaint();
}

void RulerComponent::mouseUp (const MouseEvent& e)
{
    auto& transport = viewState.edit.getTransport();

    if (draggingLoop && ! e.mouseWasDraggedSinceMouseDown())
        transport.looping = ! transport.looping.get();   // clique simples liga/desliga o cycle

    if (! draggingLoop)
        transport.setUserDragging (false);

    draggingLoop = false;
    repaint();
}

//==============================================================================
// PlayheadOverlay
//==============================================================================
PlayheadOverlay::PlayheadOverlay (ViewState& vs) : viewState (vs)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void PlayheadOverlay::paint (Graphics& g)
{
    auto x = viewState.timeToX (viewState.edit.getTransport().getPosition());
    g.setColour (viewState.edit.getTransport().isRecording() ? Theme::record : Theme::playhead);
    g.fillRect (Rectangle<float> (x - 0.5f, 0.0f, 1.5f, (float) getHeight()));
}

void PlayheadOverlay::timerCallback()
{
    auto x = viewState.timeToX (viewState.edit.getTransport().getPosition());

    if (std::abs (x - lastX) > 0.5f)
    {
        repaint (Rectangle<float> (lastX - 2.0f, 0.0f, 5.0f, (float) getHeight()).getSmallestIntegerContainer());
        repaint (Rectangle<float> (x - 2.0f, 0.0f, 5.0f, (float) getHeight()).getSmallestIntegerContainer());
        lastX = x;
    }
}

//==============================================================================
// ArrangeView
//==============================================================================
ArrangeView::ArrangeView (te::Edit& e, te::SelectionManager& sm)
    : edit (e), viewState (e, sm)
{
    addAndMakeVisible (ruler);
    addAndMakeVisible (headerArea);
    addAndMakeVisible (laneArea);
    addAndMakeVisible (playhead);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    viewState.addChangeListener (this);
    viewState.selection.addChangeListener (this);
    edit.state.addListener (this);

    buildTracks();
}

ArrangeView::~ArrangeView()
{
    edit.state.removeListener (this);
    viewState.selection.removeChangeListener (this);
    viewState.removeChangeListener (this);
}

void ArrangeView::paint (Graphics& g)
{
    g.fillAll (Theme::background);

    // Canto superior esquerdo (acima dos cabeçalhos)
    auto corner = Rectangle<int> (0, 0, headerWidth, rulerHeight);
    g.setColour (Theme::panel);
    g.fillRect (corner);
    g.setColour (Theme::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("FAIXAS", corner.reduced (12, 0), Justification::centredLeft);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) headerWidth);

    if (lanes.isEmpty())
    {
        g.setColour (Theme::textDim);
        g.setFont (FontOptions (15.0f));
        g.drawFittedText ("Crie uma faixa em  Faixa > Nova faixa de instrumento  (Ctrl/Cmd + T)\n"
                          "ou arraste arquivos de áudio/MIDI para cá."_t,
                          getLocalBounds().withTrimmedTop (rulerHeight + 40), Justification::centredTop, 3);
    }
}

void ArrangeView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (rulerHeight);
    top.removeFromLeft (headerWidth);
    ruler.setBounds (top.withTrimmedRight (scrollBarSize));

    auto bottom = r.removeFromBottom (scrollBarSize);
    bottom.removeFromLeft (headerWidth);
    hScroll.setBounds (bottom.withTrimmedRight (scrollBarSize));

    vScroll.setBounds (r.removeFromRight (scrollBarSize));
    headerArea.setBounds (r.removeFromLeft (headerWidth));
    laneArea.setBounds (r);
    playhead.setBounds (laneArea.getBounds().withTop (ruler.getY()));

    layoutTracks();
}

void ArrangeView::layoutTracks()
{
    int y = -viewState.scrollY;
    const int h = viewState.trackHeight;

    for (int i = 0; i < lanes.size(); ++i)
    {
        headers[i]->setBounds (0, y, headerArea.getWidth(), h);
        lanes[i]->setBounds (0, y, laneArea.getWidth(), h);
        y += h;
    }

    updateScrollBars();
}

double ArrangeView::getTotalSeconds() const
{
    return std::max (300.0, edit.getLength().inSeconds() + 60.0);
}

void ArrangeView::updateScrollBars()
{
    const auto visibleSeconds = getLanesWidth() / viewState.pixelsPerSecond;
    hScroll.setRangeLimits (0.0, std::max (getTotalSeconds(), viewState.viewStart + visibleSeconds));
    hScroll.setCurrentRange (viewState.viewStart, visibleSeconds, dontSendNotification);

    const auto contentH = (double) (lanes.size() * viewState.trackHeight + 40);
    vScroll.setRangeLimits (0.0, std::max (contentH, (double) laneArea.getHeight()));
    vScroll.setCurrentRange (viewState.scrollY, laneArea.getHeight(), dontSendNotification);
}

void ArrangeView::scrollBarMoved (ScrollBar* bar, double start)
{
    if (bar == &hScroll)
        viewState.setViewStart (start);
    else
        viewState.setScrollY (roundToInt (start));
}

void ArrangeView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto lanesX = (float) (e.getEventRelativeTo (&laneArea).x);

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Ctrl/Cmd + roda = zoom horizontal
        viewState.setZoom (viewState.pixelsPerSecond * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15), lanesX);
    }
    else if (e.mods.isAltDown())
    {
        // Alt + roda = altura das faixas
        viewState.trackHeight = jlimit (44, 240, viewState.trackHeight + (w.deltaY > 0 ? 8 : -8));
        viewState.sendChangeMessage();
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        auto delta = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        viewState.setViewStart (viewState.viewStart - delta * 400.0 / viewState.pixelsPerSecond);
    }
    else
    {
        const auto maxScroll = jmax (0, lanes.size() * viewState.trackHeight + 40 - laneArea.getHeight());
        viewState.setScrollY (jlimit (0, maxScroll, viewState.scrollY - roundToInt (w.deltaY * 300.0f)));
    }
}

void ArrangeView::mouseMagnify (const MouseEvent& e, float scale)
{
    viewState.setZoom (viewState.pixelsPerSecond * scale, (float) e.getEventRelativeTo (&laneArea).x);
}

void ArrangeView::followPlayhead()
{
    auto x = viewState.timeToX (edit.getTransport().getPosition());

    if (x > (float) getLanesWidth() * 0.9f || x < 0.0f)
        viewState.setViewStart (edit.getTransport().getPosition().inSeconds() - (getLanesWidth() * 0.1) / viewState.pixelsPerSecond);
}

void ArrangeView::importFiles (te::AudioTrack& track, const StringArray& files, te::TimePosition at)
{
    for (auto lane : lanes)
        if (&lane->getTrack() == &track)
            return lane->filesDropped (files, roundToInt (viewState.timeToX (at)), 0);
}

te::AudioTrack* ArrangeView::getTrackAtScreenPosition (Point<int> screenPos)
{
    auto local = laneArea.getLocalPoint (nullptr, screenPos);

    for (auto lane : lanes)
        if (lane->getBounds().contains (local))
            return &lane->getTrack();

    return nullptr;
}

void ArrangeView::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &viewState)
    {
        layoutTracks();

        for (auto l : lanes)
        {
            l->layoutClips();
            l->repaint();
        }

        ruler.repaint();
        playhead.repaint();
    }
    else
    {
        // Seleção mudou: redesenha destaques
        for (auto h : headers) h->repaint();
        for (auto l : lanes)
        {
            l->repaint();
            for (int i = 0; i < l->getNumChildComponents(); ++i)
                l->getChildComponent (i)->repaint();
        }
    }
}

void ArrangeView::valueTreeChildAdded (ValueTree&, ValueTree& c)
{
    if (te::TrackList::isTrack (c))
        triggerAsyncUpdate();
}

void ArrangeView::valueTreeChildRemoved (ValueTree&, ValueTree& c, int)
{
    if (te::TrackList::isTrack (c))
        triggerAsyncUpdate();
}

void ArrangeView::valueTreeChildOrderChanged (ValueTree& parent, int a, int b)
{
    if (te::TrackList::isTrack (parent.getChild (a)) || te::TrackList::isTrack (parent.getChild (b)))
        triggerAsyncUpdate();
}

void ArrangeView::handleAsyncUpdate()
{
    buildTracks();
}

void ArrangeView::buildTracks()
{
    headers.clear();
    lanes.clear();

    for (auto t : ZS::getUserTracks (edit))
    {
        te::AudioTrack::Ptr track (t);

        auto h = headers.add (new TrackHeaderComponent (viewState, track));
        headerArea.addAndMakeVisible (h);

        auto l = lanes.add (new TrackLaneComponent (viewState, track, *this));
        laneArea.addAndMakeVisible (l);
    }

    playhead.toFront (false);
    resized();
    repaint();
}
