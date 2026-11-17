#pragma once

// Motor de partitura do Z-Studio.
// Converte MIDI tocado de forma humana numa partitura limpa, SEM alterar o MIDI:
// toda a "limpeza" é apenas uma interpretação visual controlada pelos ajustes abaixo.
// Este módulo usa só a biblioteca padrão de C++ (testável fora do app).

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace score
{
    /** Resolução interna: tiques por semínima (permite fusas e tercinas de semicolcheia). */
    constexpr int DIV = 96;

    //==============================================================================
    /** Nota MIDI como foi tocada (tempos em semínimas desde o início do projeto). */
    struct InputNote
    {
        int pitch = 60;
        double startQ = 0.0;
        double lengthQ = 1.0;
        int velocity = 100;
        int id = 0;
    };

    struct InputTrack
    {
        std::string name;
        std::vector<InputNote> notes;
        bool isDrums = false;
    };

    //==============================================================================
    /** Ajustes de limpeza (o painel "Limpeza do MIDI"). */
    struct Settings
    {
        // Quantização visual (em tiques; DIV = semínima)
        int startGrid = 24;            // grade dos inícios (24 = semicolcheia)
        int lengthGrid = 0;            // grade das durações (0 = igual à dos inícios)

        // Tercinas
        bool autoTuplets = true;
        int tupletSensitivity = 50;    // 0..100 (maior = detecta tercinas com mais facilidade)

        // Notas atacadas quase juntas viram um só acorde (tiques; humanos nunca tocam exatamente juntos)
        int chordTolerance = 10;

        // Finais: legato que "passa um pouquinho" do tempo é arredondado para baixo
        int releaseBias = 35;          // 0..50 (%): quanto do próximo passo da grade o final precisa invadir

        // Filtros de "sujeira"
        int ignoreShorterThan = 6;     // ignora notas menores que isso (tiques) — notas-fantasma
        int minVelocity = 0;           // ignora notas mais fracas que isso

        // Legato / pausas
        bool noOverlap = true;         // corta sobreposições na mesma voz
        int fillGaps = 24;             // fecha pausas menores que isso (tiques); 0 = nunca

        // Polifonia e pautas
        enum class Polyphony { chords, autoVoices };
        Polyphony polyphony = Polyphony::autoVoices;

        enum class Staves { automatic, single, piano };
        Staves staves = Staves::automatic;

        enum class Split { smart, fixed };
        Split split = Split::smart;
        int splitPoint = 60;           // Dó central

        // Tonalidade e grafia
        bool autoKey = true;
        int keyFifths = 0;             // -7..7 (usado quando autoKey = false)
        bool keyMinor = false;

        // Cifras
        bool chordSymbols = true;
        enum class ChordRate { bar, halfBar, beat };
        ChordRate chordRate = ChordRate::halfBar;

        // Escrita rítmica
        enum class Syncopation { modern, rigid };
        Syncopation syncopation = Syncopation::modern;
        int maxDots = 1;

        // Articulações deduzidas da execução
        bool detectStaccato = true;
        int staccatoPercent = 50;      // nota tocada com menos de X% da duração escrita => staccato
        bool detectAccents = false;
        int accentVelocity = 112;
    };

    /** Valores predefinidos (pontos de partida rápidos). */
    enum class Preset { balanced, piano, melody, rubato, strict };
    Settings makePreset (Preset);

    //==============================================================================
    // Partitura resultante

    struct Pitch
    {
        int midi = 60;
        char step = 'C';      // C D E F G A B
        int alter = 0;        // -2..2
        int octave = 4;
        bool showAccidental = false;
        int accidental = 0;   // valor do acidente a mostrar (0 = bequadro)
        bool tieStart = false, tieStop = false;
    };

    struct Element                     // nota, acorde ou pausa dentro de uma voz
    {
        int start = 0;                 // tiques desde o início do compasso
        int duration = 0;              // tiques reais
        std::vector<Pitch> pitches;    // vazio = pausa
        int typeTicks = DIV;           // figura base (384 semibreve ... 6 semifusa)
        int dots = 0;
        bool triplet = false;
        bool tupletStart = false, tupletStop = false;
        bool wholeMeasureRest = false;
        bool hidden = false;
        bool staccato = false, accent = false;
        std::vector<std::string> beams;   // por nível: "begin", "continue", "end", "forward hook", "backward hook"

        bool isRest() const     { return pitches.empty(); }
    };

    struct Voice
    {
        int number = 1;   // 1..4
        int staff = 1;
        std::vector<Element> elements;
    };

    struct Harmony
    {
        int position = 0;              // tiques no compasso
        char rootStep = 'C'; int rootAlter = 0;
        std::string kind = "major";    // valores do MusicXML (major, minor, dominant...)
        std::string text;              // texto exibido (ex.: "m7", "7", "maj7")
        bool hasBass = false; char bassStep = 'C'; int bassAlter = 0;
    };

    struct Measure
    {
        int number = 1;
        int length = 4 * DIV;
        std::vector<Voice> voices;
        std::vector<Harmony> harmonies;
    };

    struct Part
    {
        std::string name;
        int staves = 1;
        std::string clefs[2] = { "G", "F" };
        int keyFifths = 0;
        bool minor = false;
        std::vector<Measure> measures;
    };

    struct Score
    {
        std::string title;
        int beats = 4, beatType = 4;
        double bpm = 120.0;
        std::vector<Part> parts;
    };

    //==============================================================================
    /** Converte as faixas MIDI em partitura usando os ajustes (um por faixa). */
    Score buildScore (const std::vector<InputTrack>& tracks,
                      const std::vector<Settings>& settingsPerTrack,
                      int beats, int beatType, double bpm,
                      const std::string& title);

    /** Gera o MusicXML 4.0 (partwise) da partitura. */
    std::string toMusicXML (const Score&);

    /** Nome legível de uma grade (ex.: 24 -> "1/16"). */
    std::string gridName (int ticks);
}
