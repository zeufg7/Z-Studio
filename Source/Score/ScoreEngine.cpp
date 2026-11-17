#include "ScoreModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <set>
#include <functional>

namespace score
{
//==============================================================================
Settings makePreset (Preset p)
{
    Settings s;

    switch (p)
    {
        case Preset::balanced:
            break;

        case Preset::piano:
            s.staves = Settings::Staves::piano;
            s.polyphony = Settings::Polyphony::autoVoices;
            s.chordSymbols = true;
            break;

        case Preset::melody:
            s.staves = Settings::Staves::single;
            s.polyphony = Settings::Polyphony::chords;
            s.fillGaps = 24;
            s.chordSymbols = false;
            break;

        case Preset::rubato:      // execução livre: grade mais larga, mais tolerância
            s.startGrid = 48;
            s.fillGaps = 48;
            s.ignoreShorterThan = 12;
            s.tupletSensitivity = 35;
            s.syncopation = Settings::Syncopation::modern;
            break;

        case Preset::strict:      // tudo como foi tocado, grade fina
            s.startGrid = 12;
            s.fillGaps = 0;
            s.ignoreShorterThan = 0;
            s.tupletSensitivity = 60;
            s.syncopation = Settings::Syncopation::rigid;
            s.detectStaccato = false;
            break;
    }

    return s;
}

std::string gridName (int t)
{
    switch (t)
    {
        case 384: return "1/1";
        case 192: return "1/2";
        case 96:  return "1/4";
        case 48:  return "1/8";
        case 24:  return "1/16";
        case 12:  return "1/32";
        case 6:   return "1/64";
        default:  return std::to_string (t);
    }
}

namespace
{
    //==============================================================================
    // Nota já quantizada
    struct QNote
    {
        int pitch = 60, start = 0, end = 0, velocity = 100;
        double played = 0.0;   // duração real tocada (tiques)
        int staff = 1;
        bool staccato = false, accent = false;
    };

    struct Event          // nota ou acorde numa voz
    {
        int start = 0, end = 0;
        std::vector<QNote> notes;
    };

    struct Meter
    {
        int beats = 4, beatType = 4;
        int measure = 4 * DIV;
        int beat = DIV;
        bool compound = false;
        bool halfBar = true;

        Meter (int b, int bt) : beats (b), beatType (bt)
        {
            measure = b * 4 * DIV / bt;
            compound = (bt == 8 && b % 3 == 0 && b > 3);
            beat = compound ? (3 * DIV / 2) : (4 * DIV / bt);
            halfBar = ! compound && (b % 2 == 0) && b >= 4;
        }
    };

    int floorDiv (int a, int b)    { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

    //==============================================================================
    // Grafia (enarmonia) e tonalidade

    const char  stepNames[7]   = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
    const int   stepPc[7]      = { 0, 2, 4, 5, 7, 9, 11 };

    int stepIndex (char s)
    {
        for (int i = 0; i < 7; ++i)
            if (stepNames[i] == s)
                return i;
        return 0;
    }

    /** Alteração da armadura para cada nota (C..B) numa tonalidade. */
    std::array<int, 7> keyAlters (int fifths)
    {
        std::array<int, 7> a {};
        const int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B
        const int flatOrder[7]  = { 6, 2, 5, 1, 4, 0, 3 };   // B E A D G C F

        for (int i = 0; i < std::min (7, std::abs (fifths)); ++i)
            a[(size_t) (fifths > 0 ? sharpOrder[i] : flatOrder[i])] = fifths > 0 ? 1 : -1;

        return a;
    }

    int majorTonicPc (int fifths)   { return ((fifths * 7) % 12 + 12) % 12; }

    struct Speller
    {
        int fifths = 0;
        bool minor = false;
        std::array<int, 7> alters {};
        int leadingPc = -1, raisedSixthPc = -1;
        int leadingStep = 0, sixthStep = 0;

        Speller (int f, bool m) : fifths (f), minor (m), alters (keyAlters (f))
        {
            if (minor)
            {
                // tônica menor = 6º grau da relativa maior
                const int majorTonic = majorTonicPc (fifths);
                const int tonicPc = (majorTonic + 9) % 12;
                leadingPc = (tonicPc + 11) % 12;
                raisedSixthPc = (tonicPc + 9) % 12;

                // descobre o "step" da tônica maior e daí os graus
                int majorStep = 0;
                for (int i = 0; i < 7; ++i)
                    if ((stepPc[i] + alters[(size_t) i] + 12) % 12 == majorTonic)
                        majorStep = i;

                const int tonicStep = (majorStep + 5) % 7;
                leadingStep = (tonicStep + 6) % 7;
                sixthStep = (tonicStep + 5) % 7;
            }
        }

        Pitch spell (int midi) const
        {
            const int pc = ((midi % 12) + 12) % 12;
            int step = -1, alter = 0;

            // 1) diatônica
            for (int i = 0; i < 7 && step < 0; ++i)
                if ((stepPc[i] + alters[(size_t) i] + 12) % 12 == pc)
                {
                    step = i;
                    alter = alters[(size_t) i];
                }

            // 2) sensível / 6º elevado no menor
            if (step < 0 && minor && pc == leadingPc)
            {
                step = leadingStep;
                alter = alters[(size_t) step] + 1;
            }
            else if (step < 0 && minor && pc == raisedSixthPc)
            {
                step = sixthStep;
                alter = alters[(size_t) step] + 1;
            }

            // 3) cromática: sustenidos em tons com #, bemóis em tons com b
            if (step < 0)
            {
                bool useSharp = fifths > 0;

                if (fifths == 0)
                    useSharp = (pc == 1 || pc == 6);   // C#, F#; demais como Eb, Ab, Bb

                for (int i = 0; i < 7 && step < 0; ++i)
                {
                    if (useSharp && (stepPc[i] + 1) % 12 == pc)        { step = i; alter = 1; }
                    else if (! useSharp && (stepPc[i] + 11) % 12 == pc) { step = i; alter = -1; }
                }
            }

            if (step < 0) { step = 0; alter = pc; }   // não deve acontecer

            Pitch p;
            p.midi = midi;
            p.step = stepNames[step];
            p.alter = alter;
            p.octave = (midi - alter) / 12 - 1;
            return p;
        }
    };

    /** Detecção de tonalidade (Krumhansl-Schmuckler). */
    void detectKey (const std::vector<QNote>& notes, int& fifths, bool& minor)
    {
        static const double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        // tonalidade preferida para cada tônica (maior): C Db D Eb E F F# G Ab A Bb B
        static const int majorFifths[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };

        double hist[12] = {};
        for (auto& n : notes)
            hist[n.pitch % 12] += std::max (1, n.end - n.start);

        double total = std::accumulate (std::begin (hist), std::end (hist), 0.0);
        fifths = 0; minor = false;

        if (total <= 0.0)
            return;

        auto correlate = [&] (const double* profile, int tonic)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += hist[(i + tonic) % 12]; my += profile[i]; }
            mx /= 12; my /= 12;
            double num = 0, dx = 0, dy = 0;
            for (int i = 0; i < 12; ++i)
            {
                auto x = hist[(i + tonic) % 12] - mx, y = profile[i] - my;
                num += x * y; dx += x * x; dy += y * y;
            }
            return (dx > 0 && dy > 0) ? num / std::sqrt (dx * dy) : -1.0;
        };

