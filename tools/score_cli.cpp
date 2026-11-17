// Ferramenta de teste do motor de partitura (não faz parte do app).
// Uso: score_cli entrada.mid saida_prefixo [grid=24] [fill=24] [poly=voices|chords]
//                [staves=auto|single|piano] [tuplets=1] [sens=50] [sync=modern|rigid] [chords=1]

#include <JuceHeader.h>
#include "../Source/Score/ScoreModel.h"
#include "../Source/Score/ScoreRenderer.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 3)
    {
        std::cerr << "uso: score_cli entrada.mid saida [opcoes]\n";
        return 1;
    }

    juce::File in (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    juce::String out (argv[2]);

    juce::FileInputStream stream (in);
    juce::MidiFile mf;
    if (! stream.openedOk() || ! mf.readFrom (stream))
    {
        std::cerr << "nao consegui ler " << argv[1] << "\n";
        return 1;
    }

    const double tpq = mf.getTimeFormat();
    std::vector<score::InputTrack> tracks;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        score::InputTrack track;
        track.name = "Faixa " + std::to_string (t + 1);

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto& m = seq.getEventPointer (i)->message;
            if (m.isTrackNameEvent()) track.name = m.getTextFromTextMetaEvent().toStdString();
            if (! m.isNoteOn()) continue;

            score::InputNote n;
            n.pitch = m.getNoteNumber();
            n.velocity = m.getVelocity();
            n.startQ = m.getTimeStamp() / tpq;
            n.lengthQ = (seq.getTimeOfMatchingKeyUp (i) - m.getTimeStamp()) / tpq;
            n.id = i;
            track.notes.push_back (n);
        }

        if (! track.notes.empty())
            tracks.push_back (track);
    }

    score::Settings s;
    for (int a = 3; a < argc; ++a)
    {
        juce::String arg (argv[a]);
        auto k = arg.upToFirstOccurrenceOf ("=", false, false);
        auto v = arg.fromFirstOccurrenceOf ("=", false, false);

        if (k == "grid")    s.startGrid = v.getIntValue();
        if (k == "lgrid")   s.lengthGrid = v.getIntValue();
        if (k == "fill")    s.fillGaps = v.getIntValue();
        if (k == "ignore")  s.ignoreShorterThan = v.getIntValue();
        if (k == "poly")    s.polyphony = v == "chords" ? score::Settings::Polyphony::chords : score::Settings::Polyphony::autoVoices;
        if (k == "staves")  s.staves = v == "piano" ? score::Settings::Staves::piano : v == "single" ? score::Settings::Staves::single : score::Settings::Staves::automatic;
        if (k == "tuplets") s.autoTuplets = v.getIntValue() != 0;
        if (k == "sens")    s.tupletSensitivity = v.getIntValue();
        if (k == "sync")    s.syncopation = v == "rigid" ? score::Settings::Syncopation::rigid : score::Settings::Syncopation::modern;
        if (k == "chords")  s.chordSymbols = v.getIntValue() != 0;
        if (k == "stacc")   s.detectStaccato = v.getIntValue() != 0;
    }

    std::vector<score::Settings> settings (tracks.size(), s);
    auto sc = score::buildScore (tracks, settings, 4, 4, 120.0, in.getFileNameWithoutExtension().toStdString());
    auto xml = score::toMusicXML (sc);

    juce::File (out + ".musicxml").replaceWithText (xml);

    auto dataDir = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("libs/verovio/data");
    score::Renderer renderer (dataDir.getFullPathName().toStdString());

    if (! renderer.isReady())
    {
        std::cerr << "verovio sem recursos\n";
        return 2;
    }

    auto svg = renderer.render (xml, 1400, 45);
    juce::File (out + ".svg").replaceWithText (svg);

    // Desenha com o JUCE (o mesmo caminho do app) para conferir a renderização
    {
        if (auto drawable = juce::Drawable::createFromSVGString (juce::String::fromUTF8 (svg.c_str())))
        {
            auto bounds = drawable->getDrawableBounds();
            const int w = juce::jmax (100, (int) bounds.getRight() + 20);
            const int h = juce::jmax (100, (int) bounds.getBottom() + 20);

            juce::Image img (juce::Image::ARGB, w, h, true);
            juce::Graphics g (img);
            g.fillAll (juce::Colours::white);
            drawable->draw (g, 1.0f);

            juce::FileOutputStream os (juce::File (out + ".png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "ok: " << w << "x" << h << "  partes=" << sc.parts.size()
                      << "  compassos=" << (sc.parts.empty() ? 0 : sc.parts[0].measures.size()) << "\n";
        }
    }

    return 0;
}
