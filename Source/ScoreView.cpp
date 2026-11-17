#include "ScoreView.h"
#include "Score/ScoreRenderer.h"
#include "Score/ScoreSettingsTree.h"
#include "ZeufgBinaryData.h"

using namespace juce;

//==============================================================================
// Opções dos controles
namespace
{
    const int gridValues[] = { 96, 48, 24, 12, 6 };
    const StringArray gridLabels { "1/4  (seminima)", "1/8  (colcheia)", "1/16  (semicolcheia)", "1/32  (fusa)", "1/64  (semifusa)" };

    int indexOf (const int* values, int count, int v, int fallback)
    {
        for (int i = 0; i < count; ++i)
            if (values[i] == v)
                return i;
        return fallback;
    }

    String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return String (names[midi % 12]) + String (midi / 12 - 1);
    }

    // Tonalidades: armadura -7..7, maior e menor
    String keyName (int fifths, bool minor)
    {
        static const char* major[] = { "Dob", "Solb", "Reb", "Lab", "Mib", "Sib", "Fa", "Do", "Sol", "Re", "La", "Mi", "Si", "Fa#", "Do#" };
        static const char* minr[]  = { "Lab", "Mib", "Sib", "Fa", "Do", "Sol", "Re", "La", "Mi", "Si", "Fa#", "Do#", "Sol#", "Re#", "La#" };
        return String::fromUTF8 ((minor ? minr : major)[fifths + 7]) + (minor ? " menor" : " maior");
    }
}

//==============================================================================
// Extrai os textos do SVG do Verovio (coordenadas já convertidas para pixels)
namespace
{
    String mapSmuflChars (const String& t)
    {
        String out;
        for (auto c : t)
        {
            switch ((int) c)
            {
                case 0xE260: out << String::charToString ((juce_wchar) 0x266D); break;   // bemol
                case 0xE261: out << String::charToString ((juce_wchar) 0x266E); break;   // bequadro
                case 0xE262: out << String::charToString ((juce_wchar) 0x266F); break;   // sustenido
                case 0xE263: out << "x"; break;
                case 0xECA5: out << String::charToString ((juce_wchar) 0x2669); break;   // semínima (andamento)
                case 0xECA7: out << String::charToString ((juce_wchar) 0x266A); break;   // colcheia
                case 0xECB7: out << "."; break;
                case 0xE264: out << String::charToString ((juce_wchar) 0x266D) << String::charToString ((juce_wchar) 0x266D); break;
                default:
                    if ((int) c >= 0xE000 && (int) c <= 0xF8FF) break;   // outros símbolos SMuFL
                    out << String::charToString (c);
            }
        }
        return out;
    }

    float parseSize (const String& s)    { return s.trim().upToFirstOccurrenceOf ("px", false, true).getFloatValue(); }

    void collectText (const XmlElement& e, String& text, float& size, bool& bold, bool& italic)
    {
        if (e.isTextElement())
        {
            text << e.getText();
            return;
        }

        if (e.hasAttribute ("font-size") && size <= 0.0f)
            size = parseSize (e.getStringAttribute ("font-size"));

        if (e.getStringAttribute ("font-weight") == "bold")   bold = true;
        if (e.getStringAttribute ("font-style") == "italic")  italic = true;

        for (auto* c : e.getChildIterator())
            collectText (*c, text, size, bold, italic);
    }

    void walkSvg (const XmlElement& e, Point<float> offset, float scale, std::vector<ScoreText>& out)
    {
        auto t = e.getStringAttribute ("transform");
        if (t.startsWith ("translate("))
        {
            auto args = StringArray::fromTokens (t.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false), ", ", "");
            args.removeEmptyStrings();
            if (args.size() >= 1) offset.x += args[0].getFloatValue();
            if (args.size() >= 2) offset.y += args[1].getFloatValue();
        }