        double best = -2.0;
        for (int t = 0; t < 12; ++t)
        {
            auto ma = correlate (majorProfile, t);
            auto mi = correlate (minorProfile, t);

            if (ma > best) { best = ma; fifths = majorFifths[t]; minor = false; }
            if (mi > best) { best = mi; fifths = majorFifths[(t + 3) % 12]; minor = true; }
        }
    }

    //==============================================================================
    // Valores rítmicos

    struct NoteValue { int base = 0; int dots = 0; };

    bool representable (int d, int maxDots, NoteValue& out)
    {
        static const int bases[] = { 384, 192, 96, 48, 24, 12, 6, 3 };

        for (int b : bases)
        {
            if (d == b)                                 { out = { b, 0 }; return true; }
            if (maxDots >= 1 && b >= 6 && d * 2 == b * 3) { out = { b, 1 }; return true; }
            if (maxDots >= 2 && b >= 12 && d * 4 == b * 7) { out = { b, 2 }; return true; }
        }

        return false;
    }

    /** Divide um trecho [s, e) do compasso em figuras escrevíveis, respeitando a métrica. */
    void decompose (int s, int e, const Meter& m, bool isRest, const Settings& st,
                    std::vector<std::pair<int, int>>& out)
    {
        const int d = e - s;
        if (d <= 0)
            return;

        NoteValue nv;
        const bool rep = representable (d, st.maxDots, nv);

        auto allowed = [&]
        {
            if (! rep)
                return false;

            const bool rigid = isRest || st.syncopation == Settings::Syncopation::rigid;

            if (rigid)
            {
                if (d >= m.beat)
                {
                    if (s % m.beat != 0 || d % m.beat != 0)
                        return false;

                    const int half = m.measure / 2;
                    if (m.halfBar && s < half && e > half && s % half != 0)
                        return false;

                    return true;
                }

                return (s / m.beat) == ((e - 1) / m.beat);
            }

            // moderno (síncopes permitidas quando simétricas)
            const int half = std::max (1, nv.base / 2);
            return s % half == 0;
        };

        if (allowed())
        {
            out.push_back ({ s, d });
            return;
        }

        // procura a divisão métrica mais forte dentro do trecho
        std::vector<int> units;
        if (m.halfBar) units.push_back (m.measure / 2);
        units.push_back (m.beat);
        for (int u = m.compound ? m.beat / 3 : m.beat / 2; u >= 3; u /= 2)
            units.push_back (u);

        for (int u : units)
        {
            int b = (s / u + 1) * u;
            if (b > s && b < e)
            {
                decompose (s, b, m, isRest, st, out);
                decompose (b, e, m, isRest, st, out);
                return;
            }
        }

        // último recurso: maior figura que cabe
        static const int bases[] = { 384, 288, 192, 144, 96, 72, 48, 36, 24, 18, 12, 9, 6, 3 };
        for (int b : bases)
            if (b <= d)
            {
                out.push_back ({ s, b });
                decompose (s + b, e, m, isRest, st, out);
                return;
            }

        out.push_back ({ s, d });
    }

