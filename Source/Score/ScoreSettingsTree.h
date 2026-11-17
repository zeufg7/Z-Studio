#pragma once

#include <JuceHeader.h>
#include "ScoreModel.h"

// Guarda os ajustes de limpeza dentro do projeto (.zstudio):
//  - no projeto (padrão para todas as faixas)
//  - em cada faixa (quando a faixa tem ajustes próprios)
namespace ScoreSettingsTree
{
    inline const juce::Identifier node ("ZSSCORE");
    inline const juce::Identifier overrideFlag ("override");

    inline void write (juce::ValueTree& v, const score::Settings& s, juce::UndoManager* um)
    {
        v.setProperty ("startGrid", s.startGrid, um);
        v.setProperty ("lengthGrid", s.lengthGrid, um);
        v.setProperty ("autoTuplets", s.autoTuplets, um);
        v.setProperty ("tupletSensitivity", s.tupletSensitivity, um);
        v.setProperty ("chordTolerance", s.chordTolerance, um);
        v.setProperty ("releaseBias", s.releaseBias, um);
        v.setProperty ("ignoreShorterThan", s.ignoreShorterThan, um);
        v.setProperty ("minVelocity", s.minVelocity, um);
        v.setProperty ("noOverlap", s.noOverlap, um);
        v.setProperty ("fillGaps", s.fillGaps, um);
        v.setProperty ("polyphony", (int) s.polyphony, um);
        v.setProperty ("staves", (int) s.staves, um);
        v.setProperty ("split", (int) s.split, um);
        v.setProperty ("splitPoint", s.splitPoint, um);
        v.setProperty ("autoKey", s.autoKey, um);
        v.setProperty ("keyFifths", s.keyFifths, um);
        v.setProperty ("keyMinor", s.keyMinor, um);
        v.setProperty ("chordSymbols", s.chordSymbols, um);
        v.setProperty ("chordRate", (int) s.chordRate, um);
        v.setProperty ("syncopation", (int) s.syncopation, um);
        v.setProperty ("maxDots", s.maxDots, um);
        v.setProperty ("detectStaccato", s.detectStaccato, um);
        v.setProperty ("staccatoPercent", s.staccatoPercent, um);
        v.setProperty ("detectAccents", s.detectAccents, um);
        v.setProperty ("accentVelocity", s.accentVelocity, um);
    }

    inline score::Settings read (const juce::ValueTree& v)
    {
        score::Settings s;
        auto get = [&] (const char* name, auto def) { return v.hasProperty (name) ? (decltype (def)) v[name] : def; };

        s.startGrid          = get ("startGrid", s.startGrid);
        s.lengthGrid         = get ("lengthGrid", s.lengthGrid);
        s.autoTuplets        = get ("autoTuplets", s.autoTuplets);
        s.tupletSensitivity  = get ("tupletSensitivity", s.tupletSensitivity);
        s.chordTolerance     = get ("chordTolerance", s.chordTolerance);
        s.releaseBias        = get ("releaseBias", s.releaseBias);
        s.ignoreShorterThan  = get ("ignoreShorterThan", s.ignoreShorterThan);
        s.minVelocity        = get ("minVelocity", s.minVelocity);
        s.noOverlap          = get ("noOverlap", s.noOverlap);
        s.fillGaps           = get ("fillGaps", s.fillGaps);
        s.polyphony          = (score::Settings::Polyphony) get ("polyphony", (int) s.polyphony);
        s.staves             = (score::Settings::Staves) get ("staves", (int) s.staves);
        s.split              = (score::Settings::Split) get ("split", (int) s.split);
        s.splitPoint         = get ("splitPoint", s.splitPoint);
        s.autoKey            = get ("autoKey", s.autoKey);
        s.keyFifths          = get ("keyFifths", s.keyFifths);
        s.keyMinor           = get ("keyMinor", s.keyMinor);
        s.chordSymbols       = get ("chordSymbols", s.chordSymbols);
        s.chordRate          = (score::Settings::ChordRate) get ("chordRate", (int) s.chordRate);
        s.syncopation        = (score::Settings::Syncopation) get ("syncopation", (int) s.syncopation);
        s.maxDots            = get ("maxDots", s.maxDots);
        s.detectStaccato     = get ("detectStaccato", s.detectStaccato);
        s.staccatoPercent    = get ("staccatoPercent", s.staccatoPercent);
        s.detectAccents      = get ("detectAccents", s.detectAccents);
        s.accentVelocity     = get ("accentVelocity", s.accentVelocity);
        return s;
    }

    /** Ajustes efetivos de uma faixa: os dela (se tiver) ou os do projeto. */
    inline score::Settings resolve (const juce::ValueTree& editState, const juce::ValueTree& trackState)
    {
        auto t = trackState.getChildWithName (node);
        if (t.isValid() && (bool) t[overrideFlag])
            return read (t);

        return read (editState.getChildWithName (node));
    }
}
