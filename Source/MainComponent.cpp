#include "MainComponent.h"

using namespace juce;

//==============================================================================
MainComponent::MainComponent (te::Engine& e)
    : engine (e)
{
    addAndMakeVisible (transportBar);
    addAndMakeVisible (inspector);
    addChildComponent (scoreView);

    for (auto* b : { &tabArrange, &tabScore })
    {
        b->setClickingTogglesState (false);
        b->setColour (TextButton::buttonOnColourId, Theme::accent);
        addAndMakeVisible (b);
    }

    tabArrange.onClick = [this] { setScoreMode (false); };
    tabScore.onClick   = [this] { setScoreMode (true); };
    tabArrange.setToggleState (true, dontSendNotification);
    ZS::disableFocus (tabArrange);
    ZS::disableFocus (tabScore);

    transportBar.onSnapToggled = [this]
    {
        if (arrangeView != nullptr)
        {
            auto& vs = arrangeView->getViewState();
            vs.snapToGrid = ! vs.snapToGrid;
            transportBar.setSnapState (vs.snapToGrid);
        }
    };

    selectionManager.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.registerAllCommandsForTarget (JUCEApplication::getInstance());
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    ZS::disableFocus (transportBar);

    newProject();
    startTimerHz (4);

    setSize (1400, 860);
}

MainComponent::~MainComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
    selectionManager.removeChangeListener (this);

    transportBar.setEdit (nullptr);
    inspector.setTrack (nullptr, nullptr);
    scoreView.setEdit (nullptr);
    selectionManager.deselectAll();
    arrangeView.reset();
    edit.reset();
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar.setBounds (r.removeFromTop (58));

    auto tabs = r.removeFromTop (32).reduced (8, 4);
    tabArrange.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (4);
    tabScore.setBounds (tabs.removeFromLeft (110));

    scoreView.setVisible (scoreMode);
    if (arrangeView != nullptr)
        arrangeView->setVisible (! scoreMode);

    if (scoreMode)
    {
        inspector.setVisible (false);
        scoreView.setBounds (r);
        return;
    }

    inspector.setVisible (showInspector);
    if (showInspector)
        inspector.setBounds (r.removeFromLeft (230));

    if (arrangeView != nullptr)
        arrangeView->setBounds (r);
}

//==============================================================================
void MainComponent::prepareEdit (te::Edit& ed)
{
    ed.playInStopEnabled = true;   // permite tocar o teclado MIDI com o transporte parado
    ZS::enableAllMidiInputs (engine);
    ed.getTransport().ensureContextAllocated();
    ed.restartPlayback();
}

void MainComponent::setScoreMode (bool shouldShowScore)
{
    scoreMode = shouldShowScore;
    tabArrange.setToggleState (! scoreMode, dontSendNotification);
    tabScore.setToggleState (scoreMode, dontSendNotification);
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit, const File& file)
{
    transportBar.setEdit (nullptr);
    scoreView.setEdit (nullptr);
    inspector.setTrack (nullptr, nullptr);
    selectionManager.deselectAll();
    arrangeView.reset();
    edit.reset();

    edit = std::move (newEdit);
    currentFile = file;
    edit->editFileRetriever = [this] { return currentFile; };

    prepareEdit (*edit);

    arrangeView = std::make_unique<ArrangeView> (*edit, selectionManager);
    addAndMakeVisible (*arrangeView);
    transportBar.setEdit (edit.get());
    scoreView.setEdit (edit.get());
    transportBar.setSnapState (arrangeView->getViewState().snapToGrid);

    if (auto first = ZS::getUserTracks (*edit).getFirst())
        selectionManager.selectOnly (first);

    resized();
    updateTitle();
}

void MainComponent::newProject()
{
    auto file = ZS::getDefaultProjectsFolder().getNonexistentChildFile ("Projeto sem título"_t, ZS::fileExtension, false);
    auto newEdit = te::createEmptyEdit (engine, file);
    newEdit->tempoSequence.getTempo (0)->setBpm (120.0);

    // Remove faixas vazias padrão e começa com uma faixa de instrumento (como o Logic)
    for (auto t : te::getAudioTracks (*newEdit))
        newEdit->deleteTrack (t);

    auto* raw = newEdit.get();
    setEdit (std::move (newEdit), file);

    if (edit.get() == raw)
    {
        addTrack (ZS::TrackKind::instrument);
        edit->getUndoManager().clearUndoHistory();
        edit->resetChangedStatus();
    }
}

