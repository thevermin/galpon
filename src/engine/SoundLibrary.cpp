#include "SoundLibrary.h"

namespace galpon
{

struct SoundLibrary::Job : private juce::URL::DownloadTaskListener
{
    Sound sound;
    juce::File zip;
    std::unique_ptr<juce::URL::DownloadTask> task;
    std::atomic<float> fraction { 0.0f };
    std::atomic<int> phase { 0 };        // 0 downloading, 1 unpacking, 2 done, 3 failed
    juce::CriticalSection lock;
    juce::String error;
    std::atomic<bool> cancelled { false };
    std::unique_ptr<juce::Thread> unpacker;

    void start()
    {
        zip = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("galpon-" + sound.id, ".zip", false);
        task = juce::URL (sound.zipUrl).downloadToFile (zip, juce::URL::DownloadTaskOptions().withListener (this));
        if (task == nullptr) fail (juce::String (u8"No se pudo empezar la descarga. ¿Hay internet?"));
    }

    void progress (juce::URL::DownloadTask*, juce::int64 downloaded, juce::int64 total) override
    {
        // unknown size (GitHub doesn't say): negative = MB downloaded so far
        fraction.store (total > 0 ? 0.9f * (float) downloaded / (float) total : -(float) downloaded / 1.0e6f);
    }

    void finished (juce::URL::DownloadTask* t, bool success) override
    {
        if (cancelled) { zip.deleteFile(); return; }
        if (! success || t->hadError() || (t->statusCode() != 0 && t->statusCode() >= 400))
        {
            zip.deleteFile();
            fail (juce::String (u8"La descarga falló") + (t->statusCode() >= 400 ? " (error " + juce::String (t->statusCode()) + ")" : juce::String()));
            return;
        }
        phase = 1;
        fraction = 0.9f;
        struct Unpack : juce::Thread
        {
            Job& job;
            explicit Unpack (Job& j) : juce::Thread ("Galpon unpack"), job (j) {}
            void run() override
            {
                auto r = SoundLibrary::install (job.sound, job.zip, [this] (float p) { job.fraction = 0.9f + 0.1f * p; });
                job.zip.deleteFile();
                if (r.failed()) job.fail (r.getErrorMessage());
                else            job.phase = 2;
            }
        };
        unpacker = std::make_unique<Unpack> (*this);
        unpacker->startThread();
    }

    void fail (const juce::String& e)
    {
        { const juce::ScopedLock sl (lock); error = e; }
        phase = 3;
    }