        if (e.hasTagName ("text"))
        {
            ScoreText st;
            float size = 0.0f;
            String text;

            for (auto* c : e.getChildIterator())
                collectText (*c, text, size, st.bold, st.italic);

            text = mapSmuflChars (text).trim();

            if (text.isNotEmpty() && size > 0.0f)
            {
                st.text = text;
                st.size = size * scale;
                st.position = { (offset.x + (float) e.getDoubleAttribute ("x")) * scale,
                                (offset.y + (float) e.getDoubleAttribute ("y")) * scale };

                auto anchor = e.getStringAttribute ("text-anchor");
                st.justification = anchor == "end" ? Justification::right
                                 : anchor == "middle" ? Justification::horizontallyCentred : Justification::left;
                out.push_back (st);
            }
            return;
        }

        for (auto* c : e.getChildIterator())
            walkSvg (*c, offset, scale, out);
    }

    std::vector<ScoreText> extractTexts (const std::string& svg)
    {
        std::vector<ScoreText> out;
        auto xml = XmlDocument::parse (String::fromUTF8 (svg.c_str()));
        if (xml == nullptr)
            return out;

        const float outerWidth = parseSize (xml->getStringAttribute ("width"));

        for (auto* inner : xml->getChildWithTagNameIterator ("svg"))
        {
            auto vb = StringArray::fromTokens (inner->getStringAttribute ("viewBox"), " ", "");
            if (vb.size() < 4 || vb[2].getFloatValue() <= 0.0f)
                continue;

            const float scale = outerWidth / vb[2].getFloatValue();
            walkSvg (*inner, {}, scale, out);
        }

        return out;
    }
}

//==============================================================================
// Thread de desenho (o motor e o Verovio rodam fora da interface)
//==============================================================================
class ScoreRenderThread : public Thread
{
public:
    struct Request
    {
        std::vector<score::InputTrack> tracks;
        std::vector<score::Settings> settings;
        int beats = 4, beatType = 4;
        double bpm = 120.0;
        std::string title;
        int width = 1000;
        int scale = 40;
    };

    ScoreRenderThread (ScoreView& v, String resources)
        : Thread ("Partitura"), view (v), resourcePath (std::move (resources))
    {
        startThread();
    }

    ~ScoreRenderThread() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (4000);
    }

    void post (Request r)
    {
        {
            const ScopedLock sl (lock);
            pending = std::make_unique<Request> (std::move (r));
        }
        notify();
    }

    void run() override
    {
        score::Renderer renderer (resourcePath.toStdString());

        while (! threadShouldExit())
        {
            std::unique_ptr<Request> req;
            {
                const ScopedLock sl (lock);
                req = std::move (pending);
            }

            if (req == nullptr)
            {
                wait (-1);
                continue;
            }

            auto sc = score::buildScore (req->tracks, req->settings, req->beats, req->beatType, req->bpm, req->title);
            auto xml = score::toMusicXML (sc);

            std::unique_ptr<Drawable> drawable;
            std::vector<ScoreText> texts;
            int w = 0, h = 0;

            if (renderer.isReady() && ! sc.parts.empty())
            {
                auto svg = renderer.render (xml, req->width, req->scale);
                if (! svg.empty())
                {
                    drawable = Drawable::createFromSVGString (String::fromUTF8 (svg.c_str()));
                    texts = extractTexts (svg);
                    if (drawable != nullptr)
                    {
                        auto b = drawable->getDrawableBounds();
                        w = (int) std::ceil (b.getRight());
                        h = (int) std::ceil (b.getBottom());
                    }
                }
            }

            // resumo para a barra de status
            String info;
            if (sc.parts.empty())
            {
                info = "Nenhuma nota MIDI nas faixas escolhidas";
            }
            else
            {
                auto& p = sc.parts.front();
                info << keyName (p.keyFifths, p.minor) << "  |  " << (int) p.measures.size() << " compassos";
                if (sc.parts.size() > 1) info << "  |  " << (int) sc.parts.size() << " faixas";
                else if (p.staves == 2) info << "  |  piano (2 pautas)";
            }

            if (threadShouldExit())
                break;

            auto shared = std::make_shared<std::unique_ptr<Drawable>> (std::move (drawable));
            auto sharedTexts = std::make_shared<std::vector<ScoreText>> (std::move (texts));
            MessageManager::callAsync ([sp = Component::SafePointer<ScoreView> (&view), shared, sharedTexts, xml, info, w, h,
                                        ready = renderer.isReady()]
            {
                if (sp == nullptr)
                    return;

                sp->lastMusicXml = xml;
                sp->status.setText (ready ? info : String ("Fontes de partitura indisponiveis"), dontSendNotification);
                sp->page.message = info;
                sp->page.setDrawable (std::move (*shared), std::move (*sharedTexts), w, h);
            });
        }
    }