void MainComponent::openFile (const File& f)
{
    if (! f.existsAsFile())
        return;

    if (auto loaded = te::loadEditFromFile (engine, f))
    {
        setEdit (std::move (loaded), f);
        engine.getPropertyStorage().setDefaultLoadSaveDirectory ("projects", f.getParentDirectory());
    }
    else
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Abrir projeto",
                                          "Não foi possível abrir: "_t + f.getFullPathName());
    }
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this] (bool ok)
    {
        if (! ok)
            return;

        chooser = std::make_shared<FileChooser> ("Abrir projeto", ZS::getDefaultProjectsFolder(), ZS::fileWildcard);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { openFile (fc.getResult()); });
    });
}

void MainComponent::saveProject (bool saveAs, std::function<void (bool)> onDone)
{
    if (edit == nullptr)
        return;

    const bool isUntitled = currentFile.getFileNameWithoutExtension().startsWith ("Projeto sem título"_t)
                             && ! currentFile.existsAsFile();

    if (saveAs || isUntitled)
    {
        chooser = std::make_shared<FileChooser> ("Salvar projeto como", currentFile, ZS::fileWildcard);
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                                | FileBrowserComponent::warnAboutOverwriting,
                              [this, onDone] (const FileChooser& fc)
        {
            auto f = fc.getResult();

            if (f == File())
            {
                if (onDone) onDone (false);
                return;
            }

            if (! f.hasFileExtension ("zstudio;tracktionedit"))
                f = f.withFileExtension (ZS::fileExtension);

            currentFile = f;
            const bool ok = te::EditFileOperations (*edit).writeToFile (f, false);
            if (ok) edit->resetChangedStatus();
            updateTitle();
            if (onDone) onDone (ok);
        });
        return;
    }

    const bool ok = te::EditFileOperations (*edit).writeToFile (currentFile, false);
    if (ok) edit->resetChangedStatus();
    updateTitle();
    if (onDone) onDone (ok);
}

void MainComponent::confirmDiscardChanges (std::function<void (bool)> onDone)
{
    if (edit == nullptr || ! edit->hasChangedSinceSaved())
    {
        onDone (true);
        return;
    }

    auto opts = MessageBoxOptions()
                  .withIconType (MessageBoxIconType::NoIcon)
                  .withTitle ("Salvar alterações?"_t)
                  .withMessage ("O projeto \"" + currentFile.getFileNameWithoutExtension() + "\" tem alterações não salvas."_t)
                  .withButton ("Salvar", 1)
                  .withButton ("Não salvar"_t, 2)
                  .withButton ("Cancelar", 0)
                  .withAssociatedComponent (this);

    AlertWindow::showAsync (opts, [this, onDone] (int result)
    {
        if (result == 1)       saveProject (false, onDone);   // Salvar
        else if (result == 2)  onDone (true);                 // Não salvar
        else                   onDone (false);                // Cancelar
    });
}

void MainComponent::importAudio()
{
    chooser = std::make_shared<FileChooser> ("Importar áudio ou MIDI"_t,
                                             engine.getPropertyStorage().getDefaultLoadSaveDirectory ("import"),
                                             engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats() + ";*.mid;*.midi");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                            | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty() || edit == nullptr)
            return;

        engine.getPropertyStorage().setDefaultLoadSaveDirectory ("import", files.getFirst().getParentDirectory());

        StringArray paths;
        bool anyMidi = false;
        for (auto& f : files)
        {
            paths.add (f.getFullPathName());
            anyMidi = anyMidi || f.hasFileExtension ("mid;midi");
        }

        // Usa a faixa selecionada (se for do tipo certo) ou cria uma nova
        auto* track = getSelectedTrack();
        auto wantKind = anyMidi ? ZS::TrackKind::instrument : ZS::TrackKind::audio;

        if (track == nullptr || ZS::getKind (*track) != wantKind)
        {
            addTrack (wantKind);
            track = getSelectedTrack();
        }

        if (track != nullptr && arrangeView != nullptr)
            arrangeView->importFiles (*track, paths, edit->getTransport().getPosition());
    });
}

