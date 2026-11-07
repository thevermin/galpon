#include "MainComponent.h"
#include "GalponUI.h"

namespace galpon
{

namespace
{
    juce::PopupMenu::Item item (const juce::String& text, int id, const juce::String& shortcut = {})
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        return i;
    }

    enum MenuIds
    {
        newSong = 1, openSong, saveSong, saveSongAs, importAudio, importMidi, exportMix,
        undo = 20, redo,
        audioSettings = 30, scanPlugins, showSongFolder, recoverRecordings,
        help = 40
    };

    juce::String mimeFor (const juce::String& path)
    {
        if (path.endsWith (".html")) return "text/html";
        if (path.endsWith (".css"))  return "text/css";
        if (path.endsWith (".js"))   return "text/javascript";
        if (path.endsWith (".svg"))  return "image/svg+xml";
        if (path.endsWith (".png"))  return "image/png";
        if (path.endsWith (".json")) return "application/json";
        return "application/octet-stream";
    }

    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : items) o->setProperty (k, v);
        return juce::var (o);
    }
}

MainComponent::MainComponent (Engine& e) : engine (e)
{
    // For working on the interface without rebuilding: GALPON_UI_DIR=/path/to/galpon/ui
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("GALPON_UI_DIR", {}); dir.isNotEmpty())
        devUiFolder = juce::File (dir);

    api.openPluginEditor = [this] (int trackId, int slot) -> juce::Result
    {
        auto* s = engine.findSlot (trackId, slot);
        if (s == nullptr || s->processor == nullptr) return juce::Result::fail ("No hay nada en ese lugar");
        pluginWindows.open (*s->processor, s->name);
        return juce::Result::ok();
    };

    engine.onStatus = [this] (const juce::String& message)
    {
        juce::MessageManager::callAsync ([this, message] { emit ("toast", message); });
    };
    engine.onSongChanged = [this] { api.touch(); };
    engine.plugins.onChange = [this] { api.touch(); };

    auto options = juce::WebBrowserComponent::Options{}
       #if JUCE_WINDOWS
        // Without this JUCE uses the old Internet Explorer control, which can't show the interface.
        // WebView2 keeps its data in a writable folder (not next to the .exe in Program Files).
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                     .getChildFile ("Galpon").getChildFile ("WebView2"))
            .withBackgroundColour (juce::Colour (0xff161618)))
       #endif
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return serve (url); })
        .withNativeFunction ("galpon", [this] (const juce::Array<juce::var>& args,
                                               juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            handle (args.size() > 0 ? args[0].toString() : juce::String(), args.size() > 1 ? args[1] : juce::var(), std::move (done));
        });

   #if JUCE_WINDOWS
    if (! juce::WebBrowserComponent::areOptionsSupported (options))
    {
        // WebView2 comes with Windows 10 and 11, but it can be missing or removed.
        auto opts = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
            "Falta Microsoft WebView2",
            juce::String (u8"Galpon necesita el componente WebView2 de Microsoft para mostrar su pantalla. "
                          u8"Instálalo (es gratis y tarda un minuto) y vuelve a abrir Galpon."),
            "Descargar WebView2", "Cerrar");
        missingWebViewBox = juce::NativeMessageBox::showScopedAsync (opts, [] (int result)
        {
            if (result == 1)
                juce::URL ("https://go.microsoft.com/fwlink/p/?LinkId=2124703").launchInDefaultBrowser();
        });
    }
   #endif

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setSize (1440, 900);
    startTimerHz (30);

    // Notice audio interfaces plugged in while Galpon is open
    knownDevices = currentDeviceNames();
    for (auto* type : engine.deviceManager.getAvailableDeviceTypes())
        type->addListener (this);
}

juce::StringArray MainComponent::currentDeviceNames()
{
    juce::StringArray names;
    if (auto* type = engine.deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        names.addArray (type->getDeviceNames (true));
        names.addArray (type->getDeviceNames (false));
        names.removeDuplicates (false);
    }
    return names;
}

void MainComponent::audioDeviceListChanged()
{
    auto now = currentDeviceNames();
    const auto setup = engine.deviceManager.getAudioDeviceSetup();
    for (auto& name : now)
    {
        if (knownDevices.contains (name) || name == setup.inputDeviceName) continue;
        // a device that has inputs = an interface you can record with
        bool hasInputs = false;
        if (auto* type = engine.deviceManager.getCurrentDeviceTypeObject())
            hasInputs = type->getDeviceNames (true).contains (name);
        if (! hasInputs) continue;

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name);
        o->setProperty ("hasOutput", engine.deviceManager.getCurrentDeviceTypeObject()->getDeviceNames (false).contains (name));
        emit ("deviceAdded", juce::var (o));
    }
    for (auto& name : knownDevices)
        if (! now.contains (name) && (name == setup.inputDeviceName || name == setup.outputDeviceName))
            emit ("toast", juce::String (u8"Se desconectó ") + name);
    knownDevices = now;
    api.touch();
}

MainComponent::~MainComponent()
{
    stopTimer();
    for (auto* type : engine.deviceManager.getAvailableDeviceTypes())
        type->removeListener (this);
    engine.onStatus = nullptr;
    engine.onSongChanged = nullptr;
    engine.plugins.onChange = nullptr;
    pluginWindows.closeAll();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff161618));
}

