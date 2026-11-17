#pragma once

#include "Helpers.h"
#include "ArrangeView.h"
#include "TransportBar.h"
#include "Inspector.h"
#include "ScoreView.h"

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (te::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pergunta se deve salvar antes de fechar; chama onDone(true) para continuar. */
    void confirmDiscardChanges (std::function<void (bool)> onDone);

    void openFile (const juce::File&);

    std::function<void (const juce::String&)> onTitleChanged;

    //==============================================================================
    enum CommandIDs
    {
        cmdNew = 1, cmdOpen, cmdSave, cmdSaveAs, cmdImportAudio, cmdExport,
        cmdUndo, cmdRedo, cmdDelete,
        cmdNewInstrumentTrack, cmdNewAudioTrack, cmdDeleteTrack,
        cmdPlayStop, cmdRecord, cmdGoToStart, cmdToggleLoop, cmdToggleClick, cmdToggleSnap,
        cmdZoomIn, cmdZoomOut, cmdToggleInspector, cmdShowArrange, cmdShowScore, cmdExportMusicXML,
        cmdAudioSettings, cmdPluginManager, cmdAbout
    };

    juce::ApplicationCommandTarget* getNextCommandTarget() override     { return juce::JUCEApplication::getInstance(); }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    juce::ApplicationCommandManager commandManager;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void setEdit (std::unique_ptr<te::Edit>, const juce::File&);
    void newProject();
    void openProject();
    void saveProject (bool saveAs, std::function<void (bool)> onDone = {});
    void importAudio();
    void exportMix();
    void addTrack (ZS::TrackKind);
    void deleteSelected();
    void showAudioSettings();
    void showPluginManager();
    void updateTitle();

    te::AudioTrack* getSelectedTrack() const;
    void selectionChanged();
    void prepareEdit (te::Edit&);

    te::Engine& engine;
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    juce::File currentFile;

    TransportBar transportBar;
    Inspector inspector;
    std::unique_ptr<ArrangeView> arrangeView;
    ScoreView scoreView { engine };
    juce::TextButton tabArrange { "Arranjo" }, tabScore { "Partitura" };
    bool scoreMode = false;
    void setScoreMode (bool);
    bool showInspector = true;
    juce::String lastTitle;
    juce::TooltipWindow tooltips { this, 600 };
    std::shared_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
