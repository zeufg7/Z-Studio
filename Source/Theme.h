#pragma once

#include <JuceHeader.h>

//==============================================================================
// Paleta e LookAndFeel do Zeufg Studio (tema escuro, inspirado no Logic/Cubase)
namespace Theme
{
    inline const juce::Colour background   { 0xff1b1c1f };
    inline const juce::Colour panel        { 0xff232428 };
    inline const juce::Colour panelLight   { 0xff2c2e33 };
    inline const juce::Colour lane         { 0xff26282c };
    inline const juce::Colour laneAlt      { 0xff222327 };
    inline const juce::Colour outline      { 0xff3a3d44 };
    inline const juce::Colour gridBar      { 0xff41444c };
    inline const juce::Colour gridBeat     { 0xff2f3237 };
    inline const juce::Colour text         { 0xffe8e9ec };
    inline const juce::Colour textDim      { 0xff9aa0aa };
    inline const juce::Colour accent       { 0xffe0a458 };   // âmbar
    inline const juce::Colour playhead     { 0xfff2f2f2 };
    inline const juce::Colour record       { 0xffe5484d };
    inline const juce::Colour mute         { 0xff4f8ff7 };
    inline const juce::Colour solo         { 0xfff5c542 };
    inline const juce::Colour loopRegion   { 0xff3fb6a8 };
    inline const juce::Colour lcd          { 0xff101113 };

    // Cores de faixa (ciclam conforme novas faixas são criadas)
    inline juce::Colour trackColour (int index)
    {
        static const juce::uint32 colours[] = { 0xff4f8ff7, 0xff3fb6a8, 0xffe0a458, 0xffd36ba6,
                                                0xff8e7cf0, 0xff6cc36c, 0xffe5774d, 0xff46b3d9 };
        return juce::Colour (colours[(size_t) index % std::size (colours)]);
    }

    //==============================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
            : juce::LookAndFeel_V4 ({ background.getARGB(),   // windowBackground
                                      panel.getARGB(),        // widgetBackground
                                      panelLight.getARGB(),   // menuBackground
                                      outline.getARGB(),      // outline
                                      text.getARGB(),         // defaultText
                                      panelLight.getARGB(),   // defaultFill
                                      0xff111111,             // highlightedText
                                      accent.getARGB(),       // highlightedFill
                                      text.getARGB() })       // menuText
        {
            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::DocumentWindow::backgroundColourId, background);
            setColour (juce::TextButton::buttonColourId, panelLight);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111111));
            setColour (juce::Slider::thumbColourId, accent);
            setColour (juce::Slider::trackColourId, accent.withAlpha (0.7f));
            setColour (juce::Slider::rotarySliderFillColourId, accent);
            setColour (juce::Slider::rotarySliderOutlineColourId, outline);
            setColour (juce::Slider::backgroundColourId, outline);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Label::textColourId, text);
            setColour (juce::PopupMenu::backgroundColourId, panelLight);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff111111));
            setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.2f));
            setColour (juce::ComboBox::backgroundColourId, panelLight);
            setColour (juce::ComboBox::outlineColourId, outline);
            setColour (juce::TextEditor::backgroundColourId, lcd);
            setColour (juce::TextEditor::outlineColourId, outline);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::ListBox::backgroundColourId, panel);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                   bool isHighlighted, bool isDown) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = backgroundColour;

            if (isDown)             c = c.darker (0.25f);
            else if (isHighlighted) c = c.brighter (0.12f);

            g.setColour (c);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (outline);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f));
        }
    };
}