void MainComponent::resized()
{
    browser->setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> MainComponent::serve (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).fromFirstOccurrenceOf ("/", false, false);
    if (path.isEmpty()) path = "index.html";
    path = path.fromLastOccurrenceOf ("/", false, false);

    std::vector<std::byte> bytes;
    if (devUiFolder.isDirectory())
    {
        juce::MemoryBlock mb;
        if (devUiFolder.getChildFile (path).loadFileAsData (mb))
            bytes.assign ((const std::byte*) mb.getData(), (const std::byte*) mb.getData() + mb.getSize());
    }
    if (bytes.empty())
    {
        for (int i = 0; i < GalponUI::namedResourceListSize; ++i)
        {
            if (juce::String (GalponUI::originalFilenames[i]) != path) continue;
            int size = 0;
            auto* data = GalponUI::getNamedResource (GalponUI::namedResourceList[i], size);
            bytes.assign ((const std::byte*) data, (const std::byte*) data + size);
            break;
        }
    }
    if (bytes.empty()) return std::nullopt;

    // A UTF-8 byte-order mark makes every web view read accented text correctly,
    // whatever it assumes about the encoding.
    const auto mime = mimeFor (path);
    if (mime.startsWith ("text/") && ! (bytes.size() >= 3 && bytes[0] == std::byte { 0xEF }))
        bytes.insert (bytes.begin(), { std::byte { 0xEF }, std::byte { 0xBB }, std::byte { 0xBF } });
    return juce::WebBrowserComponent::Resource { std::move (bytes), mime };
}

void MainComponent::emit (const juce::String& event, const juce::var& payload)
{
    browser->emitEventIfBrowserIsVisible (event, payload);
}

void MainComponent::timerCallback()
{
    emit ("tick", api.getTick());
}

void MainComponent::handle (const juce::String& cmd, const juce::var& args,
                            juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    if (cmd == "choose")
    {
        choose (args, std::move (done));
        return;
    }
    if (cmd == "revealSongFolder")
    {
        engine.songFolder.revealToUser();
        done (obj ({ { "ok", true } }));
        return;
    }
    done (api.call (cmd, args));
}

void MainComponent::choose (const juce::var& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    const auto mode = getOrDefault (args, "mode", "open").toString();
    const auto title = getOrDefault (args, "title", juce::String (u8"Elige un archivo")).toString();
    const auto patterns = getOrDefault (args, "patterns", "*").toString();
    const auto name = getOrDefault (args, "name", "").toString();

    auto start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    if (mode == "song" || mode == "saveSong") start = Engine::defaultSongsFolder();
    if (mode == "save")   // exports go next to the songs, in Música › Galpon › Exportados
    {
        start = Engine::defaultSongsFolder().getChildFile (juce::String (u8"Exportados"));
        start.createDirectory();
    }
    if (name.isNotEmpty()) start = start.getChildFile (name);

    chooser = std::make_unique<juce::FileChooser> (title, start, patterns, true);
    int flags = juce::FileBrowserComponent::canSelectFiles;
    if (mode == "song")
        flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectFiles;
    else if (mode == "feedpak")    // game songs: .feedpak files or folders, several at once
        flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
              | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems;
    else if (mode == "folder")
        flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    else if (mode == "save" || mode == "saveSong")
        flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
    else
        flags |= juce::FileBrowserComponent::openMode;

    chooser->launchAsync (flags, [done = std::move (done), mode] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (mode == "saveSong" && file != juce::File() && ! file.hasFileExtension ("galpon"))
            file = file.withFileExtension ("galpon");
        juce::Array<juce::var> paths;
        for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
        done (obj ({ { "ok", file != juce::File() }, { "path", file.getFullPathName() }, { "paths", paths } }));
    });
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Archivo", juce::String (u8"Edición"), juce::String (u8"Configuración") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    if (index == 0)
    {
        m.addItem (item (juce::String (u8"Canción nueva"), newSong, juce::String (u8"⌘N")));
        m.addItem (item (juce::String (u8"Abrir…"), openSong, juce::String (u8"⌘O")));
        m.addSeparator();
        m.addItem (item ("Guardar", saveSong, juce::String (u8"⌘S")));
        m.addItem (item (juce::String (u8"Guardar como…"), saveSongAs, juce::String (u8"⇧⌘S")));
        m.addSeparator();
        m.addItem (item (juce::String (u8"Importar audio…"), importAudio, juce::String (u8"⌘I")));
        m.addItem (item (juce::String (u8"Importar MIDI…"), importMidi));
        m.addItem (item (juce::String (u8"Exportar canción…"), exportMix, juce::String (u8"⌘E")));
        m.addSeparator();
        m.addItem (item (juce::String (u8"Recuperar grabaciones…"), recoverRecordings));
        m.addItem (item (juce::String (u8"Mostrar carpeta de la canción"), showSongFolder));
    }
    else if (index == 1)
    {
        m.addItem (item ("Deshacer", undo, juce::String (u8"⌘Z")));
        m.addItem (item ("Rehacer", redo, juce::String (u8"⇧⌘Z")));
    }
    else
    {
        m.addItem (item (juce::String (u8"Audio y MIDI…"), audioSettings, juce::String (u8"⌘,")));
        m.addItem (item ("Buscar plugins", scanPlugins));
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    // Most menu actions live in the interface (it shows dialogs, toasts, etc.)
    static const std::map<int, const char*> actions {
        { newSong, "newSong" }, { openSong, "openSong" }, { saveSong, "saveSong" }, { saveSongAs, "saveSongAs" },
        { importAudio, "importAudio" }, { importMidi, "importMidi" }, { exportMix, "export" },
        { undo, "undo" }, { redo, "redo" }, { audioSettings, "settings" }, { scanPlugins, "scanPlugins" },
        { recoverRecordings, "recoverRecordings" }
    };
    if (id == showSongFolder) { engine.songFolder.revealToUser(); return; }
    if (auto it = actions.find (id); it != actions.end())
        emit ("menu", juce::String (it->second));
}

bool MainComponent::confirmQuit()
{
    if (engine.isRecording()) engine.stop();
    if (engine.dirty) engine.saveSong();
    return true;
}

} // namespace galpon