void MainComponent::exportMix()
{
    if (edit == nullptr)
        return;

    if (edit->getLength() <= 0s)
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Exportar", "O projeto está vazio."_t);
        return;
    }

    auto def = currentFile.getParentDirectory().getChildFile (currentFile.getFileNameWithoutExtension() + ".wav");
    chooser = std::make_shared<FileChooser> ("Exportar mixagem (WAV)", def, "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                            | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File())
            return;

        f = f.withFileExtension (".wav");
        edit->getTransport().stop (false, true);

        const bool ok = te::Renderer::renderToFile (*edit, f, true);

        if (ok && f.existsAsFile())
            f.revealToUser();
        else
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Exportar", "Falha ao exportar o arquivo.");

        prepareEdit (*edit);
    });
}

void MainComponent::addTrack (ZS::TrackKind kind)
{
    if (edit == nullptr)
        return;

    edit->getUndoManager().beginNewTransaction ("Nova faixa");

    auto tracks = ZS::getUserTracks (*edit);
    auto* after = getSelectedTrack();
    auto insertPoint = after != nullptr ? te::TrackInsertPoint (*after, false)
                                        : te::TrackInsertPoint::getEndOfTracks (*edit);

    auto track = edit->insertNewAudioTrack (insertPoint, nullptr, true);
    if (track == nullptr)
        return;

    int count = 1;
    for (auto t : tracks)
        if (ZS::getKind (*t) == kind)
            ++count;

    const bool isInstrument = kind == ZS::TrackKind::instrument;
    track->state.setProperty (ZS::trackKind, isInstrument ? "instrument" : "audio", nullptr);
    track->setName ((isInstrument ? "Instrumento "_t : "Áudio "_t) + String (count));
    track->setColour (Theme::trackColour (tracks.size()));

    if (isInstrument)
        ZS::setInstrument (*track, ZS::createBuiltIn (*edit, te::FourOscPlugin::xmlTypeName));

    selectionManager.selectOnly (track.get());
}

void MainComponent::deleteSelected()
{
    if (edit == nullptr)
        return;

    auto selected = selectionManager.getSelectedObjects();
    if (selected.isEmpty())
        return;

    edit->getUndoManager().beginNewTransaction ("Apagar");

    juce::Array<te::Clip::Ptr> clips;
    juce::Array<te::Track::Ptr> tracks;

    for (auto obj : selected)
    {
        if (auto c = dynamic_cast<te::Clip*> (obj))       clips.add (c);
        else if (auto t = dynamic_cast<te::Track*> (obj)) tracks.add (t);
    }

    selectionManager.deselectAll();

    for (auto& c : clips)
        c->removeFromParent();

    if (clips.isEmpty())
    {
        inspector.setTrack (nullptr, nullptr);

        for (auto& t : tracks)
            edit->deleteTrack (t.get());
    }
}

te::AudioTrack* MainComponent::getSelectedTrack() const
{
    for (auto obj : selectionManager.getSelectedObjects())
    {
        if (auto t = dynamic_cast<te::AudioTrack*> (obj))
            return t;

        if (auto c = dynamic_cast<te::Clip*> (obj))
            if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                return t;
    }

    return nullptr;
}

void MainComponent::selectionChanged()
{
    auto* track = getSelectedTrack();
    inspector.setTrack (edit.get(), track);
    scoreView.setSelectedTrack (track);

    // Estilo Logic: o teclado MIDI toca a faixa de instrumento selecionada
    if (track != nullptr && ZS::isInstrumentTrack (*track))
        ZS::routeMidiInputsTo (*track);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &selectionManager)
    {
        selectionChanged();
        commandManager.commandStatusChanged();
    }
    else if (source == &engine.getDeviceManager() && edit != nullptr)
    {
        // Dispositivos mudaram (ex.: teclado conectado): reativa e reencaminha o MIDI
        ZS::enableAllMidiInputs (engine);

        if (auto* t = getSelectedTrack())
            if (ZS::isInstrumentTrack (*t))
                ZS::routeMidiInputsTo (*t);
    }
}