private:
    ScoreView& view;
    String resourcePath;
    CriticalSection lock;
    std::unique_ptr<Request> pending;
};

//==============================================================================
// CleanupPanel
//==============================================================================
CleanupPanel::CleanupPanel()
{
    using S = score::Settings;

    addSection ("RITMO");
    addChoice ("Grade dos ataques", gridLabels,
               [] (const S& s) { return indexOf (gridValues, 5, s.startGrid, 2); },
               [] (S& s, int i) { s.startGrid = gridValues[i]; });
    StringArray lengthLabels { "Igual aos ataques" };
    lengthLabels.addArray (gridLabels);
    addChoice ("Grade das duracoes", lengthLabels,
               [] (const S& s) { return s.lengthGrid == 0 ? 0 : 1 + indexOf (gridValues, 5, s.lengthGrid, 2); },
               [] (S& s, int i) { s.lengthGrid = i == 0 ? 0 : gridValues[i - 1]; });
    addToggle ("Detectar tercinas",
               [] (const S& s) { return s.autoTuplets; },
               [] (S& s, bool b) { s.autoTuplets = b; });
    addSlider ("Sensibilidade das tercinas", 0, 100, 1, [] (double v) { return String ((int) v) + "%"; },
               [] (const S& s) { return (double) s.tupletSensitivity; },
               [] (S& s, double v) { s.tupletSensitivity = (int) v; });
    addChoice ("Escrita de sincopes", { "Moderna (sincopas simetricas)", "Rigida (ligaduras no tempo)" },
               [] (const S& s) { return (int) s.syncopation; },
               [] (S& s, int i) { s.syncopation = (S::Syncopation) i; });
    addChoice ("Pontos de aumento", { "Nenhum", "Ate 1", "Ate 2" },
               [] (const S& s) { return s.maxDots; },
               [] (S& s, int i) { s.maxDots = i; });

    addSection ("EXECUCAO HUMANA");
    addSlider ("Tolerancia de acorde", 0, 48, 1, [this] (double v) { return String (roundToInt (v / score::DIV * 60000.0 / bpm)) + " ms"; },
               [] (const S& s) { return (double) s.chordTolerance; },
               [] (S& s, double v) { s.chordTolerance = (int) v; });
    addSlider ("Arredondar finais para baixo", 0, 50, 1, [] (double v) { return String ((int) v) + "%"; },
               [] (const S& s) { return (double) s.releaseBias; },
               [] (S& s, double v) { s.releaseBias = (int) v; });
    addChoice ("Fechar pausas menores que", { "Nunca", "1/32", "1/16", "1/8", "1/4" },
               [] (const S& s) { const int v[] = { 0, 12, 24, 48, 96 }; return indexOf (v, 5, s.fillGaps, 2); },
               [] (S& s, int i) { const int v[] = { 0, 12, 24, 48, 96 }; s.fillGaps = v[i]; });
    addChoice ("Ignorar notas menores que", { "Nao ignorar", "1/64", "1/32", "1/16" },
               [] (const S& s) { const int v[] = { 0, 6, 12, 24 }; return indexOf (v, 4, s.ignoreShorterThan, 1); },
               [] (S& s, int i) { const int v[] = { 0, 6, 12, 24 }; s.ignoreShorterThan = v[i]; });
    addSlider ("Ignorar velocity abaixo de", 0, 127, 1, [] (double v) { return v <= 0 ? String ("-") : String ((int) v); },
               [] (const S& s) { return (double) s.minVelocity; },
               [] (S& s, double v) { s.minVelocity = (int) v; });

    addSection ("VOZES E PAUTAS");
    addChoice ("Polifonia", { "Vozes automaticas", "Tudo em acordes" },
               [] (const S& s) { return s.polyphony == S::Polyphony::autoVoices ? 0 : 1; },
               [] (S& s, int i) { s.polyphony = i == 0 ? S::Polyphony::autoVoices : S::Polyphony::chords; });
    addChoice ("Pautas", { "Automatico", "Uma pauta", "Piano (duas pautas)" },
               [] (const S& s) { return (int) s.staves; },
               [] (S& s, int i) { s.staves = (S::Staves) i; });
    addChoice ("Divisao das maos", { "Inteligente", "Ponto fixo" },
               [] (const S& s) { return (int) s.split; },
               [] (S& s, int i) { s.split = (S::Split) i; });
    addSlider ("Ponto de divisao", 36, 84, 1, [] (double v) { return noteName ((int) v); },
               [] (const S& s) { return (double) s.splitPoint; },
               [] (S& s, double v) { s.splitPoint = (int) v; });

    addSection ("TONALIDADE E CIFRAS");
    {
        StringArray keys { "Automatica" };
        for (int f = -7; f <= 7; ++f)
        {
            keys.add (keyName (f, false));
            keys.add (keyName (f, true));
        }

        addChoice ("Tonalidade", keys,
                   [] (const S& s) { return s.autoKey ? 0 : 1 + (s.keyFifths + 7) * 2 + (s.keyMinor ? 1 : 0); },
                   [] (S& s, int i)
                   {
                       s.autoKey = (i == 0);
                       if (i > 0) { s.keyFifths = (i - 1) / 2 - 7; s.keyMinor = ((i - 1) % 2) == 1; }
                   });
    }
    addToggle ("Cifras automaticas",
               [] (const S& s) { return s.chordSymbols; },
               [] (S& s, bool b) { s.chordSymbols = b; });
    addChoice ("Frequencia das cifras", { "Por compasso", "Meio compasso", "Por tempo" },
               [] (const S& s) { return (int) s.chordRate; },
               [] (S& s, int i) { s.chordRate = (S::ChordRate) i; });

    addSection ("ARTICULACOES");
    addToggle ("Detectar staccato",
               [] (const S& s) { return s.detectStaccato; },
               [] (S& s, bool b) { s.detectStaccato = b; });
    addSlider ("Staccato abaixo de", 10, 90, 1, [] (double v) { return String ((int) v) + "% da duracao"; },
               [] (const S& s) { return (double) s.staccatoPercent; },
               [] (S& s, double v) { s.staccatoPercent = (int) v; });
    addToggle ("Acentos pela velocity",
               [] (const S& s) { return s.detectAccents; },
               [] (S& s, bool b) { s.detectAccents = b; });
    addSlider ("Acento a partir de", 64, 127, 1, [] (double v) { return String ((int) v); },
               [] (const S& s) { return (double) s.accentVelocity; },
               [] (S& s, double v) { s.accentVelocity = (int) v; });

    for (auto& r : rows)
        if (r.editor != nullptr)
            addAndMakeVisible (*r.editor);

    ZS::disableFocus (*this);
}

