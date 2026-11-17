#pragma once

#include "Helpers.h"
#include "Score/ScoreModel.h"

class ScoreRenderThread;

//==============================================================================
/** Painel "Limpeza do MIDI": todos os controles de interpretação da partitura. */
class CleanupPanel : public juce::Component
{
public:
    CleanupPanel();

    void setSettings (const score::Settings&, double bpm);
    std::function<void (const score::Settings&)> onChange;

    void resized() override;
    void paint (juce::Graphics&) override;
    int getIdealHeight() const      { return idealHeight; }

private:
    struct Row
    {
        juce::String section;           // título de seção (linha só de título)
        juce::String label;
        std::unique_ptr<juce::Component> editor;
        std::function<void()> refresh;
    };

    void addSection (const juce::String&);
    void addChoice (const juce::String& label, juce::StringArray items,
                    std::function<int (const score::Settings&)> get,
                    std::function<void (score::Settings&, int)> set);
    void addSlider (const juce::String& label, double min, double max, double step,
                    std::function<juce::String (double)> format,
                    std::function<double (const score::Settings&)> get,
                    std::function<void (score::Settings&, double)> set);
    void addToggle (const juce::String& label,
                    std::function<bool (const score::Settings&)> get,
                    std::function<void (score::Settings&, bool)> set);

    void changed()      { if (onChange) onChange (settings); }

    score::Settings settings;
    double bpm = 120.0;
    std::vector<Row> rows;
    int idealHeight = 600;
    bool updating = false;
};

//==============================================================================
/** Texto da partitura (cifras, nomes, andamento) — desenhado à parte, pois o leitor de SVG só desenha formas. */
struct ScoreText
{
    juce::Point<float> position;
    float size = 12.0f;
    juce::String text;
    juce::Justification justification = juce::Justification::left;
    bool bold = false, italic = false;
};

/** Folha com a partitura desenhada. */
class ScorePage : public juce::Component
{
public:
    void setDrawable (std::unique_ptr<juce::Drawable> d, std::vector<ScoreText> texts, int width, int height);
    void paint (juce::Graphics&) override;
    juce::String message = "Gerando partitura...";

private:
    std::unique_ptr<juce::Drawable> drawable;
    std::vector<ScoreText> texts;
};

//==============================================================================
/** Aba Partitura: MIDI humano -> partitura limpa (sem alterar o MIDI). */
class ScoreView : public juce::Component,
                  private te::ValueTreeAllEventListener,
                  private juce::Timer
{
public:
    explicit ScoreView (te::Engine&);
    ~ScoreView() override;

    void setEdit (te::Edit*);
    void setSelectedTrack (te::AudioTrack*);

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override     { if (isVisible()) requestUpdate (true); }

    void exportMusicXML();

private:
    void valueTreeChanged() override      { requestUpdate (false); }
    void timerCallback() override;

    void requestUpdate (bool immediate);
    void startRender();
    void loadSettingsIntoPanel();
    void applySettings (const score::Settings&);
    juce::Array<te::AudioTrack*> getTracksToShow() const;
    std::vector<score::InputTrack> gatherTracks (std::vector<score::Settings>&) const;
    juce::String getResourcePath();

    te::Engine& engine;
    te::Edit* edit = nullptr;
    te::AudioTrack::Ptr selectedTrack;

    CleanupPanel panel;
    juce::Viewport panelViewport;
    juce::Label title;
    juce::ComboBox presets, scope, which;
    juce::TextButton resetButton { "Restaurar" }, exportButton { "Exportar MusicXML..." };
    juce::Slider zoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label status;

    ScorePage page;
    juce::Viewport pageViewport;

    std::unique_ptr<ScoreRenderThread> renderThread;
    std::string lastMusicXml;
    std::shared_ptr<juce::FileChooser> chooser;
    int lastWidth = 0;

    friend class ScoreRenderThread;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreView)
};
