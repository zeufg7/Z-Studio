#include "ScoreModel.h"

#include <sstream>
#include <algorithm>

namespace score
{
namespace
{
    std::string esc (const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            switch (c)
            {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default:  out += c;
            }
        }
        return out;
    }

    const char* typeName (int ticks)
    {
        switch (ticks)
        {
            case 384: return "whole";
            case 192: return "half";
            case 96:  return "quarter";
            case 48:  return "eighth";
            case 24:  return "16th";
            case 12:  return "32nd";
            case 6:   return "64th";
            case 3:   return "128th";
            default:  return "quarter";
        }
    }

    const char* accidentalName (int alter)
    {
        switch (alter)
        {
            case -2: return "flat-flat";
            case -1: return "flat";
            case 1:  return "sharp";
            case 2:  return "double-sharp";
            default: return "natural";
        }
    }

    void writeHarmony (std::ostringstream& x, const Harmony& h, int offset, int staff)
    {
        x << "      <harmony print-frame=\"no\">\n"
          << "        <root><root-step>" << h.rootStep << "</root-step>";
        if (h.rootAlter != 0) x << "<root-alter>" << h.rootAlter << "</root-alter>";
        x << "</root>\n"
          << "        <kind text=\"" << esc (h.text) << "\">" << h.kind << "</kind>\n";

        if (h.hasBass)
        {
            x << "        <bass><bass-step>" << h.bassStep << "</bass-step>";
            if (h.bassAlter != 0) x << "<bass-alter>" << h.bassAlter << "</bass-alter>";
            x << "</bass>\n";
        }

        if (offset != 0)
            x << "        <offset>" << offset << "</offset>\n";

        x << "        <staff>" << staff << "</staff>\n"
          << "      </harmony>\n";
    }

    void writeElement (std::ostringstream& x, const Element& e, const Voice& v, bool multiVoice)
    {
        const size_t count = e.isRest() ? 1 : e.pitches.size();

        for (size_t i = 0; i < count; ++i)
        {
            x << "      <note" << (e.hidden ? " print-object=\"no\"" : "") << ">\n";

            if (i > 0)
                x << "        <chord/>\n";

            const Pitch* p = e.isRest() ? nullptr : &e.pitches[i];

            if (p == nullptr)
            {
                x << "        <rest" << (e.wholeMeasureRest ? " measure=\"yes\"" : "") << "/>\n";
            }
            else
            {
                x << "        <pitch><step>" << p->step << "</step>";
                if (p->alter != 0) x << "<alter>" << p->alter << "</alter>";
                x << "<octave>" << p->octave << "</octave></pitch>\n";
            }

            x << "        <duration>" << e.duration << "</duration>\n";

            if (p != nullptr)
            {
                if (p->tieStop)  x << "        <tie type=\"stop\"/>\n";
                if (p->tieStart) x << "        <tie type=\"start\"/>\n";
            }

            x << "        <voice>" << v.number << "</voice>\n";

            if (! e.wholeMeasureRest)
            {
                x << "        <type>" << typeName (e.typeTicks) << "</type>\n";
                for (int d = 0; d < e.dots; ++d)
                    x << "        <dot/>\n";
            }

            if (p != nullptr && p->showAccidental)
                x << "        <accidental>" << accidentalName (p->accidental) << "</accidental>\n";

            if (e.triplet)
                x << "        <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>\n";

            if (p != nullptr && multiVoice)
                x << "        <stem>" << ((v.number % 2 == 1) ? "up" : "down") << "</stem>\n";

            x << "        <staff>" << v.staff << "</staff>\n";

            if (i == 0)
                for (size_t b = 0; b < e.beams.size(); ++b)
                    x << "        <beam number=\"" << (b + 1) << "\">" << e.beams[b] << "</beam>\n";

            // notações
            std::ostringstream n;

            if (p != nullptr)
            {
                if (p->tieStop)  n << "          <tied type=\"stop\"/>\n";
                if (p->tieStart) n << "          <tied type=\"start\"/>\n";
            }

            if (i == 0)
            {
                if (e.tupletStart) n << "          <tuplet type=\"start\" bracket=\"no\"/>\n";
                if (e.tupletStop)  n << "          <tuplet type=\"stop\"/>\n";

                if (e.staccato || e.accent)
                {
                    n << "          <articulations>";
                    if (e.accent)   n << "<accent/>";
                    if (e.staccato) n << "<staccato/>";
                    n << "</articulations>\n";
                }
            }

            if (! n.str().empty())
                x << "        <notations>\n" << n.str() << "        </notations>\n";

            x << "      </note>\n";
        }
    }
}

