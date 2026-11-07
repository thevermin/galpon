#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "engine/EngineApi.h"

namespace galpon
{

static juce::File appDataFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Galpon");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Galpon");
   #endif
}

/** If Galpon ever closes by itself, leave a note saying where it was, so it can be fixed:
    the next time it opens, it offers to show it. */
static juce::File crashNoteFile;
static void writeCrashNote (void*)
{
    juce::String text;
    text << "Galpon " << JUCE_APPLICATION_VERSION_STRING << " se cerró inesperadamente el " << juce::Time::getCurrentTime().toString (true, true) << "\n"
         << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getCpuModel() << ", "
         << juce::SystemStats::getMemorySizeInMegabytes() << " MB de memoria\n\n"
         << "Últimos comandos: " << EngineApi::recentCommands() << "\n\n"
         << "Dónde estaba:\n" << juce::SystemStats::getStackBacktrace() << "\n";
    crashNoteFile.replaceWithText (text);
}

class GalponApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Galpon"; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        appDataFolder().createDirectory();
        crashNoteFile = appDataFolder().getChildFile ("cierre-inesperado.txt");
        juce::SystemStats::setApplicationCrashHandler (writeCrashNote);
        engine = std::make_unique<Engine> (appDataFolder());
        mainWindow = std::make_unique<MainWindow> (*engine);

        // Opened by double-clicking a song
        auto path = commandLine.unquoted().trim();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
            engine->loadSong (juce::File (path));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr || mainWindow->content->confirmQuit())
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        auto path = commandLine.unquoted().trim();
        if (path.isNotEmpty() && engine != nullptr)
            engine->loadSong (juce::File (path));
    }

private:
    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (Engine& engine)
            : juce::DocumentWindow ("Galpon", juce::Colour (0xff161618), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (engine);
            setContentOwned (content, true);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (content);
           #else
            setMenuBar (content);
           #endif
            setResizable (true, true);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (1440, 900);
            setVisible (true);
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* content = nullptr;
    };

    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace galpon

START_JUCE_APPLICATION (galpon::GalponApplication)