    //==============================================================================
    // Cifras

    struct ChordTemplate { std::vector<int> intervals; const char* kind; const char* text; double complexity; };

    const std::vector<ChordTemplate>& chordTemplates()
    {
        static const std::vector<ChordTemplate> t =
        {
            { { 0, 4, 7 },      "major",              "",     0.00 },
            { { 0, 3, 7 },      "minor",              "m",    0.00 },
            { { 0, 4, 7, 10 },  "dominant",           "7",    0.06 },
            { { 0, 4, 7, 11 },  "major-seventh",      "maj7", 0.06 },
            { { 0, 3, 7, 10 },  "minor-seventh",      "m7",   0.06 },
            { { 0, 3, 6 },      "diminished",         "dim",  0.10 },
            { { 0, 3, 6, 10 },  "half-diminished",    "m7(b5)", 0.10 },
            { { 0, 3, 6, 9 },   "diminished-seventh", "dim7", 0.12 },
            { { 0, 4, 8 },      "augmented",          "aug",  0.12 },
            { { 0, 5, 7 },      "suspended-fourth",   "sus4", 0.08 },
            { { 0, 2, 7 },      "suspended-second",   "sus2", 0.10 },
            { { 0, 4, 7, 9 },   "major-sixth",        "6",    0.10 },
            { { 0, 3, 7, 9 },   "minor-sixth",        "m6",   0.10 },
            { { 0, 5, 7, 10 },  "dominant",           "7sus4", 0.12 },
        };
        return t;
    }

    bool detectChord (const double (&w)[12], int bassPc, int& rootPc, const ChordTemplate*& best)
    {
        double total = 0; int distinct = 0;
        for (double x : w) { total += x; if (x > 0.0) ++distinct; }

        if (total <= 0 || distinct < 2)
            return false;

        double bestScore = -1e9;
        best = nullptr;

        for (int root = 0; root < 12; ++root)
        {
            for (auto& t : chordTemplates())
            {
                double in = 0, missing = 0;
                bool member[12] = {};

                for (int iv : t.intervals)
                {
                    const int pc = (root + iv) % 12;
                    member[pc] = true;
                    in += w[pc];
                    if (w[pc] <= 0.0) missing += 1.0;
                }

                double out = 0;
                for (int pc = 0; pc < 12; ++pc)
                    if (! member[pc]) out += w[pc];

                // a fundamental e a terça/quinta precisam existir
                if (w[root] <= 0.0 && root != bassPc)
                    continue;

                double score = (in - 0.8 * out) / total - 0.22 * missing - t.complexity;
                if (root == bassPc) score += 0.08;

                if (score > bestScore)
                {
                    bestScore = score;
                    rootPc = root;
                    best = &t;
                }
            }
        }

        return best != nullptr && bestScore > 0.25;
    }

    //==============================================================================
    // Construção de uma parte