//==============================================================================
std::string toMusicXML (const Score& sc)
{
    std::ostringstream x;

    x << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
      << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
         "\"http://www.musicxml.org/dtds/partwise.dtd\">\n"
      << "<score-partwise version=\"4.0\">\n";

    if (! sc.title.empty())
        x << "  <work><work-title>" << esc (sc.title) << "</work-title></work>\n";

    x << "  <identification>\n"
      << "    <encoding><software>Z-Studio</software></encoding>\n"
      << "  </identification>\n";

    x << "  <part-list>\n";
    for (size_t i = 0; i < sc.parts.size(); ++i)
        x << "    <score-part id=\"P" << (i + 1) << "\"><part-name>" << esc (sc.parts[i].name) << "</part-name></score-part>\n";
    x << "  </part-list>\n";

    for (size_t pi = 0; pi < sc.parts.size(); ++pi)
    {
        auto& part = sc.parts[pi];
        x << "  <part id=\"P" << (pi + 1) << "\">\n";

        for (size_t mi = 0; mi < part.measures.size(); ++mi)
        {
            auto& m = part.measures[mi];
            x << "    <measure number=\"" << m.number << "\">\n";

            if (mi == 0)
            {
                x << "      <attributes>\n"
                  << "        <divisions>" << DIV << "</divisions>\n"
                  << "        <key><fifths>" << part.keyFifths << "</fifths><mode>" << (part.minor ? "minor" : "major") << "</mode></key>\n"
                  << "        <time><beats>" << sc.beats << "</beats><beat-type>" << sc.beatType << "</beat-type></time>\n";

                if (part.staves > 1)
                    x << "        <staves>" << part.staves << "</staves>\n";

                for (int s = 0; s < part.staves; ++s)
                {
                    const bool treble = part.clefs[s] == "G";
                    x << "        <clef number=\"" << (s + 1) << "\"><sign>" << (treble ? "G" : "F")
                      << "</sign><line>" << (treble ? 2 : 4) << "</line></clef>\n";
                }

                x << "      </attributes>\n";

                if (pi == 0)
                    x << "      <direction placement=\"above\">\n"
                      << "        <direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>"
                      << (int) (sc.bpm + 0.5) << "</per-minute></metronome></direction-type>\n"
                      << "        <staff>1</staff>\n"
                      << "        <sound tempo=\"" << sc.bpm << "\"/>\n"
                      << "      </direction>\n";
            }

            // voz 1 e 2 compartilham a pauta: hastes definidas quando há duas vozes
            auto voices = m.voices;
            std::sort (voices.begin(), voices.end(), [] (auto& a, auto& b) { return a.number < b.number; });

            for (size_t vi = 0; vi < voices.size(); ++vi)
            {
                auto& v = voices[vi];

                if (vi > 0)
                    x << "      <backup><duration>" << m.length << "</duration></backup>\n";

                bool multi = false;
                for (auto& other : voices)
                    if (&other != &v && other.staff == v.staff)
                        multi = true;

                const bool harmonyVoice = (vi == 0);
                size_t nextHarmony = 0;

                for (auto& e : v.elements)
                {
                    while (harmonyVoice && nextHarmony < m.harmonies.size()
                           && m.harmonies[nextHarmony].position < e.start + e.duration)
                    {
                        auto& h = m.harmonies[nextHarmony];
                        writeHarmony (x, h, std::max (0, h.position - e.start), 1);
                        ++nextHarmony;
                    }

                    writeElement (x, e, v, multi);
                }
            }

            x << "    </measure>\n";
        }

        x << "  </part>\n";
    }

    x << "</score-partwise>\n";
    return x.str();
}
}
