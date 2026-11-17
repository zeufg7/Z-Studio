#pragma once

#include "Helpers.h"

//==============================================================================
/** Botão de um slot de plugin (instrumento ou insert). Clique abre a janela do plugin. */
class PluginSlotButton : public juce::TextButton
{
public:
    PluginSlotButton (te::Plugin::Ptr p) : plugin (p)
    {
        setButtonText (plugin->getName());
        setTooltip ("Clique: abrir  |  Botão direito: opções"_t);
        updateColour();
    }

    void clicked (const juce::ModifierKeys& mods) override
    {
        if (mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("Abrir", [p = plugin] { p->showWindowExplicitly(); });
            m.addItem (plugin->isEnabled() ? "Desativar (bypass)" : "Ativar", [this]
            {
                plugin->setEnabled (! plugin->isEnabled());
                updateColour();
            });
            m.addSeparator();
            m.addItem ("Remover", [p = plugin] { p->deleteFromParent(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
        else
        {
            plugin->showWindowExplicitly();
        }
    }

private:
    void updateColour()
    {
        setColour (juce::TextButton::buttonColourId, plugin->isEnabled() ? Theme::panelLight.brighter (0.08f)
                                                                          : Theme::panel);
        setColour (juce::TextButton::textColourOffId, plugin->isEnabled() ? Theme::text : Theme::textDim);
    }

    te::Plugin::Ptr plugin;
};

//==============================================================================
/** Inspetor (como no Logic/Cubase): canal da faixa selecionada. */
class Inspector : public juce::Component,
                  private te::ValueTreeAllEventListener,
                  private juce::AsyncUpdater,
                  private juce::Timer
{
public:
    Inspector()
    {
        trackName.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        trackName.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (trackName);

        addInstrumentButton.onClick = [this] { chooseInstrument(); };
        addInsertButton.onClick     = [this] { chooseInsert(); };
        addAndMakeVisible (addInstrumentButton);
        addAndMakeVisible (addInsertButton);

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setTextValueSuffix (" dB");
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.onValueChange = [this]
        {
            if (auto vp = getVolume())
                vp->setVolumeDb ((float) volume.getValue());
        };
        addAndMakeVisible (volume);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.textFromValueFunction = [] (double v)
        {
            auto pct = juce::roundToInt (std::abs (v) * 100.0);
            return pct == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (pct);
        };
        pan.onValueChange = [this]
        {
            if (auto vp = getVolume())
                vp->setPan ((float) pan.getValue());
        };
        pan.updateText();
        addAndMakeVisible (pan);

        masterVolume.setSliderStyle (juce::Slider::LinearHorizontal);
        masterVolume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        masterVolume.setRange (-60.0, 6.0, 0.1);
        masterVolume.setSkewFactorFromMidPoint (-12.0);
        masterVolume.setTextValueSuffix (" dB");
        masterVolume.setDoubleClickReturnValue (true, 0.0);
        masterVolume.onValueChange = [this]
        {
            if (edit != nullptr)
                if (auto mv = edit->getMasterVolumePlugin())
                    mv->setVolumeDb ((float) masterVolume.getValue());
        };
        addAndMakeVisible (masterVolume);

        startTimerHz (10);
        setTrack (nullptr, nullptr);
    }

    ~Inspector() override
    {
        setTrack (nullptr, nullptr);
    }

    void setTrack (te::Edit* e, te::AudioTrack* t)
    {
        if (track != nullptr)
            track->state.removeListener (this);

        edit = e;
        track = t;

        if (track != nullptr)
            track->state.addListener (this);

        if (edit != nullptr)
            if (auto mv = edit->getMasterVolumePlugin())
                masterVolume.setValue (mv->getVolumeDb(), juce::dontSendNotification);

        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::outline);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

        for (auto& [y, text] : sectionTitles)
            g.drawText (text, 12, y, getWidth() - 24, 16, juce::Justification::centredLeft);

        if (track != nullptr)
        {
            g.setColour (track->getColour());
            g.fillRect (0, 0, getWidth() - 1, 4);
        }
        else
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Selecione uma faixa", getLocalBounds().reduced (12).withTrimmedTop (30),
                              juce::Justification::centredTop, 2);
        }
    }

    void resized() override
    {
        sectionTitles.clear();
        auto r = getLocalBounds().reduced (12, 10);

        trackName.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);

        if (track != nullptr && ZS::isInstrumentTrack (*track))
        {
            sectionTitles.push_back ({ r.getY(), "INSTRUMENTO" });
            r.removeFromTop (18);

            if (instrumentSlot != nullptr)
                instrumentSlot->setBounds (r.removeFromTop (28));

            r.removeFromTop (4);
            addInstrumentButton.setBounds (r.removeFromTop (24));
            r.removeFromTop (12);
        }

        sectionTitles.push_back ({ r.getY(), "INSERTS (EFEITOS)" });
        r.removeFromTop (18);

        for (auto s : insertSlots)
        {
            s->setBounds (r.removeFromTop (26));
            r.removeFromTop (3);
        }

        addInsertButton.setBounds (r.removeFromTop (24));
        r.removeFromTop (14);

        // Master no rodapé
        auto masterArea = r.removeFromBottom (46);
        sectionTitles.push_back ({ masterArea.getY(), "MASTER" });
        masterVolume.setBounds (masterArea.withTrimmedTop (18));

        r.removeFromBottom (10);
        sectionTitles.push_back ({ r.getY(), "CANAL" });
        r.removeFromTop (18);

        auto channel = r;
        pan.setBounds (channel.removeFromTop (78).withSizeKeepingCentre (70, 78));
        channel.removeFromTop (6);
        volume.setBounds (channel.withSizeKeepingCentre (70, channel.getHeight()));

        repaint();
    }

