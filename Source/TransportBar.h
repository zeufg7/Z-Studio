#pragma once

#include "Helpers.h"

//==============================================================================
/** Visor central (estilo LCD do Logic): compassos/tempo, BPM e fórmula de compasso. */
class PositionDisplay : public juce::Component,
                        private juce::Timer
{
public:
    PositionDisplay()                       { startTimerHz (25); }

    void setEdit (te::Edit* e)              { edit = e; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::lcd);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        if (edit == nullptr)
            return;

        auto pos = edit->getTransport().getPosition();
        auto area = getLocalBounds().reduced (12, 4);

        g.setColour (Theme::accent);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
        g.drawText (ZS::formatBarsBeats (*edit, pos), area.removeFromTop (area.getHeight() * 3 / 5),
                    juce::Justification::centred);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        g.drawText (ZS::formatTime (pos), area, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick) onClick();
    }

    std::function<void()> onClick;

private:
    void timerCallback() override
    {
        if (edit == nullptr)
            return;

        auto p = edit->getTransport().getPosition();
        if (p != lastPos)
        {
            lastPos = p;
            repaint();
        }
    }

    te::Edit* edit = nullptr;
    te::TimePosition lastPos;
};

//==============================================================================
/** Botão de transporte com ícone desenhado (não depende de fontes com símbolos). */
class TransportButton : public juce::Button
{
public:
    enum class Icon { toStart, stop, play, record, loop };

    TransportButton (Icon i) : juce::Button ({}), icon (i) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto on = getToggleState();
        auto bg = on ? findColour (juce::TextButton::buttonOnColourId) : Theme::panelLight;
        if (down) bg = bg.darker (0.2f); else if (over) bg = bg.brighter (0.1f);

        g.setColour (bg);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        auto c = on ? juce::Colour (0xff111111) : (icon == Icon::record ? Theme::record : Theme::text);
        g.setColour (c);

        auto a = r.withSizeKeepingCentre (14.0f, 14.0f);
        juce::Path p;

        switch (icon)
        {
            case Icon::toStart:
                p.addRectangle (a.getX(), a.getY(), 2.5f, a.getHeight());
                p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX() + 3.0f, a.getCentreY());
                break;
            case Icon::stop:
                p.addRectangle (a.reduced (1.0f));
                break;
            case Icon::play:
                p.addTriangle (a.getX() + 1.0f, a.getY(), a.getX() + 1.0f, a.getBottom(), a.getRight(), a.getCentreY());
                break;
            case Icon::record:
                p.addEllipse (a.reduced (0.5f));
                break;
            case Icon::loop:
            {
                juce::Path arc;
                arc.addCentredArc (a.getCentreX(), a.getCentreY(), 6.0f, 6.0f, 0.0f, 0.6f, 5.6f, true);
                g.strokePath (arc, juce::PathStrokeType (2.0f));
                auto end = arc.getCurrentPosition();
                p.addTriangle (end.x - 4.0f, end.y - 1.0f, end.x + 3.0f, end.y - 3.0f, end.x + 1.0f, end.y + 4.0f);
                break;
            }
        }

        g.fillPath (p);
    }

private:
    Icon icon;
};