void CleanupPanel::addSection (const String& name)
{
    Row r;
    r.section = name;
    rows.push_back (std::move (r));
}

void CleanupPanel::addChoice (const String& label, StringArray items,
                              std::function<int (const score::Settings&)> get,
                              std::function<void (score::Settings&, int)> set)
{
    auto box = std::make_unique<ComboBox>();
    box->addItemList (items, 1);
    auto* b = box.get();

    box->onChange = [this, b, set]
    {
        if (updating) return;
        set (settings, b->getSelectedItemIndex());
        changed();
    };

    Row r;
    r.label = label;
    r.refresh = [this, b, get] { b->setSelectedItemIndex (get (settings), dontSendNotification); };
    r.editor = std::move (box);
    rows.push_back (std::move (r));
}

void CleanupPanel::addSlider (const String& label, double min, double max, double step,
                              std::function<String (double)> format,
                              std::function<double (const score::Settings&)> get,
                              std::function<void (score::Settings&, double)> set)
{
    auto sl = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::TextBoxRight);
    sl->setRange (min, max, step);
    sl->setTextBoxStyle (Slider::TextBoxRight, true, 86, 20);
    sl->textFromValueFunction = format;
    auto* s = sl.get();

    s->onValueChange = [this, s, set]
    {
        if (updating) return;
        set (settings, s->getValue());
        changed();
    };
    s->onDragEnd = [s] { s->updateText(); };

    Row r;
    r.label = label;
    r.refresh = [this, s, get] { s->setValue (get (settings), dontSendNotification); s->updateText(); };
    r.editor = std::move (sl);
    rows.push_back (std::move (r));
}