    Part buildPart (const InputTrack& track, const Settings& st, const Meter& meter)
    {
        Part part;
        part.name = track.name;

        const int startGrid = std::max (3, st.startGrid);
        const int lengthGrid = st.lengthGrid > 0 ? st.lengthGrid : startGrid;

        // 1) filtra notas-fantasma
        std::vector<InputNote> input;
        for (auto& n : track.notes)
            if (n.lengthQ * DIV >= st.ignoreShorterThan && n.velocity >= st.minVelocity)
                input.push_back (n);

        std::sort (input.begin(), input.end(), [] (auto& a, auto& b) { return a.startQ < b.startQ; });

        // 2) ataques quase simultâneos (acorde "arpejado" sem querer) usam o mesmo ponto
        std::vector<double> anchoredStart (input.size());
        {
            size_t c = 0;
            for (size_t k = 0; k < input.size(); ++k)
            {
                if ((input[k].startQ - input[c].startQ) * DIV > st.chordTolerance)
                    c = k;
                anchoredStart[k] = input[c].startQ;
            }
        }

        std::vector<QNote> notes;
        std::vector<double> rawStart, rawEnd;

        for (size_t k = 0; k < input.size(); ++k)
        {
            QNote q;
            q.pitch = input[k].pitch;
            q.velocity = input[k].velocity;
            q.played = input[k].lengthQ * DIV;
            q.start = (int) std::lround (anchoredStart[k] * DIV);   // provisório (para agrupar)
            q.end = q.start + 1;
            notes.push_back (q);
            rawStart.push_back (anchoredStart[k] * DIV);
            rawEnd.push_back ((input[k].startQ + input[k].lengthQ) * DIV);
        }

        // 3) pautas (piano = duas pautas) — decidido antes da quantização
        bool piano = st.staves == Settings::Staves::piano;

        if (st.staves == Settings::Staves::automatic && ! notes.empty())
        {
            int lo = 127, hi = 0;
            for (auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
            piano = (hi - lo > 19 && lo < 57 && hi > 64);
        }

        part.staves = piano ? 2 : 1;

        if (piano)
        {
            if (st.split == Settings::Split::fixed)
            {
                for (auto& n : notes)
                    n.staff = n.pitch >= st.splitPoint ? 1 : 2;
            }
            else
            {
                // divisão inteligente: cada ataque é dividido entre as mãos
                double right = 72.0, left = 48.0;
                size_t i = 0;

                while (i < notes.size())
                {
                    size_t j = i;
                    while (j < notes.size() && notes[j].start == notes[i].start) ++j;

                    std::vector<QNote*> group;
                    for (size_t k = i; k < j; ++k) group.push_back (&notes[k]);
                    std::sort (group.begin(), group.end(), [] (auto x, auto y) { return x->pitch > y->pitch; });

                    double bestCost = 1e9; size_t bestSplit = 0;

                    for (size_t sp = 0; sp <= group.size(); ++sp)   // [0, sp) = mão direita
                    {
                        double cost = 0;
                        int rHi = -1, rLo = 200, lHi = -1, lLo = 200;

                        for (size_t k = 0; k < group.size(); ++k)
                        {
                            const int pch = group[k]->pitch;
                            if (k < sp) { cost += std::abs (pch - right); rHi = std::max (rHi, pch); rLo = std::min (rLo, pch); }
                            else        { cost += std::abs (pch - left);  lHi = std::max (lHi, pch); lLo = std::min (lLo, pch); }
                        }

                        if (rHi >= 0 && rHi - rLo > 14) cost += 40.0 * (rHi - rLo - 14);
                        if (lHi >= 0 && lHi - lLo > 14) cost += 40.0 * (lHi - lLo - 14);
                        if (rLo < 200 && rLo < st.splitPoint - 12) cost += 12.0;
                        if (lHi >= 0 && lHi > st.splitPoint + 12) cost += 12.0;

                        if (cost < bestCost) { bestCost = cost; bestSplit = sp; }
                    }

                    double rSum = 0, lSum = 0; int rN = 0, lN = 0;
                    for (size_t k = 0; k < group.size(); ++k)
                    {
                        group[k]->staff = k < bestSplit ? 1 : 2;
                        if (k < bestSplit) { rSum += group[k]->pitch; ++rN; } else { lSum += group[k]->pitch; ++lN; }
                    }

                    if (rN > 0) right = right * 0.6 + (rSum / rN) * 0.4;
                    if (lN > 0) left  = left  * 0.6 + (lSum / lN) * 0.4;
                    right = std::max (right, left + 7.0);

                    i = j;
                }
            }

            part.clefs[0] = "G";
            part.clefs[1] = "F";
        }
        else
        {
            std::vector<int> pitches;
            for (auto& n : notes) pitches.push_back (n.pitch);
            std::sort (pitches.begin(), pitches.end());
            const int median = pitches.empty() ? 60 : pitches[pitches.size() / 2];
            part.clefs[0] = median < 57 ? "F" : "G";
        }

        // reagrupa os acordes dentro de cada mão (uma mão não "puxa" a outra)
        for (int staff = 1; staff <= part.staves; ++staff)
        {
            std::vector<size_t> idx;
            for (size_t k = 0; k < notes.size(); ++k)
                if (notes[k].staff == staff)
                    idx.push_back (k);

            std::sort (idx.begin(), idx.end(), [&] (size_t x, size_t y) { return input[x].startQ < input[y].startQ; });

            size_t c = 0;
            for (size_t k = 0; k < idx.size(); ++k)
            {
                if ((input[idx[k]].startQ - input[idx[c]].startQ) * DIV > st.chordTolerance)
                    c = k;
                rawStart[idx[k]] = input[idx[c]].startQ * DIV;
            }
        }

        // 4) tercinas: decididas por tempo (semínima) e por pauta, como um músico leria
        std::map<int, int> windowGrids[2];

        if (st.autoTuplets && ! meter.compound)
        {
            const double factor = 0.6 + (100 - std::clamp (st.tupletSensitivity, 0, 100)) / 100.0 * 2.4;

            for (int staff = 1; staff <= part.staves; ++staff)
            {
                std::map<int, std::vector<double>> starts;
                for (size_t k = 0; k < notes.size(); ++k)
                    if (notes[k].staff == staff)
                        starts[(int) std::floor (rawStart[k] / DIV)].push_back (rawStart[k]);

                for (auto& [w, list] : starts)
                {
                    std::vector<double> inner;   // ignora o que cai praticamente no tempo
                    for (double pos : list)
                    {
                        const double rel = pos - w * DIV;
                        if (rel > 6.0 && rel < DIV - 6.0)
                            inner.push_back (pos);
                    }

                    std::sort (inner.begin(), inner.end());
                    inner.erase (std::unique (inner.begin(), inner.end()), inner.end());

                    if (inner.empty())
                        continue;

                    auto errorFor = [&] (int g)
                    {
                        double e = 0;
                        for (double pos : inner)
                        {
                            const double rel = pos - w * DIV;
                            e += std::abs (rel - std::round (rel / g) * g);
                        }
                        return e;
                    };

                    const double errStraight = errorFor (startGrid);
                    std::vector<int> candidates { 32 };
                    if (startGrid <= 24) candidates.push_back (16);

                    for (int tg : candidates)
                    {
                        const double errTriplet = errorFor (tg) + (tg == 16 ? 2.0 * (double) inner.size() : 0.0);

                        if (errTriplet * factor + 1.0 < errStraight && errStraight > 3.0 * (double) inner.size())
                        {
                            windowGrids[staff - 1][w] = tg;
                            break;
                        }
                    }
                }
            }
        }

        auto gridAt = [&] (int staff, int window, int straight)
        {
            auto& wg = windowGrids[staff - 1];
            auto it = wg.find (window);
            return it != wg.end() ? it->second : straight;
        };

        auto snap = [&] (int staff, double pos, int straight, double bias)
        {
            const int w = (int) std::floor (pos / DIV);
            const int g = gridAt (staff, w, straight);
            const double rel = pos - w * DIV;
            return w * DIV + (int) std::floor (rel / g + 0.5 - bias) * g;
        };

        const double endBias = std::clamp (st.releaseBias, 0, 50) / 100.0;

        // 5) quantiza (só para a leitura!)
        for (size_t k = 0; k < notes.size(); ++k)
        {
            auto& q = notes[k];
            q.start = std::max (0, snap (q.staff, rawStart[k], startGrid, 0.0));
            q.end = snap (q.staff, rawEnd[k], lengthGrid, endBias);

            if (q.end <= q.start)
                q.end = q.start + gridAt (q.staff, q.start / DIV, startGrid);
        }

        // duplicatas (mesma nota no mesmo ponto)
        std::sort (notes.begin(), notes.end(), [] (auto& x, auto& y)
        {
            return x.start != y.start ? x.start < y.start : x.pitch > y.pitch;
        });

        {
            std::vector<QNote> unique;
            for (auto& n : notes)
            {
                if (! unique.empty() && unique.back().start == n.start && unique.back().pitch == n.pitch)
                    unique.back().end = std::max (unique.back().end, n.end);
                else
                    unique.push_back (n);
            }
            notes.swap (unique);
        }

        // tonalidade
        part.keyFifths = st.keyFifths;
        part.minor = st.keyMinor;
        if (st.autoKey)
            detectKey (notes, part.keyFifths, part.minor);

        // 6) vozes por pauta
        std::vector<std::vector<Event>> voices;   // índice = (staff-1)*2 + (voz-1)
        voices.resize (4);

        for (int staff = 1; staff <= part.staves; ++staff)
        {
            std::vector<QNote> sn;
            for (auto& n : notes)
                if (n.staff == staff)
                    sn.push_back (n);

            size_t i = 0;
            int voiceEnd[2] = { 0, 0 };
            int voiceStart[2] = { -100000, -100000 };

            // uma voz está "livre" se acabou, ou se só invade o novo ataque por legato
            auto isFree = [&] (int v, int start)
            {
                const int overlap = voiceEnd[v] - start;
                return overlap <= 0 || (overlap <= lengthGrid && overlap < (start - voiceStart[v]));
            };
            auto& v1 = voices[(size_t) (staff - 1) * 2];
            auto& v2 = voices[(size_t) (staff - 1) * 2 + 1];

            while (i < sn.size())
            {
                size_t j = i;
                while (j < sn.size() && sn[j].start == sn[i].start) ++j;
                const int start = sn[i].start;

                // agrupa por final parecido
                std::vector<Event> subs;
                std::vector<QNote> group (sn.begin() + (long) i, sn.begin() + (long) j);
                std::sort (group.begin(), group.end(), [] (auto& a, auto& b) { return a.end > b.end; });

                for (auto& n : group)
                {
                    bool placed = false;
                    for (auto& sub : subs)
                    {
                        if (st.polyphony == Settings::Polyphony::chords || std::abs (sub.end - n.end) <= lengthGrid)
                        {
                            sub.notes.push_back (n);
                            sub.end = std::max (sub.end, n.end);
                            placed = true;
                            break;
                        }
                    }

                    if (! placed)
                        subs.push_back ({ start, n.end, { n } });
                }

                // o grupo com a nota mais aguda é o principal (voz 1)
                std::sort (subs.begin(), subs.end(), [] (auto& a, auto& b)
                {
                    int ha = 0, hb = 0;
                    for (auto& n : a.notes) ha = std::max (ha, n.pitch);
                    for (auto& n : b.notes) hb = std::max (hb, n.pitch);
                    return ha > hb;
                });

                for (size_t k = 0; k < subs.size(); ++k)
                {
                    auto& sub = subs[k];
                    const bool primary = (k == 0);
                    int target = -1;

                    if (st.polyphony == Settings::Polyphony::chords)
                    {
                        target = 0;
                    }
                    else if (primary)
                    {
                        if (isFree (0, start))      target = 0;
                        else if (isFree (1, start)) target = 1;
                        else                        target = 0;
                    }
                    else
                    {
                        if (isFree (1, start))      target = 1;
                        else if (isFree (0, start)) target = 0;
                        else                        target = 1;
                    }

                    auto& vv = target == 0 ? v1 : v2;

                    if (! vv.empty() && vv.back().start == start)
                    {
                        // mesmo ataque na mesma voz: vira acorde
                        for (auto& n : sub.notes) vv.back().notes.push_back (n);
                        vv.back().end = std::max (vv.back().end, sub.end);
                    }
                    else
                    {
                        vv.push_back (sub);
                    }

                    if (voiceStart[target] != start)
                        voiceEnd[target] = sub.end;
                    else
                        voiceEnd[target] = std::max (voiceEnd[target], sub.end);
                    voiceStart[target] = start;
                }

                i = j;
            }

            // 7) legato: sem sobreposição e fechando pausas pequenas
            for (auto* vv : { &v1, &v2 })
            {
                for (size_t k = 0; k + 1 < vv->size(); ++k)
                {
                    auto& cur = (*vv)[k];
                    auto& next = (*vv)[k + 1];

                    if (cur.end > next.start)
                        cur.end = next.start;   // uma voz não pode se sobrepor

                    const int gap = next.start - cur.end;
                    const int ioi = next.start - cur.start;

                    double played = 0;
                    for (auto& n : cur.notes) played = std::max (played, n.played);

                    // nota curta seguida de perto por outra = staccato (escreve o valor inteiro)
                    const bool shortTouch = st.detectStaccato && ioi <= DIV
                                             && played < ioi * st.staccatoPercent / 100.0;

                    if (gap > 0 && (gap < st.fillGaps || shortTouch))
                        cur.end = next.start;
                }

                for (auto& ev : *vv)
                    for (auto& n : ev.notes)
                        n.end = ev.end;
            }

            // a voz 1 (hastes para cima) deve ser a mais aguda
            if (! v2.empty() && ! v1.empty())
            {
                auto mean = [] (const std::vector<Event>& vv)
                {
                    double sum = 0; int n = 0;
                    for (auto& ev : vv) for (auto& x : ev.notes) { sum += x.pitch; ++n; }
                    return n > 0 ? sum / n : 0.0;
                };

                if (mean (v2) > mean (v1))
                    std::swap (v1, v2);
            }
        }

        // 8) articulações deduzidas
        for (auto& vv : voices)
        {
            for (auto& ev : vv)
            {
                const int written = ev.end - ev.start;
                double maxPlayed = 0; int maxVel = 0;

                for (auto& n : ev.notes) { maxPlayed = std::max (maxPlayed, n.played); maxVel = std::max (maxVel, n.velocity); }

                const bool stacc = st.detectStaccato && written <= DIV && written >= 24
                                     && maxPlayed < written * st.staccatoPercent / 100.0;
                const bool acc = st.detectAccents && maxVel >= st.accentVelocity;

                for (auto& n : ev.notes) { n.staccato = stacc; n.accent = acc; }
            }
        }

        // 9) compassos
        int lastEnd = 0;
        for (auto& vv : voices)
            for (auto& ev : vv)
                lastEnd = std::max (lastEnd, ev.end);

        const int numMeasures = std::max (1, (lastEnd + meter.measure - 1) / meter.measure);
        part.measures.resize ((size_t) numMeasures);

        Speller speller (part.keyFifths, part.minor);

        for (int mi = 0; mi < numMeasures; ++mi)
        {
            auto& measure = part.measures[(size_t) mi];
            measure.number = mi + 1;
            measure.length = meter.measure;
            const int mStart = mi * meter.measure;
            const int mEnd = mStart + meter.measure;

            for (int vi = 0; vi < 4; ++vi)
            {
                const int staff = vi / 2 + 1;
                if (staff > part.staves)
                    continue;

                // segmentos desta voz neste compasso
                struct Seg { int s, e; const Event* ev; bool tieIn, tieOut; };
                std::vector<Seg> segs;

                for (auto& ev : voices[(size_t) vi])
                {
                    if (ev.end <= mStart || ev.start >= mEnd)
                        continue;

                    Seg sg;
                    sg.s = std::max (ev.start, mStart) - mStart;
                    sg.e = std::min (ev.end, mEnd) - mStart;
                    sg.ev = &ev;
                    sg.tieIn = ev.start < mStart;
                    sg.tieOut = ev.end > mEnd;
                    segs.push_back (sg);
                }

                const bool isMainVoice = (vi % 2 == 0);
                if (segs.empty() && ! isMainVoice)
                    continue;

                Voice voice;
                voice.staff = staff;
                voice.number = (staff - 1) * 4 + (vi % 2) + 1;

                if (segs.empty())
                {
                    Element rest;
                    rest.start = 0;
                    rest.duration = meter.measure;
                    rest.wholeMeasureRest = true;
                    rest.typeTicks = 384;
                    voice.elements.push_back (rest);
                    measure.voices.push_back (voice);
                    continue;
                }

                const auto& windowGrid = windowGrids[staff - 1];

                // preenche com pausas e divide em figuras
                int pos = 0;
                std::vector<Seg> filled;
                for (auto& sg : segs)
                {
                    if (sg.s > pos) filled.push_back ({ pos, sg.s, nullptr, false, false });
                    filled.push_back (sg);
                    pos = sg.e;
                }
                if (pos < meter.measure) filled.push_back ({ pos, meter.measure, nullptr, false, false });

                for (auto& sg : filled)
                {
                    const bool isRest = sg.ev == nullptr;

                    // corta só onde o trecho começa/termina dentro de um tempo com tercina
                    std::vector<int> cuts { sg.s };
                    {
                        const int ws = (mStart + sg.s) / DIV, we = (mStart + sg.e) / DIV;
                        const int cutA = (sg.s % DIV != 0 && windowGrid.count (ws)) ? (sg.s / DIV + 1) * DIV : -1;
                        const int cutB = (sg.e % DIV != 0 && windowGrid.count (we)) ? (sg.e / DIV) * DIV : -1;

                        if (cutA > sg.s && cutA < sg.e) cuts.push_back (cutA);
                        if (cutB > sg.s && cutB < sg.e && cutB != cutA) cuts.push_back (cutB);
                    }
                    cuts.push_back (sg.e);
                    std::sort (cuts.begin(), cuts.end());

                    std::vector<std::pair<int, int>> pieces;

                    for (size_t c = 0; c + 1 < cuts.size(); ++c)
                    {
                        const int a = cuts[c], b = cuts[c + 1];
                        const int w = (mStart + a) / DIV;
                        const int wEnd = (w + 1) * DIV - mStart;
                        const bool tripletBeat = windowGrid.count (w) && b <= wEnd
                                                 && ! (a % DIV == 0 && b == wEnd);

                        if (tripletBeat)
                        {
                            // dentro de uma tercina: figuras em "tempo de tercina" (x 3/2)
                            int p = a;
                            while (p < b)
                            {
                                int remain = (b - p) * 3 / 2;
                                static const int vals[] = { 144, 96, 72, 48, 36, 24, 12 };
                                int chosen = 0;
                                for (int v : vals)
                                    if (v <= remain && (v * 2) % 3 == 0) { chosen = v; break; }
                                if (chosen == 0) chosen = remain;
                                const int real = std::max (1, chosen * 2 / 3);
                                pieces.push_back ({ p, -real });   // negativo = tercina
                                p += real;
                            }
                        }
                        else
                        {
                            decompose (a, b, meter, isRest, st, pieces);
                        }
                    }

                    for (size_t k = 0; k < pieces.size(); ++k)
                    {
                        Element el;
                        el.start = pieces[k].first;
                        const bool trip = pieces[k].second < 0;
                        el.duration = std::abs (pieces[k].second);
                        el.triplet = trip;

                        NoteValue nv;
                        const int displayTicks = trip ? el.duration * 3 / 2 : el.duration;
                        if (representable (displayTicks, std::max (st.maxDots, 1), nv))
                        {
                            el.typeTicks = nv.base;
                            el.dots = nv.dots;
                        }
                        else
                        {
                            el.typeTicks = displayTicks;
                        }

                        if (! isRest)
                        {
                            for (auto& n : sg.ev->notes)
                            {
                                Pitch p = speller.spell (n.pitch);
                                p.tieStop  = (k > 0) || (k == 0 && sg.tieIn);
                                p.tieStart = (k + 1 < pieces.size()) || (k + 1 == pieces.size() && sg.tieOut);
                                el.pitches.push_back (p);
                            }

                            std::sort (el.pitches.begin(), el.pitches.end(), [] (auto& a, auto& b) { return a.midi < b.midi; });

                            // articulação só no primeiro pedaço
                            if (k == 0 && ! sg.tieIn)
                            {
                                el.staccato = sg.ev->notes.front().staccato && pieces.size() == 1;
                                el.accent = sg.ev->notes.front().accent;
                            }
                        }

                        voice.elements.push_back (el);
                    }
                }

                // pausa de compasso inteiro
                if (voice.elements.size() == 1 && voice.elements[0].isRest() && voice.elements[0].duration == meter.measure)
                {
                    voice.elements[0].wholeMeasureRest = true;
                    voice.elements[0].typeTicks = 384;
                }

                // grupos de tercina
                for (size_t k = 0; k < voice.elements.size(); ++k)
                {
                    auto& el = voice.elements[k];
                    if (! el.triplet) continue;

                    const int w = el.start / DIV;
                    const bool first = (k == 0) || ! voice.elements[k - 1].triplet || voice.elements[k - 1].start / DIV != w;
                    const bool last = (k + 1 == voice.elements.size()) || ! voice.elements[k + 1].triplet
                                      || voice.elements[k + 1].start / DIV != w;
                    el.tupletStart = first;
                    el.tupletStop = last;
                }

                // barras (beams) por tempo
                const int beamUnit = meter.compound ? meter.beat : std::min (meter.beat, DIV);
                auto flags = [] (const Element& e)
                {
                    if (e.isRest() || e.typeTicks > 48) return 0;
                    if (e.typeTicks > 24) return 1;
                    if (e.typeTicks > 12) return 2;
                    if (e.typeTicks > 6)  return 3;
                    return 4;
                };

                size_t k = 0;
                while (k < voice.elements.size())
                {
                    if (flags (voice.elements[k]) == 0) { ++k; continue; }

                    const int w = voice.elements[k].start / beamUnit;
                    size_t j = k;
                    while (j < voice.elements.size() && flags (voice.elements[j]) > 0
                           && voice.elements[j].start / beamUnit == w)
                        ++j;

                    if (j - k >= 2)
                    {
                        for (size_t x = k; x < j; ++x)
                        {
                            auto& el = voice.elements[x];
                            const int f = flags (el);
                            for (int level = 1; level <= f; ++level)
                            {
                                const bool prev = x > k && flags (voice.elements[x - 1]) >= level;
                                const bool next = x + 1 < j && flags (voice.elements[x + 1]) >= level;

                                if (prev && next)      el.beams.push_back ("continue");
                                else if (prev)         el.beams.push_back ("end");
                                else if (next)         el.beams.push_back ("begin");
                                else                   el.beams.push_back (x == k ? "forward hook" : "backward hook");
                            }
                        }
                    }

                    k = j;
                }

                measure.voices.push_back (voice);
            }

            // acidentes por pauta (estado dentro do compasso)
            const auto ka = keyAlters (part.keyFifths);
            for (int staff = 1; staff <= part.staves; ++staff)
            {
                std::vector<Element*> els;
                for (auto& v : measure.voices)
                    if (v.staff == staff)
                        for (auto& e : v.elements)
                            if (! e.isRest())
                                els.push_back (&e);

                std::stable_sort (els.begin(), els.end(), [] (auto a, auto b) { return a->start < b->start; });
                std::map<int, int> state;   // (step*10 + oitava) -> alteração vigente

                for (auto* e : els)
                {
                    for (auto& p : e->pitches)
                    {
                        const int key = stepIndex (p.step) * 16 + p.octave;
                        const int current = state.count (key) ? state[key] : ka[(size_t) stepIndex (p.step)];

                        if (p.tieStop)
                        {
                            state[key] = p.alter;
                            continue;
                        }

                        if (p.alter != current)
                        {
                            p.showAccidental = true;
                            p.accidental = p.alter;
                        }

                        state[key] = p.alter;
                    }
                }
            }
        }

        // 10) cifras
        if (st.chordSymbols && ! track.isDrums)
        {
            const int window = st.chordRate == Settings::ChordRate::bar     ? meter.measure
                             : st.chordRate == Settings::ChordRate::halfBar ? (meter.halfBar ? meter.measure / 2 : meter.measure)
                                                                            : meter.beat;
            std::string lastChord;

            for (int t = 0; t < numMeasures * meter.measure; t += window)
            {
                double w[12] = {};
                int bass = 200;

                for (auto& vv : voices)
                    for (auto& ev : vv)
                    {
                        const int ov = std::min (ev.end, t + window) - std::max (ev.start, t);
                        if (ov <= 0) continue;

                        // baixo e notas longas pesam mais que notas de passagem
                        const double lengthWeight = (ev.end - ev.start) >= DIV ? 1.0 : 0.65;

                        for (auto& n : ev.notes)
                        {
                            w[n.pitch % 12] += ov * lengthWeight * (n.pitch < 60 ? 1.4 : 1.0);
                            bass = std::min (bass, n.pitch);
                        }
                    }

                int root = 0; const ChordTemplate* tmpl = nullptr;
                if (bass == 200 || ! detectChord (w, bass % 12, root, tmpl))
                    continue;

                // grafia da fundamental e do baixo pela tonalidade
                Pitch rp = speller.spell (60 + root);
                Harmony h;
                h.position = t % meter.measure;
                h.rootStep = rp.step;
                h.rootAlter = rp.alter;
                h.kind = tmpl->kind;
                h.text = tmpl->text;

                if (bass % 12 != root)
                {
                    Pitch bp = speller.spell (bass);
                    h.hasBass = true;
                    h.bassStep = bp.step;
                    h.bassAlter = bp.alter;
                }

                const std::string id = std::string (1, h.rootStep) + std::to_string (h.rootAlter) + h.kind
                                       + (h.hasBass ? std::string (1, h.bassStep) + std::to_string (h.bassAlter) : "");

                // repete a cifra no início de cada compasso? não: só quando muda
                if (id == lastChord)
                    continue;

                lastChord = id;
                part.measures[(size_t) (t / meter.measure)].harmonies.push_back (h);
            }
        }

        return part;
    }
}

//==============================================================================
Score buildScore (const std::vector<InputTrack>& tracks, const std::vector<Settings>& settings,
                  int beats, int beatType, double bpm, const std::string& title)
{
    Score sc;
    sc.title = title;
    sc.beats = beats;
    sc.beatType = beatType;
    sc.bpm = bpm;

    const Meter meter (beats, beatType);

    for (size_t i = 0; i < tracks.size(); ++i)
        sc.parts.push_back (buildPart (tracks[i], i < settings.size() ? settings[i] : Settings(), meter));

    // todas as partes com o mesmo número de compassos
    size_t maxMeasures = 0;
    for (auto& p : sc.parts) maxMeasures = std::max (maxMeasures, p.measures.size());

    for (auto& p : sc.parts)
    {
        while (p.measures.size() < maxMeasures)
        {
            Measure m;
            m.number = (int) p.measures.size() + 1;
            m.length = meter.measure;

            for (int staff = 1; staff <= p.staves; ++staff)
            {
                Voice v;
                v.staff = staff;
                v.number = (staff - 1) * 4 + 1;
                Element rest;
                rest.duration = meter.measure;
                rest.wholeMeasureRest = true;
                rest.typeTicks = 384;
                v.elements.push_back (rest);
                m.voices.push_back (v);
            }

            p.measures.push_back (m);
        }
    }

    return sc;
}
}