void MainComponent::timerCallback()
{
    updateTitle();

    // Mantém os atalhos do transporte ativos quando nada está com o foco
    if (Component::getCurrentlyFocusedComponent() == nullptr && isShowing()
        && Process::isForegroundProcess() && getTopLevelComponent()->isOnDesktop())
        grabKeyboardFocus();

    if (edit != nullptr && arrangeView != nullptr && edit->getTransport().isPlaying()
        && ! edit->getTransport().isUserDragging())
        arrangeView->followPlayhead();
}

void MainComponent::updateTitle()
{
    if (edit == nullptr)
        return;

    auto title = currentFile.getFileNameWithoutExtension()
                 + (edit->hasChangedSinceSaved() ? " *" : "")
                 + "  -  Z-Studio";

    if (title != lastTitle)
    {
        lastTitle = title;
        if (onTitleChanged)
            onTitleChanged (title);
    }
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Áudio e MIDI"_t;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;

    auto* selector = new AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                       0, 256, 0, 256,
                                                       true,    // entradas MIDI
                                                       true,    // saída MIDI
                                                       true,    // canais em pares estéreo
                                                       false);
    selector->setSize (520, 620);
    o.content.setOwned (selector);
    o.launchAsync();
}

void MainComponent::showPluginManager()
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Gerenciar plugins (VST3 / AU)";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.useBottomRightCornerResizer = true;

    auto& pm = engine.getPluginManager();
    auto* list = new PluginListComponent (pm.pluginFormatManager, pm.knownPluginList,
                                          engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                          std::addressof (engine.getPropertyStorage().getPropertiesFile()),
                                          true);
    list->setSize (860, 600);
    o.content.setOwned (list);
    o.launchAsync();
}

//==============================================================================
// Comandos e atalhos
//==============================================================================
void MainComponent::getAllCommands (Array<CommandID>& c)
{
    c.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdImportAudio, cmdExport,
                  cmdUndo, cmdRedo, cmdDelete,
                  cmdNewInstrumentTrack, cmdNewAudioTrack, cmdDeleteTrack,
                  cmdPlayStop, cmdRecord, cmdGoToStart, cmdToggleLoop, cmdToggleClick, cmdToggleSnap,
                  cmdZoomIn, cmdZoomOut, cmdToggleInspector, cmdShowArrange, cmdShowScore, cmdExportMusicXML,
                  cmdAudioSettings, cmdPluginManager, cmdAbout });
}