void CleanupPanel::addToggle (const String& label,
                              std::function<bool (const score::Settings&)> get,
                              std::function<void (score::Settings&, bool)> set)
{
    auto t = std::make_unique<ToggleButton> (label);
    auto* b = t.get();

    b->onClick = [this, b, set]
    {
        if (updating) return;
        set (settings, b->getToggleState());
        changed();
    };

    Row r;
    r.refresh = [this, b, get] { b->setToggleState (get (settings), dontSendNotification); };
    r.editor = std::move (t);
    rows.push_back (std::move (r));
}

void CleanupPanel::setSettings (const score::Settings& s, double newBpm)
{
    settings = s;
    bpm = newBpm;
    updating = true;

    for (auto& r : rows)
        if (r.refresh)
            r.refresh();

    updating = false;
}

void CleanupPanel::paint (Graphics& g)
{
    g.fillAll (Theme::panel);
    int y = 0;

    for (auto& r : rows)
    {
        if (r.section.isNotEmpty())
        {
            y += 10;
            g.setColour (Theme::accent);
            g.setFont (FontOptions (11.5f, Font::bold));
            g.drawText (r.section, 14, y, getWidth() - 28, 18, Justification::centredLeft);
            y += 22;
        }
        else if (r.label.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (FontOptions (12.0f));
            g.drawText (r.label, 14, y, getWidth() - 28, 16, Justification::centredLeft);
            y += 16 + 28;
        }
        else
        {
            y += 28;
        }
    }
}

void CleanupPanel::resized()
{
    int y = 0;

    for (auto& r : rows)
    {
        if (r.section.isNotEmpty())
        {
            y += 32;
            continue;
        }

        if (r.label.isNotEmpty())
            y += 16;

        if (r.editor != nullptr)
            r.editor->setBounds (12, y + 2, getWidth() - 24, 24);

        y += 28;
    }

    idealHeight = y + 20;
}

//==============================================================================
// ScorePage
//==============================================================================
void ScorePage::setDrawable (std::unique_ptr<Drawable> d, std::vector<ScoreText> newTexts, int width, int height)
{
    drawable = std::move (d);
    texts = std::move (newTexts);

    if (drawable != nullptr)
        setSize (jmax (width + 40, getParentWidth()), height + 60);
    else
        setSize (jmax (200, getParentWidth()), 300);

    repaint();
}

void ScorePage::paint (Graphics& g)
{
    g.fillAll (Theme::background);

    auto paper = getLocalBounds().reduced (20, 20);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (paper.translated (3, 4));
    g.setColour (Colour (0xfffdfcf8));
    g.fillRect (paper);

    if (drawable != nullptr)
    {
        drawable->drawAt (g, (float) paper.getX(), (float) paper.getY(), 1.0f);

        g.setColour (Colours::black);
        for (auto& t : texts)
        {
            auto style = (t.bold ? Font::bold : 0) | (t.italic ? Font::italic : 0);
            g.setFont (FontOptions (Font::getDefaultSerifFontName(), t.size, style));
            g.drawSingleLineText (t.text, roundToInt ((float) paper.getX() + t.position.x),
                                  roundToInt ((float) paper.getY() + t.position.y), t.justification);
        }
    }
    else
    {
        g.setColour (Colours::grey);
        g.setFont (FontOptions (15.0f));
        g.drawFittedText (message, paper.reduced (20), Justification::centred, 3);
    }
}

