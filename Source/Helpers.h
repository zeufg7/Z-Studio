#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace te = tracktion;
using namespace std::literals;

/** Texto com acentos (UTF-8) para a interface: "Opções"_t */
inline juce::String operator""_t (const char* s, std::size_t n)   { return juce::String::fromUTF8 (s, (int) n); }

//==============================================================================
namespace ZS
{
    // Propriedades próprias salvas dentro do projeto
    inline const juce::Identifier trackKind ("zsKind");   // "instrument" | "audio"

    enum class TrackKind { audio, instrument };

    inline TrackKind getKind (const te::Track& t)
    {
        return t.state.getProperty (trackKind).toString() == "instrument" ? TrackKind::instrument
                                                                          : TrackKind::audio;
    }

    inline bool isInstrumentTrack (const te::Track& t)   { return getKind (t) == TrackKind::instrument; }

    inline const juce::String fileExtension = ".zstudio";
    inline const juce::String fileWildcard  = "*.zstudio;*.tracktionedit";

    inline juce::File getDefaultProjectsFolder()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("Z-Studio").getChildFile ("Projetos");
        d.createDirectory();
        return d;
    }

    //==============================================================================
    inline juce::Array<te::AudioTrack*> getUserTracks (te::Edit& edit)
    {
        return te::getAudioTracks (edit);
    }

    /** Instrumento da faixa (primeiro plugin que é sintetizador). */
    inline te::Plugin* getInstrument (te::AudioTrack& t)
    {
        for (auto p : t.pluginList)
            if (p->isSynth())
                return p;

        return nullptr;
    }

    /** Plugins de efeito (inserts), sem o volume/pan e o medidor internos. */
    inline juce::Array<te::Plugin*> getInserts (te::AudioTrack& t)
    {
        juce::Array<te::Plugin*> result;

        for (auto p : t.pluginList)
            if (! p->isSynth()
                && dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr
                && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr)
                result.add (p);

        return result;
    }

    /** Posição para inserir um efeito: antes do volume/pan da faixa. */
    inline int getInsertIndex (te::AudioTrack& t)
    {
        int i = 0;

        for (auto p : t.pluginList)
        {
            if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr)
                return i;

            ++i;
        }

        return -1;
    }

    inline te::Plugin::Ptr createBuiltIn (te::Edit& edit, const juce::String& xmlType)
    {
        juce::PluginDescription d;
        d.pluginFormatName = te::PluginManager::builtInPluginFormatName;
        d.fileOrIdentifier = xmlType;
        return edit.getPluginCache().createNewPlugin (xmlType, d);
    }

    /** Troca (ou define) o instrumento da faixa. */
    inline void setInstrument (te::AudioTrack& t, te::Plugin::Ptr newInstrument)
    {
        if (newInstrument == nullptr)
            return;

        if (auto old = getInstrument (t))
            old->deleteFromParent();

        t.pluginList.insertPlugin (newInstrument, 0, nullptr);
    }

    //==============================================================================
    // Entradas MIDI e de áudio

    inline void enableAllMidiInputs (te::Engine& engine)
    {
        for (auto& midiIn : engine.getDeviceManager().getMidiInDevices())
        {
            midiIn->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            midiIn->setEnabled (true);
        }
    }

    /** Estilo Logic: todas as entradas MIDI tocam a faixa de instrumento selecionada. */
    inline void routeMidiInputsTo (te::AudioTrack& track)
    {
        auto& edit = track.edit;

        for (auto instance : edit.getAllInputDevices())
        {
            if (instance->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice)
                continue;

            for (auto target : instance->getTargets())
                if (target != track.itemID)
                    [[maybe_unused]] auto r = instance->removeTarget (target, nullptr);

            if (! instance->getTargets().contains (track.itemID))
                [[maybe_unused]] auto r = instance->setTarget (track.itemID, true, nullptr, 0);

            instance->setRecordingEnabled (track.itemID, true);
        }
    }

    inline juce::Array<te::InputDeviceInstance*> getInputsTargeting (te::AudioTrack& t)
    {
        juce::Array<te::InputDeviceInstance*> result;

        for (auto instance : t.edit.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0))
                result.add (instance);

        return result;
    }

    inline bool isTrackArmed (te::AudioTrack& t)
    {
        for (auto instance : getInputsTargeting (t))
            if (instance->isRecordingEnabled (t.itemID))
                return true;

        return false;
    }

    inline void armTrack (te::AudioTrack& t, bool arm)
    {
        for (auto instance : getInputsTargeting (t))
            instance->setRecordingEnabled (t.itemID, arm);
    }

    /** Impede que cliques em botões "roubem" o foco do teclado (Espaço = tocar, como numa DAW). */
    inline void disableFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);

        for (auto* child : c.getChildren())
            disableFocus (*child);
    }

    //==============================================================================
    inline juce::String formatBarsBeats (te::Edit& edit, te::TimePosition t)
    {
        auto bb = edit.tempoSequence.toBarsAndBeats (t);
        auto beats = bb.beats.inBeats();
        auto wholeBeat = (int) std::floor (beats);
        auto ticks = (int) ((beats - wholeBeat) * 960.0);
        return juce::String::formatted ("%3d . %d . %03d", bb.bars + 1, wholeBeat + 1, ticks);
    }

    inline juce::String formatTime (te::TimePosition t)
    {
        auto ms = juce::roundToInt (t.inSeconds() * 1000.0);
        return juce::String::formatted ("%02d:%02d.%03d", (ms / 60000), (ms / 1000) % 60, ms % 1000);
    }
}