void MainComponent::getCommandInfo (CommandID id, ApplicationCommandInfo& info)
{
    const auto cmd = ModifierKeys::commandModifier;
    const auto shift = ModifierKeys::shiftModifier;

    switch (id)
    {
        case cmdNew:            info.setInfo ("Novo projeto", {}, "Arquivo", 0);                 info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:           info.setInfo ("Abrir...", {}, "Arquivo", 0);                     info.addDefaultKeypress ('o', cmd); break;
        case cmdSave:           info.setInfo ("Salvar", {}, "Arquivo", 0);                       info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs:         info.setInfo ("Salvar como...", {}, "Arquivo", 0);               info.addDefaultKeypress ('s', cmd | shift); break;
        case cmdImportAudio:    info.setInfo ("Importar áudio/MIDI..."_t, {}, "Arquivo", 0);       info.addDefaultKeypress ('i', cmd); break;
        case cmdExport:         info.setInfo ("Exportar mixagem (WAV)...", {}, "Arquivo", 0);    info.addDefaultKeypress ('e', cmd); break;

        case cmdUndo:
            info.setInfo ("Desfazer", {}, "Editar", 0);
            info.addDefaultKeypress ('z', cmd);
            info.setActive (edit != nullptr && edit->getUndoManager().canUndo());
            break;
        case cmdRedo:
            info.setInfo ("Refazer", {}, "Editar", 0);
            info.addDefaultKeypress ('z', cmd | shift);
            info.setActive (edit != nullptr && edit->getUndoManager().canRedo());
            break;
        case cmdDelete:
            info.setInfo ("Apagar selecionado", {}, "Editar", 0);
            info.addDefaultKeypress (KeyPress::deleteKey, 0);
            info.addDefaultKeypress (KeyPress::backspaceKey, 0);
            break;

        case cmdNewInstrumentTrack: info.setInfo ("Nova faixa de instrumento", {}, "Faixa", 0); info.addDefaultKeypress ('t', cmd); break;
        case cmdNewAudioTrack:      info.setInfo ("Nova faixa de áudio"_t, {}, "Faixa", 0);       info.addDefaultKeypress ('t', cmd | shift); break;
        case cmdDeleteTrack:
            info.setInfo ("Apagar faixa selecionada", {}, "Faixa", 0);
            info.setActive (getSelectedTrack() != nullptr);
            break;

        case cmdPlayStop:    info.setInfo ("Tocar / Parar", {}, "Transporte", 0);         info.addDefaultKeypress (KeyPress::spaceKey, 0); break;
        case cmdRecord:      info.setInfo ("Gravar", {}, "Transporte", 0);                info.addDefaultKeypress ('r', 0); info.addDefaultKeypress (KeyPress::numberPadMultiply, 0); break;
        case cmdGoToStart:   info.setInfo ("Voltar ao início"_t, {}, "Transporte", 0);      info.addDefaultKeypress (KeyPress::returnKey, 0); info.addDefaultKeypress (KeyPress::homeKey, 0); break;
        case cmdToggleLoop:  info.setInfo ("Cycle / Loop", {}, "Transporte", 0);          info.addDefaultKeypress ('c', 0); break;
        case cmdToggleClick: info.setInfo ("Metrônomo"_t, {}, "Transporte", 0);             info.addDefaultKeypress ('k', 0); break;
        case cmdToggleSnap:  info.setInfo ("Encaixar na grade", {}, "Transporte", 0);     info.addDefaultKeypress ('j', 0);
                             info.setTicked (arrangeView != nullptr && arrangeView->getViewState().snapToGrid); break;

        case cmdZoomIn:          info.setInfo ("Aproximar (zoom +)", {}, "Ver", 0);       info.addDefaultKeypress ('h', 0); info.addDefaultKeypress ('=', cmd); break;
        case cmdZoomOut:         info.setInfo ("Afastar (zoom -)", {}, "Ver", 0);         info.addDefaultKeypress ('g', 0); info.addDefaultKeypress ('-', cmd); break;
        case cmdToggleInspector: info.setInfo ("Mostrar inspetor", {}, "Ver", 0);         info.addDefaultKeypress ('i', 0); info.setTicked (showInspector); break;

        case cmdShowArrange:     info.setInfo ("Arranjo", {}, "Ver", 0);                  info.addDefaultKeypress ('1', cmd); info.setTicked (! scoreMode); break;
        case cmdShowScore:       info.setInfo ("Partitura", {}, "Ver", 0);                info.addDefaultKeypress ('n', 0); info.addDefaultKeypress ('2', cmd);
                                 info.addDefaultKeypress ('r', cmd); info.setTicked (scoreMode); break;
        case cmdExportMusicXML:  info.setInfo ("Exportar partitura (MusicXML)...", {}, "Arquivo", 0); info.addDefaultKeypress ('e', cmd | shift); break;

        case cmdAudioSettings: info.setInfo ("Áudio e MIDI..."_t, {}, "Opções"_t, 0);         info.addDefaultKeypress (',', cmd); break;
        case cmdPluginManager: info.setInfo ("Gerenciar plugins...", {}, "Opções"_t, 0);    break;
        case cmdAbout:         info.setInfo ("Sobre o Z-Studio", {}, "Ajuda", 0);     break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    if (edit == nullptr)
        return false;

    switch (info.commandID)
    {
        case cmdNew:         confirmDiscardChanges ([this] (bool ok) { if (ok) newProject(); }); break;
        case cmdOpen:        openProject(); break;
        case cmdSave:        saveProject (false); break;
        case cmdSaveAs:      saveProject (true); break;
        case cmdImportAudio: importAudio(); break;
        case cmdExport:      exportMix(); break;

        case cmdUndo:        edit->getUndoManager().undo(); break;
        case cmdRedo:        edit->getUndoManager().redo(); break;
        case cmdDelete:      deleteSelected(); break;

        case cmdNewInstrumentTrack: addTrack (ZS::TrackKind::instrument); break;
        case cmdNewAudioTrack:      addTrack (ZS::TrackKind::audio); break;
        case cmdDeleteTrack:
            if (auto t = getSelectedTrack())
            {
                edit->getUndoManager().beginNewTransaction ("Apagar faixa");
                te::Track::Ptr keep (t);
                selectionManager.deselectAll();
                inspector.setTrack (nullptr, nullptr);
                edit->deleteTrack (t);
            }
            break;

        case cmdPlayStop:    transportBar.togglePlay(); break;
        case cmdRecord:      transportBar.toggleRecord(); break;
        case cmdGoToStart:   edit->getTransport().setPosition (0s); break;
        case cmdToggleLoop:  transportBar.toggleLoop(); break;
        case cmdToggleClick: edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); break;
        case cmdToggleSnap:  if (transportBar.onSnapToggled) transportBar.onSnapToggled(); break;

        case cmdZoomIn:          if (arrangeView) arrangeView->zoomIn(); break;
        case cmdZoomOut:         if (arrangeView) arrangeView->zoomOut(); break;
        case cmdToggleInspector: showInspector = ! showInspector; resized(); break;

        case cmdShowArrange:     setScoreMode (false); break;
        case cmdShowScore:       setScoreMode (! scoreMode); break;
        case cmdExportMusicXML:  scoreView.exportMusicXML(); break;

        case cmdAudioSettings: showAudioSettings(); break;
        case cmdPluginManager: showPluginManager(); break;
        case cmdAbout:
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Z-Studio",
                                              "Z-Studio " + String (JUCE_APPLICATION_VERSION_STRING) + "\n"
                                              "por Eliseu F. Guedes\n\n"
                                              "Motor de áudio: Tracktion Engine + JUCE (licença AGPLv3)"_t);
            break;
        default:
            return false;
    }

    commandManager.commandStatusChanged();
    return true;
}