//==============================================================================
class TransportBar : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    TransportBar()
    {
        for (auto* b : std::initializer_list<juce::Button*> { &startButton, &stopButton, &playButton, &recordButton,
                                                              &loopButton, &clickButton, &snapButton })
            addAndMakeVisible (b);

        startButton.setTooltip ("Voltar ao início (Enter)"_t);
        stopButton.setTooltip ("Parar (Espaço)"_t);
        playButton.setTooltip ("Tocar / Parar (Espaço)"_t);
        recordButton.setTooltip ("Gravar (R)");
        loopButton.setTooltip ("Cycle / Loop (C)");
        clickButton.setTooltip ("Metrônomo (K)"_t);
        snapButton.setTooltip ("Encaixar na grade");

        recordButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        loopButton.setColour (juce::TextButton::buttonOnColourId, Theme::loopRegion);
        playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff47c75e));

        startButton.onClick  = [this] { if (edit) edit->getTransport().setPosition (0s); };
        stopButton.onClick   = [this] { if (edit) stop(); };
        playButton.onClick   = [this] { if (edit) togglePlay(); };
        recordButton.onClick = [this] { if (edit) toggleRecord(); };
        loopButton.onClick   = [this] { if (edit) toggleLoop(); };
        clickButton.onClick  = [this] { if (edit) { edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); updateButtons(); } };
        snapButton.onClick   = [this] { if (onSnapToggled) onSnapToggled(); };

        addAndMakeVisible (display);

        bpm.setSliderStyle (juce::Slider::IncDecButtons);
        bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 26);
        bpm.setRange (30.0, 300.0, 0.01);
        bpm.setNumDecimalPlacesToDisplay (2);
        bpm.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        bpm.setTextValueSuffix (" bpm");
        bpm.onValueChange = [this]
        {
            if (edit != nullptr)
                if (auto t = edit->tempoSequence.getTempo (0))
                    if (std::abs (t->getBpm() - bpm.getValue()) > 0.001)
                        t->setBpm (bpm.getValue());
        };
        addAndMakeVisible (bpm);

        timeSig.addItemList ({ "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8" }, 1);
        timeSig.onChange = [this]
        {
            if (edit != nullptr)
                if (auto ts = edit->tempoSequence.getTimeSig (0))
                    if (ts->getStringTimeSig() != timeSig.getText())
                        ts->setStringTimeSig (timeSig.getText());
        };
        addAndMakeVisible (timeSig);

        startTimerHz (5);
    }

    ~TransportBar() override
    {
        setEdit (nullptr);
    }

    void setEdit (te::Edit* e)
    {
        if (edit != nullptr)
            edit->getTransport().removeChangeListener (this);

        edit = e;
        display.setEdit (e);

        if (edit != nullptr)
            edit->getTransport().addChangeListener (this);

        updateButtons();
    }

    void togglePlay()
    {
        auto& t = edit->getTransport();
        if (t.isPlaying()) t.stop (false, false);
        else               t.play (false);
    }

    void stop()
    {
        auto& t = edit->getTransport();
        if (t.isPlaying()) t.stop (false, false);
        else               t.setPosition (0s);   // segundo "stop" volta ao início (como no Logic)
    }

    void toggleRecord()
    {
        auto& t = edit->getTransport();
        if (t.isRecording()) t.stop (false, false);
        else                 t.record (false);
    }

    void toggleLoop()
    {
        auto& t = edit->getTransport();

        if (t.getLoopRange().getLength() <= 0s)
        {
            // Sem região definida: cria 4 compassos a partir da posição atual
            auto& ts = edit->tempoSequence;
            auto bars = ts.toBarsAndBeats (t.getPosition()).bars;
            t.setLoopRange ({ ts.toTime (te::tempo::BarsAndBeats { bars }), ts.toTime (te::tempo::BarsAndBeats { bars + 4 }) });
        }

        t.looping = ! t.looping.get();
        updateButtons();
    }

    void setSnapState (bool on)     { snapButton.setToggleState (on, juce::dontSendNotification); }

    std::function<void()> onSnapToggled;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        g.setColour (Theme::accent);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("Z-STUDIO", getLocalBounds().removeFromRight (150).reduced (12, 0), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        const int bw = 44;

        auto left = r.removeFromLeft (bw * 5 + 20);
        startButton.setBounds (left.removeFromLeft (bw));  left.removeFromLeft (4);
        stopButton.setBounds (left.removeFromLeft (bw));   left.removeFromLeft (4);
        playButton.setBounds (left.removeFromLeft (bw));   left.removeFromLeft (4);
        recordButton.setBounds (left.removeFromLeft (bw)); left.removeFromLeft (4);
        loopButton.setBounds (left.removeFromLeft (bw));

        r.removeFromLeft (14);
        display.setBounds (r.removeFromLeft (210));
        r.removeFromLeft (14);

        auto tempoArea = r.removeFromLeft (130);
        bpm.setBounds (tempoArea.withSizeKeepingCentre (130, 30));
        r.removeFromLeft (8);
        timeSig.setBounds (r.removeFromLeft (76).withSizeKeepingCentre (76, 28));
        r.removeFromLeft (14);
        clickButton.setBounds (r.removeFromLeft (54).withSizeKeepingCentre (54, 30));
        r.removeFromLeft (4);
        snapButton.setBounds (r.removeFromLeft (64).withSizeKeepingCentre (64, 30));
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { updateButtons(); }
    void timerCallback() override                                      { updateButtons(); }

    void updateButtons()
    {
        if (edit == nullptr)
            return;

        auto& t = edit->getTransport();
        playButton.setToggleState (t.isPlaying(), juce::dontSendNotification);
        recordButton.setToggleState (t.isRecording(), juce::dontSendNotification);
        loopButton.setToggleState (t.looping, juce::dontSendNotification);
        clickButton.setToggleState (edit->clickTrackEnabled, juce::dontSendNotification);

        if (auto tempo = edit->tempoSequence.getTempo (0))
            if (! bpm.isMouseButtonDown() && std::abs (bpm.getValue() - tempo->getBpm()) > 0.001)
                bpm.setValue (tempo->getBpm(), juce::dontSendNotification);

        if (auto ts = edit->tempoSequence.getTimeSig (0))
            if (timeSig.getText() != ts->getStringTimeSig())
                timeSig.setText (ts->getStringTimeSig(), juce::dontSendNotification);
    }

    te::Edit* edit = nullptr;

    TransportButton startButton  { TransportButton::Icon::toStart },
                    stopButton   { TransportButton::Icon::stop },
                    playButton   { TransportButton::Icon::play },
                    recordButton { TransportButton::Icon::record },
                    loopButton   { TransportButton::Icon::loop };
    juce::TextButton clickButton  { "Click" },
                     snapButton   { "Grade" };

    PositionDisplay display;
    juce::Slider bpm;
    juce::ComboBox timeSig;
};