//==============================================================================
// Menu de plugins (internos + VST3/AU escaneados)
namespace PluginMenus
{
    struct Entry
    {
        juce::String xmlType;
        juce::PluginDescription desc;
    };

    inline te::Plugin::Ptr create (te::Edit& edit, const Entry& e)
    {
        return edit.getPluginCache().createNewPlugin (e.xmlType, e.desc);
    }

    inline Entry builtIn (const juce::String& xmlType, const juce::String& name, bool synth)
    {
        Entry e;
        e.xmlType = xmlType;
        e.desc.name = name;
        e.desc.fileOrIdentifier = xmlType;
        e.desc.pluginFormatName = te::PluginManager::builtInPluginFormatName;
        e.desc.category = xmlType;
        e.desc.isInstrument = synth;
        return e;
    }

    /** Mostra um menu e chama onChosen com o plugin escolhido. */
    inline void show (te::Edit& edit, bool instruments, juce::Component* target,
                      std::function<void (te::Plugin::Ptr)> onChosen)
    {
        auto entries = std::make_shared<std::vector<Entry>>();
        juce::PopupMenu menu, builtInMenu;

        auto addBuiltIn = [&] (const juce::String& type, const juce::String& name, bool synth)
        {
            entries->push_back (builtIn (type, name, synth));
            builtInMenu.addItem ((int) entries->size(), name);
        };

        if (instruments)
        {
            addBuiltIn (te::FourOscPlugin::xmlTypeName, "4OSC (sintetizador)", true);
            addBuiltIn (te::SamplerPlugin::xmlTypeName, "Sampler", true);
        }
        else
        {
            addBuiltIn (te::EqualiserPlugin::xmlTypeName,  "EQ", false);
            addBuiltIn (te::CompressorPlugin::xmlTypeName, "Compressor", false);
            addBuiltIn (te::ReverbPlugin::xmlTypeName,     "Reverb", false);
            addBuiltIn (te::DelayPlugin::xmlTypeName,      "Delay", false);
            addBuiltIn (te::ChorusPlugin::xmlTypeName,     "Chorus", false);
            addBuiltIn (te::PhaserPlugin::xmlTypeName,     "Phaser", false);
            addBuiltIn (te::LowPassPlugin::xmlTypeName,    "Filtro", false);
            addBuiltIn (te::PitchShiftPlugin::xmlTypeName, "Pitch Shift", false);
        }

        menu.addSectionHeader (instruments ? "Instrumentos" : "Efeitos");
        menu.addSubMenu ("Z-Studio (internos)", builtInMenu);

        // Plugins escaneados, agrupados por fabricante
        auto& known = edit.engine.getPluginManager().knownPluginList;
        std::map<juce::String, juce::PopupMenu> byVendor;

        for (auto& d : known.getTypes())
        {
            if (d.isInstrument != instruments)
                continue;

            Entry e;
            e.xmlType = te::ExternalPlugin::xmlTypeName;
            e.desc = d;
            entries->push_back (e);

            auto vendor = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Outros");
            byVendor[vendor].addItem ((int) entries->size(), d.name + "  (" + d.pluginFormatName + ")");
        }

        if (byVendor.empty())
        {
            menu.addItem (-1, "Nenhum plugin escaneado (Opções > Gerenciar plugins)"_t, false);
        }
        else
        {
            menu.addSeparator();

            for (auto& [vendor, sub] : byVendor)
                menu.addSubMenu (vendor, sub);
        }

        auto opts = juce::PopupMenu::Options();

        if (target != nullptr)
            opts = opts.withTargetComponent (target);

        menu.showMenuAsync (opts, [entries, &edit, onChosen] (int result)
        {
            if (result <= 0 || result > (int) entries->size())
                return;

            if (auto p = create (edit, (*entries)[(size_t) result - 1]))
                onChosen (p);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Plugin", "Não foi possível carregar este plugin."_t);
        });
    }
}