StringArray MainComponent::getMenuBarNames()
{
    return { "Arquivo", "Editar", "Faixa", "Transporte", "Ver", "Opções"_t, "Ajuda" };
}

PopupMenu MainComponent::getMenuForIndex (int index, const String&)
{
    PopupMenu m;
    auto* cm = &commandManager;

    switch (index)
    {
        case 0:
            m.addCommandItem (cm, cmdNew);
            m.addCommandItem (cm, cmdOpen);
            m.addSeparator();
            m.addCommandItem (cm, cmdSave);
            m.addCommandItem (cm, cmdSaveAs);
            m.addSeparator();
            m.addCommandItem (cm, cmdImportAudio);
            m.addCommandItem (cm, cmdExport);
            m.addCommandItem (cm, cmdExportMusicXML);
           #if ! JUCE_MAC
            m.addSeparator();
            {
                PopupMenu::Item quitItem ("Sair");
                quitItem.setAction ([] { JUCEApplication::getInstance()->systemRequestedQuit(); });
                quitItem.shortcutKeyDescription = "Ctrl+Q";
                m.addItem (quitItem);
            }
           #endif
            break;
        case 1:
            m.addCommandItem (cm, cmdUndo);
            m.addCommandItem (cm, cmdRedo);
            m.addSeparator();
            m.addCommandItem (cm, cmdDelete);
            break;
        case 2:
            m.addCommandItem (cm, cmdNewInstrumentTrack);
            m.addCommandItem (cm, cmdNewAudioTrack);
            m.addSeparator();
            m.addCommandItem (cm, cmdDeleteTrack);
            break;
        case 3:
            m.addCommandItem (cm, cmdPlayStop);
            m.addCommandItem (cm, cmdRecord);
            m.addCommandItem (cm, cmdGoToStart);
            m.addSeparator();
            m.addCommandItem (cm, cmdToggleLoop);
            m.addCommandItem (cm, cmdToggleClick);
            m.addCommandItem (cm, cmdToggleSnap);
            break;
        case 4:
            m.addCommandItem (cm, cmdShowArrange);
            m.addCommandItem (cm, cmdShowScore);
            m.addSeparator();
            m.addCommandItem (cm, cmdZoomIn);
            m.addCommandItem (cm, cmdZoomOut);
            m.addSeparator();
            m.addCommandItem (cm, cmdToggleInspector);
            break;
        case 5:
            m.addCommandItem (cm, cmdAudioSettings);
            m.addCommandItem (cm, cmdPluginManager);
            break;
        case 6:
            m.addCommandItem (cm, cmdAbout);
            break;
        default:
            break;
    }

    return m;
}