    ~Job() override
    {
        cancelled = true;
        task.reset();
        if (unpacker) unpacker->stopThread (60000);
    }
};

//==============================================================================
SoundLibrary::SoundLibrary() = default;
SoundLibrary::~SoundLibrary() = default;

const std::vector<SoundLibrary::Sound>& SoundLibrary::catalog()
{
    static const std::vector<Sound> sounds {
        { "splendid-piano", juce::String (u8"Piano de cola real"), "Teclados",
          juce::String (u8"Un Steinway grabado nota por nota, en 4 intensidades."),
          "AKAI (adaptado por kinwie y P. Eastman)", juce::String (u8"Dominio público"), "~150 MB",
          "https://github.com/sfzinstruments/SplendidGrandPiano/archive/refs/heads/master.zip",
          "SplendidGrandPiano-master", "Splendid Grand Piano.sfz", false },
        { "virtuosity-drums", juce::String (u8"Batería acústica real"), juce::String (u8"Batería"),
          juce::String (u8"Batería grabada con varios micrófonos, mapa General MIDI: sirve con el Baterista IA."),
          "Versilian Studios y Karoryfer Samples", "CC0 (libre)", juce::String (u8"grande (varios cientos de MB)"),
          "https://github.com/sfzinstruments/virtuosity_drums/archive/refs/heads/master.zip",
          "virtuosity_drums-master", "Programs/01-basic-kit.sfz", true },
        { "black-blue-bass", juce::String (u8"Bajo eléctrico real"), "Bajos",
          juce::String (u8"Bajo tocado con los dedos, con varias intensidades y tomas."),
          "Karoryfer Samples", "Gratis (Karoryfer Samples)", juce::String (u8"grande"),
          "https://github.com/sfzinstruments/karoryfer.black-and-blue-basses/archive/refs/heads/main.zip",
          "karoryfer.black-and-blue-basses-main", "Programs/05-darkblack_pluck.sfz", false },
        { "shiny-guitar", juce::String (u8"Guitarra acústica real"), "Guitarras",
          juce::String (u8"Guitarra archtop grabada con micrófono, nota por nota."),
          "D. Smolken / Karoryfer Samples", "CC0 (libre)", juce::String (u8"mediana"),
          "https://github.com/sfzinstruments/karoryfer.shinyguitar/archive/refs/heads/master.zip",
          "karoryfer.shinyguitar-master", "Programs/acoustic.sfz", false },
    };
    return sounds;
}

const SoundLibrary::Sound* SoundLibrary::find (const juce::String& id)
{
    for (auto& s : catalog()) if (s.id == id) return &s;
    return nullptr;
}

juce::File SoundLibrary::soundsFolder()
{
    if (auto custom = juce::SystemStats::getEnvironmentVariable ("GALPON_SOUNDS_DIR", {}); custom.isNotEmpty())
        return juce::File (custom);     // tests
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Galpon").getChildFile ("Sonidos");
}

juce::File SoundLibrary::installedFile (const juce::String& id) const
{
    auto* s = find (id);
    if (s == nullptr) return {};
    auto f = soundsFolder().getChildFile (s->id).getChildFile (s->folderInZip).getChildFile (s->sfz);
    if (! f.existsAsFile()) return {};
    if (s->id == "virtuosity-drums")
        if (auto kit = drumKitForDrummer (f); kit.existsAsFile()) return kit;
    return f;
}

juce::File SoundLibrary::drumKitForDrummer (const juce::File& basicKit)
{
    // The kit has two toms and puts a rimshot and a cross-stick on the General MIDI tom keys
    // (45, 47). The drummer plays four toms on the GM keys, so Galpon writes its own copy of the
    // kit's program with the toms where the drummer expects them (the samples are the same).
    auto dir = basicKit.getParentDirectory();
    auto kit = dir.getChildFile ("galpon-kit.sfz");
    auto keymap = dir.getChildFile ("keymaps").getChildFile ("keymap_galpon.sfz");
    auto basicMap = dir.getChildFile ("keymaps").getChildFile ("keymap_basic.sfz");
    const int version = 1;
    if (kit.existsAsFile() && keymap.existsAsFile() && kit.loadFileAsString().contains ("galpon-kit v" + juce::String (version))) return kit;
    if (! basicMap.existsAsFile()) return {};

    auto map = basicMap.loadFileAsString();
    const std::pair<const char*, int> toms[] = {
        { "$HTOM_CENTER_KEY", 50 }, { "$HTOM_OFFCENTER_KEY", 47 },   // high, mid
        { "$LTOM_CENTER_KEY", 45 }, { "$LTOM_OFFCENTER_KEY", 43 },   // low, floor
        { "$LTOM_RIMSHOT_KEY", 41 }, { "$LTOM_CROSSSTICK_KEY", 48 } };
    juce::StringArray lines;
    lines.addLines (map);
    for (auto& line : lines)
        for (auto& [name, key] : toms)
            if (line.trim().startsWith ("#define") && line.contains (juce::String (name) + " ") ) line = "#define " + juce::String (name) + " " + juce::String (key);
    if (! keymap.replaceWithText (lines.joinIntoString ("\n"))) return {};

    auto program = basicKit.loadFileAsString().replace ("keymaps/keymap_basic.sfz", "keymaps/keymap_galpon.sfz");
    if (! program.contains ("keymap_galpon")) return {};
    if (! kit.replaceWithText ("// galpon-kit v" + juce::String (version) + ": the basic kit with the toms on the General MIDI keys\n" + program)) return {};
    return kit;
}

juce::Result SoundLibrary::install (const Sound& s, const juce::File& zipFile, std::function<void (float)> progress)
{
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0) return juce::Result::fail (juce::String (u8"El archivo descargado está dañado"));
    auto dest = soundsFolder().getChildFile (s.id);
    auto temp = soundsFolder().getChildFile (s.id + ".parcial");
    temp.deleteRecursively();
    temp.createDirectory();
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        auto r = zip.uncompressEntry (i, temp, true);
        if (r.failed()) { temp.deleteRecursively(); return juce::Result::fail ("No se pudo descomprimir: " + r.getErrorMessage()); }
        if (progress && i % 32 == 0) progress ((float) i / (float) zip.getNumEntries());
    }
    if (! temp.getChildFile (s.folderInZip).getChildFile (s.sfz).existsAsFile())
    {
        temp.deleteRecursively();
        return juce::Result::fail (juce::String (u8"La descarga no tiene el instrumento esperado (") + s.sfz + ")");
    }
    dest.deleteRecursively();
    if (! temp.moveFileTo (dest)) return juce::Result::fail ("No se pudo guardar en " + dest.getFullPathName());
    return juce::Result::ok();
}

juce::Result SoundLibrary::download (const juce::String& id)
{
    auto* s = find (id);
    if (s == nullptr) return juce::Result::fail ("Sonido desconocido");
    if (auto it = jobs.find (id); it != jobs.end() && it->second->phase < 2) return juce::Result::ok();   // already going
    soundsFolder().createDirectory();
    auto job = std::make_unique<Job>();
    job->sound = *s;
    job->start();
    jobs[id] = std::move (job);
    return juce::Result::ok();
}

void SoundLibrary::cancel (const juce::String& id) { jobs.erase (id); }

juce::var SoundLibrary::describe() const
{
    juce::Array<juce::var> list;
    for (auto& s : catalog())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", s.id);
        o->setProperty ("name", s.name);
        o->setProperty ("group", s.group);
        o->setProperty ("description", s.description);
        o->setProperty ("author", s.author);
        o->setProperty ("licence", s.licence);
        o->setProperty ("size", s.size);
        o->setProperty ("drums", s.drums);
        o->setProperty ("installed", installedFile (s.id).existsAsFile());
        o->setProperty ("file", installedFile (s.id).getFullPathName());
        o->setProperty ("downloading", jobs.count (s.id) > 0 && jobs.at (s.id)->phase < 2);
        list.add (juce::var (o));
    }
    return list;
}

juce::var SoundLibrary::progress()
{
    juce::Array<juce::var> list;
    for (auto it = jobs.begin(); it != jobs.end();)
    {
        auto& j = *it->second;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", it->first);
        o->setProperty ("name", j.sound.name);
        o->setProperty ("progress", (double) j.fraction.load());
        o->setProperty ("phase", j.phase.load());
        { const juce::ScopedLock sl (j.lock); o->setProperty ("error", j.error); }
        list.add (juce::var (o));
        // finished jobs are reported once, then forgotten
        if (j.phase >= 2) it = jobs.erase (it);
        else ++it;
    }
    return list;
}

} // namespace galpon