//==============================================================================
// ScoreView
//==============================================================================
ScoreView::ScoreView (te::Engine& e) : engine (e)
{
    title.setText ("LIMPEZA DO MIDI", dontSendNotification);
    title.setFont (FontOptions (15.0f, Font::bold));
    title.setColour (Label::textColourId, Theme::text);
    addAndMakeVisible (title);

    presets.addItemList ({ "Equilibrado", "Piano", "Melodia", "Rubato (livre)", "Rigido (fiel ao tocado)" }, 1);
    presets.setTextWhenNothingSelected ("Ponto de partida...");
    presets.onChange = [this]
    {
        const int i = presets.getSelectedItemIndex();
        if (i < 0) return;
        applySettings (score::makePreset ((score::Preset) i));
        loadSettingsIntoPanel();
    };
    addAndMakeVisible (presets);

    scope.addItemList ({ "Ajustes do projeto (todas as faixas)", "Ajustes so desta faixa" }, 1);
    scope.onChange = [this]
    {
        if (edit == nullptr || selectedTrack == nullptr)
            return;

        auto& um = edit->getUndoManager();
        um.beginNewTransaction ("Escopo da limpeza");

        auto t = selectedTrack->state.getOrCreateChildWithName (ScoreSettingsTree::node, &um);
        const bool own = scope.getSelectedItemIndex() == 1;

        if (own && ! (bool) t[ScoreSettingsTree::overrideFlag])
            ScoreSettingsTree::write (t, ScoreSettingsTree::resolve (edit->state, selectedTrack->state), &um);

        t.setProperty (ScoreSettingsTree::overrideFlag, own, &um);
        loadSettingsIntoPanel();
        requestUpdate (true);
    };
    addAndMakeVisible (scope);

    which.addItemList ({ "Mostrar: faixa selecionada", "Mostrar: todas as faixas de instrumento" }, 1);
    which.setSelectedItemIndex (0, dontSendNotification);
    which.onChange = [this] { requestUpdate (true); };
    addAndMakeVisible (which);

    resetButton.onClick = [this]
    {
        applySettings (score::Settings());
        loadSettingsIntoPanel();
    };
    addAndMakeVisible (resetButton);

    exportButton.onClick = [this] { exportMusicXML(); };
    exportButton.setColour (TextButton::buttonColourId, Theme::accent);
    exportButton.setColour (TextButton::textColourOffId, Colour (0xff111111));
    addAndMakeVisible (exportButton);

    zoom.setRange (20, 80, 1);
    zoom.setValue (38, dontSendNotification);
    zoom.setTooltip ("Tamanho da partitura");
    zoom.onValueChange = [this] { requestUpdate (false); };
    addAndMakeVisible (zoom);

    status.setColour (Label::textColourId, Theme::textDim);
    status.setFont (FontOptions (13.0f));
    addAndMakeVisible (status);

    panel.onChange = [this] (const score::Settings& s) { applySettings (s); };
    panelViewport.setViewedComponent (&panel, false);
    panelViewport.setScrollBarsShown (true, false);
    panelViewport.setScrollBarThickness (8);
    addAndMakeVisible (panelViewport);

    pageViewport.setViewedComponent (&page, false);
    pageViewport.setScrollBarThickness (10);
    addAndMakeVisible (pageViewport);

    ZS::disableFocus (*this);
}

ScoreView::~ScoreView()
{
    renderThread.reset();
    setEdit (nullptr);
}

String ScoreView::getResourcePath()
{
    // As fontes do Verovio vão embutidas no app e são extraídas uma vez
    auto dir = File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("Z-Studio").getChildFile ("verovio-data")
                   .getChildFile (String (ZeufgData::namedResourceListSize > 0 ? ZeufgData::namedResourceList[0] : "x")
                                  + "-" + String (JUCE_APPLICATION_VERSION_STRING));

    if (! dir.getChildFile ("Bravura.xml").existsAsFile())
    {
        int size = 0;
        if (ZeufgData::namedResourceListSize > 0)
            if (auto* data = ZeufgData::getNamedResource (ZeufgData::namedResourceList[0], size))
            {
                dir.createDirectory();
                MemoryInputStream in (data, (size_t) size, false);
                ZipFile zip (in);
                [[maybe_unused]] auto r = zip.uncompressTo (dir, true);
            }
    }

    return dir.getFullPathName();
}

