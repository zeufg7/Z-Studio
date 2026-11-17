#include <JuceHeader.h>
#include "MainComponent.h"
#include "PluginWindow.h"

//==============================================================================
/** Comportamento de interface do motor: janelas de plugin e barras de progresso. */
class ZeufgUIBehaviour : public ExtendedUIBehaviour
{
public:
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        struct Runner : public juce::ThreadWithProgressWindow
        {
            Runner (te::ThreadPoolJobWithProgress& t)
                : juce::ThreadWithProgressWindow (t.getJobName(), true, false), job (t) {}

            void run() override
            {
                while (! threadShouldExit())
                {
                    if (job.runJob() == juce::ThreadPoolJob::jobHasFinished)
                        break;

                    setProgress (job.getCurrentTaskProgress());
                }
            }

            te::ThreadPoolJobWithProgress& job;
        };

        Runner runner (task);
        runner.runThread();
    }
};

//==============================================================================
class ZStudioApplication : public juce::JUCEApplication
{
public:
    ZStudioApplication() = default;

    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<te::Engine> ("Z-Studio", std::make_unique<ZeufgUIBehaviour>(), nullptr);
        mainWindow = std::make_unique<MainWindow> (*engine, *this);

        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
            return quit();

        mainWindow->getContent().confirmDiscardChanges ([] (bool ok)
        {
            if (ok)
                juce::JUCEApplication::getInstance()->quit();
        });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFromCommandLine (commandLine);
    }

private:
    void openFromCommandLine (const juce::String& commandLine)
    {
        auto path = commandLine.unquoted().trim();

        if (path.isNotEmpty() && mainWindow != nullptr)
        {
            juce::File f (path);
            if (f.existsAsFile())
                mainWindow->getContent().confirmDiscardChanges ([this, f] (bool ok)
                {
                    if (ok && mainWindow != nullptr)
                        mainWindow->getContent().openFile (f);
                });
        }
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (te::Engine& engine, juce::JUCEApplication& a)
            : DocumentWindow ("Z-Studio", Theme::background, DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);

            auto* content = new MainComponent (engine);
            content->onTitleChanged = [this] (const juce::String& t) { setName (t); };
            setContentOwned (content, true);

           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (content);
           #else
            setMenuBar (content, 26);
           #endif

            addKeyListener (content->commandManager.getKeyMappings());

            setResizable (true, false);
            setResizeLimits (900, 560, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        MainComponent& getContent()     { return *static_cast<MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override
        {
            app.systemRequestedQuit();
        }

    private:
        juce::JUCEApplication& app;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    Theme::LookAndFeel lookAndFeel;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (ZStudioApplication)