    std::function<void()> onPluginsChanged;

private:
    te::VolumeAndPanPlugin* getVolume() const   { return track != nullptr ? track->getVolumePlugin() : nullptr; }

    void valueTreeChanged() override {}

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override
    {
        if (c.hasType (te::IDs::PLUGIN)) triggerAsyncUpdate();
    }

    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override
    {
        if (c.hasType (te::IDs::PLUGIN)) triggerAsyncUpdate();
    }

    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override   { triggerAsyncUpdate(); }

    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
    {
        if (track != nullptr && v == track->state && id == te::IDs::name)
            trackName.setText (track->getName(), juce::dontSendNotification);
    }

    void handleAsyncUpdate() override   { rebuild(); }

    void timerCallback() override
    {
        if (auto vp = getVolume())
        {
            if (! volume.isMouseButtonDown() && std::abs (vp->getVolumeDb() - (float) volume.getValue()) > 0.05f)
                volume.setValue (vp->getVolumeDb(), juce::dontSendNotification);

            if (! pan.isMouseButtonDown() && std::abs (vp->getPan() - (float) pan.getValue()) > 0.005f)
                pan.setValue (vp->getPan(), juce::dontSendNotification);
        }
    }

    void rebuild()
    {
        instrumentSlot.reset();
        insertSlots.clear();

        const bool hasTrack = track != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &volume, &pan, &addInsertButton, &trackName })
            c->setVisible (hasTrack);

        addInstrumentButton.setVisible (hasTrack && ZS::isInstrumentTrack (*track));
        masterVolume.setVisible (edit != nullptr);

        if (hasTrack)
        {
            trackName.setText (track->getName(), juce::dontSendNotification);

            if (auto inst = ZS::getInstrument (*track))
            {
                instrumentSlot = std::make_unique<PluginSlotButton> (inst);
                instrumentSlot->setColour (juce::TextButton::buttonColourId, track->getColour().withAlpha (0.35f));
                addAndMakeVisible (*instrumentSlot);
            }

            addInstrumentButton.setButtonText (instrumentSlot != nullptr ? "Trocar instrumento..." : "+ Escolher instrumento");

            for (auto p : ZS::getInserts (*track))
                addAndMakeVisible (insertSlots.add (new PluginSlotButton (p)));

            if (auto vp = getVolume())
            {
                volume.setValue (vp->getVolumeDb(), juce::dontSendNotification);
                pan.setValue (vp->getPan(), juce::dontSendNotification);
            }
        }

        ZS::disableFocus (*this);
        resized();

        if (onPluginsChanged)
            onPluginsChanged();
    }

    void chooseInstrument()
    {
        if (track == nullptr)
            return;

        te::AudioTrack::Ptr t (track);
        PluginMenus::show (track->edit, true, &addInstrumentButton, [t] (te::Plugin::Ptr p)
        {
            ZS::setInstrument (*t, p);
            p->showWindowExplicitly();
        });
    }

    void chooseInsert()
    {
        if (track == nullptr)
            return;

        te::AudioTrack::Ptr t (track);
        PluginMenus::show (track->edit, false, &addInsertButton, [t] (te::Plugin::Ptr p)
        {
            t->pluginList.insertPlugin (p, ZS::getInsertIndex (*t), nullptr);
            p->showWindowExplicitly();
        });
    }

    te::Edit* edit = nullptr;
    te::AudioTrack::Ptr track;

    juce::Label trackName;
    std::unique_ptr<PluginSlotButton> instrumentSlot;
    juce::OwnedArray<PluginSlotButton> insertSlots;
    juce::TextButton addInstrumentButton { "+ Escolher instrumento" }, addInsertButton { "+ Adicionar efeito" };
    juce::Slider volume, pan, masterVolume;
    std::vector<std::pair<int, juce::String>> sectionTitles;
};