void ScoreView::setEdit (te::Edit* e)
{
    if (edit != nullptr)
        edit->state.removeListener (this);

    edit = e;
    selectedTrack = nullptr;
    lastMusicXml.clear();

    if (edit != nullptr)
    {
        edit->state.addListener (this);
        loadSettingsIntoPanel();
        requestUpdate (true);
    }
}

void ScoreView::setSelectedTrack (te::AudioTrack* t)
{
    if (selectedTrack.get() == t)
        return;

    selectedTrack = t;
    loadSettingsIntoPanel();

    if (which.getSelectedItemIndex() == 0)
        requestUpdate (true);
}

void ScoreView::loadSettingsIntoPanel()
{
    if (edit == nullptr)
        return;

    const auto bpm = edit->tempoSequence.getTempo (0)->getBpm();

    if (selectedTrack != nullptr)
    {
        auto t = selectedTrack->state.getChildWithName (ScoreSettingsTree::node);
        scope.setSelectedItemIndex (t.isValid() && (bool) t[ScoreSettingsTree::overrideFlag] ? 1 : 0, dontSendNotification);
        panel.setSettings (ScoreSettingsTree::resolve (edit->state, selectedTrack->state), bpm);
    }
    else
    {
        scope.setSelectedItemIndex (0, dontSendNotification);
        panel.setSettings (ScoreSettingsTree::read (edit->state.getChildWithName (ScoreSettingsTree::node)), bpm);
    }

    scope.setEnabled (selectedTrack != nullptr);
}

void ScoreView::applySettings (const score::Settings& s)
{
    if (edit == nullptr)
        return;

    auto* um = &edit->getUndoManager();
    um->beginNewTransaction ("Limpeza do MIDI");

    juce::ValueTree target;

    if (selectedTrack != nullptr && scope.getSelectedItemIndex() == 1)
        target = selectedTrack->state.getOrCreateChildWithName (ScoreSettingsTree::node, um);
    else
        target = edit->state.getOrCreateChildWithName (ScoreSettingsTree::node, um);

    ScoreSettingsTree::write (target, s, um);
    requestUpdate (true);
}

Array<te::AudioTrack*> ScoreView::getTracksToShow() const
{
    Array<te::AudioTrack*> result;

    if (edit == nullptr)
        return result;

    if (which.getSelectedItemIndex() == 0)
    {
        if (selectedTrack != nullptr)
            result.add (selectedTrack.get());
        return result;
    }

    for (auto t : ZS::getUserTracks (*edit))
        if (ZS::isInstrumentTrack (*t))
            result.add (t);

    return result;
}

std::vector<score::InputTrack> ScoreView::gatherTracks (std::vector<score::Settings>& settings) const
{
    std::vector<score::InputTrack> result;

    for (auto* t : getTracksToShow())
    {
        score::InputTrack it;
        it.name = t->getName().toStdString();
        int id = 0;

        for (auto c : t->getClips())
        {
            auto mc = dynamic_cast<te::MidiClip*> (c);
            if (mc == nullptr)
                continue;

            const double clipStart = mc->getStartBeat().inBeats();
            const double offset = mc->getOffsetInBeats().inBeats();
            const double length = mc->getLengthInBeats().inBeats();

            for (auto n : mc->getSequence().getNotes())
            {
                const double s = n->getStartBeat().inBeats() - offset;
                if (s < -1.0e-6 || s >= length)
                    continue;

                score::InputNote in;
                in.pitch = n->getNoteNumber();
                in.velocity = n->getVelocity();
                in.startQ = clipStart + s;
                in.lengthQ = std::min (n->getLengthBeats().inBeats(), length - s);
                in.id = id++;
                it.notes.push_back (in);
            }
        }

        result.push_back (std::move (it));
        settings.push_back (ScoreSettingsTree::resolve (edit->state, t->state));
    }

    return result;
}

void ScoreView::requestUpdate (bool immediate)
{
    if (! isShowing() && ! immediate)
        return;

    startTimer (immediate ? 30 : 250);
}

void ScoreView::timerCallback()
{
    stopTimer();

    if (isShowing())
        startRender();
}

void ScoreView::startRender()
{
    if (edit == nullptr)
        return;

    if (renderThread == nullptr)
        renderThread = std::make_unique<ScoreRenderThread> (*this, getResourcePath());

    ScoreRenderThread::Request r;
    r.tracks = gatherTracks (r.settings);

    if (auto ts = edit->tempoSequence.getTimeSig (0))
    {
        r.beats = ts->numerator;
        r.beatType = ts->denominator;
    }

    r.bpm = edit->tempoSequence.getTempo (0)->getBpm();
    r.title = edit->getName().toStdString();
    r.scale = (int) zoom.getValue();
    r.width = jmax (400, pageViewport.getWidth() - 60);
    lastWidth = pageViewport.getWidth();

    if (r.tracks.empty())
    {
        page.message = "Selecione uma faixa de instrumento com MIDI para ver a partitura.";
        page.setDrawable (nullptr, {}, 0, 0);
        status.setText ({}, dontSendNotification);
        return;
    }

    renderThread->post (std::move (r));
}

void ScoreView::exportMusicXML()
{
    if (edit == nullptr)
        return;

    // gera de novo, com os ajustes atuais, para garantir que o arquivo bate com a tela
    std::vector<score::Settings> settings;
    auto tracks = gatherTracks (settings);

    if (tracks.empty())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Exportar MusicXML",
                                          "Selecione uma faixa de instrumento com notas MIDI."_t);
        return;
    }

    int beats = 4, beatType = 4;
    if (auto ts = edit->tempoSequence.getTimeSig (0)) { beats = ts->numerator; beatType = ts->denominator; }

    auto sc = score::buildScore (tracks, settings, beats, beatType, edit->tempoSequence.getTempo (0)->getBpm(),
                                 edit->getName().toStdString());
    auto xml = score::toMusicXML (sc);

    auto suggested = (tracks.size() == 1 ? String (tracks.front().name) : String ("Partitura")) + ".musicxml";
    chooser = std::make_shared<FileChooser> ("Exportar MusicXML",
                                             ZS::getDefaultProjectsFolder().getChildFile (File::createLegalFileName (suggested)),
                                             "*.musicxml;*.xml");

    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                            | FileBrowserComponent::warnAboutOverwriting,
                          [xml] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File())
            return;

        if (! f.hasFileExtension ("musicxml;xml"))
            f = f.withFileExtension (".musicxml");

        if (f.replaceWithText (String::fromUTF8 (xml.c_str()), false, false, "\n"))
            f.revealToUser();
        else
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Exportar MusicXML",
                                              "Nao foi possivel salvar o arquivo.");
    });
}

void ScoreView::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromLeft (300));
    g.setColour (Theme::outline);
    g.drawVerticalLine (299, 0.0f, (float) getHeight());

    auto top = getLocalBounds().withTrimmedLeft (300).removeFromTop (46);
    g.setColour (Theme::panel);
    g.fillRect (top);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (top.getBottom() - 1, (float) top.getX(), (float) top.getRight());
}

void ScoreView::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (300).reduced (0, 8);

    title.setBounds (left.removeFromTop (26).withTrimmedLeft (10));
    auto line = [&] (Component& c) { c.setBounds (left.removeFromTop (28).reduced (12, 2)); };
    line (presets);
    line (scope);
    line (which);
    resetButton.setBounds (left.removeFromTop (30).reduced (12, 3).removeFromLeft (110));
    left.removeFromTop (4);

    panelViewport.setBounds (left.withTrimmedRight (1));
    panel.setSize (panelViewport.getWidth() - panelViewport.getScrollBarThickness(), 100);
    panel.resized();
    panel.setSize (panel.getWidth(), panel.getIdealHeight());

    auto top = r.removeFromTop (46).reduced (12, 8);
    exportButton.setBounds (top.removeFromRight (180));
    top.removeFromRight (12);
    zoom.setBounds (top.removeFromRight (140));
    top.removeFromRight (8);
    status.setBounds (top);

    pageViewport.setBounds (r);

    if (std::abs (pageViewport.getWidth() - lastWidth) > 30)
        requestUpdate (false);
}
