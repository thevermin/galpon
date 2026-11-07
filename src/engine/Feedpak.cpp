#include "Feedpak.h"
#include "Yaml.h"

namespace galpon
{

bool Feedpak::isSafePath (const juce::String& p)
{
    if (p.startsWith ("./")) return isSafePath (p.substring (2));
    if (p.isEmpty() || p.startsWithChar ('/') || p.containsChar ('\\') || p.containsChar (':') || p.contains ("//"))
        return false;
    for (auto& seg : juce::StringArray::fromTokens (p, "/", ""))
        if (seg.isEmpty() || seg == ".." || seg == ".") return false;
    return true;
}

juce::String Feedpak::stripJsonComments (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes (s.getNumBytesAsUTF8());
    bool inString = false;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        if (inString)
        {
            out << juce::String::charToString (c);
            if (c == '\\' && i + 1 < s.length()) { out << juce::String::charToString (s[++i]); continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; out << "\""; continue; }
        if (c == '/' && i + 1 < s.length() && s[i + 1] == '/')
        {
            while (i < s.length() && s[i] != '\n') ++i;
            out << "\n";
            continue;
        }
        if (c == '/' && i + 1 < s.length() && s[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < s.length() && ! (s[i] == '*' && s[i + 1] == '/')) ++i;
            ++i;
            out << " ";
            continue;
        }
        out << juce::String::charToString (c);
    }
    return out;
}

juce::Result Feedpak::open (const juce::File& f)
{
    location = f;
    zip.reset();
    zipPrefix.clear();
    zipIndex.clear();
    manifestName = "manifest.yaml";
    isZip = f.existsAsFile();
    if (! f.exists()) return juce::Result::fail ("No existe " + f.getFullPathName());
    if (isZip)
    {
        zip = std::make_unique<juce::ZipFile> (f);
        if (zip->getNumEntries() == 0) return juce::Result::fail (juce::String (u8"No es un .feedpak válido (no se pudo abrir el zip)"));
        // names as tools store them: "./a.json", "a\\b.json"… all read the same
        zipIndex.clear();
        for (int i = 0; i < zip->getNumEntries(); ++i)
        {
            auto name = zip->getEntry (i)->filename.replaceCharacter ('\\', '/');
            while (name.startsWith ("./")) name = name.substring (2);
            name = name.replace ("/./", "/");
            if (name.isNotEmpty() && ! name.endsWithChar ('/')) zipIndex.emplace (name, i);
        }
        // zipped together with its folder ("song.feedpak/manifest.yaml")
        auto has = [this] (const juce::String& n) { return zipIndex.count (n) > 0; };
        if (! has ("manifest.yaml") && ! has ("manifest.yml"))
            for (auto& kv : zipIndex)
                for (auto m : { "/manifest.yaml", "/manifest.yml" })
                    if (kv.first.endsWith (m) && ! kv.first.upToLastOccurrenceOf ("/", false, false).containsChar ('/'))
                    {
                        zipPrefix = kv.first.upToLastOccurrenceOf ("/", true, false);
                        break;
                    }
        if (! has (zipPrefix + "manifest.yaml") && has (zipPrefix + "manifest.yml")) manifestName = "manifest.yml";
    }
    else if (! f.getChildFile ("manifest.yaml").existsAsFile() && f.getChildFile ("manifest.yml").existsAsFile())
        manifestName = "manifest.yml";
    juce::String text;
    if (auto r = readText (manifestName, text); r.failed())
        return juce::Result::fail (juce::String (u8"Falta manifest.yaml: no parece un .feedpak"));
    juce::String err;
    manifestVar = parseYaml (text, err);
    if (err.isNotEmpty()) return juce::Result::fail ("manifest.yaml: " + err);
    if (! manifestVar.isObject()) return juce::Result::fail ("manifest.yaml vacío");
    if (manifestVar["title"].toString().isEmpty() && manifestVar["artist"].toString().isEmpty())
        return juce::Result::fail (juce::String (u8"El manifiesto no dice título ni artista"));
    if (! manifestVar["arrangements"].isArray() || manifestVar["arrangements"].size() == 0)
        return juce::Result::fail ("El .feedpak no tiene partes para tocar");
    return juce::Result::ok();
}

juce::Result Feedpak::readBytes (const juce::String& pathIn, juce::MemoryBlock& out) const
{
    if (! isSafePath (pathIn)) return juce::Result::fail (juce::String (u8"Ruta inválida en el .feedpak: ") + pathIn);
    auto path = pathIn;
    while (path.startsWith ("./")) path = path.substring (2);
    out.reset();
    if (isZip)
    {
        if (zip == nullptr) return juce::Result::fail ("Paquete cerrado");
        const auto it = zipIndex.find (zipPrefix + path);
        const auto* entry = it != zipIndex.end() ? zip->getEntry (it->second) : nullptr;
        if (entry == nullptr) return juce::Result::fail ("Falta " + path + " en el .feedpak");
        std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (*entry));
        if (in == nullptr) return juce::Result::fail ("No se pudo leer " + path);
        in->readIntoMemoryBlock (out);
        return juce::Result::ok();
    }
    auto file = location.getChildFile (path);
    if (! file.isAChildOf (location)) return juce::Result::fail (juce::String (u8"Ruta inválida en el .feedpak: ") + path);
    if (! file.existsAsFile() || ! file.loadFileAsData (out)) return juce::Result::fail ("Falta " + path + " en el .feedpak");
    return juce::Result::ok();
}

juce::Result Feedpak::readText (const juce::String& path, juce::String& out) const
{
    juce::MemoryBlock mb;
    auto r = readBytes (path, mb);
    if (r.failed()) return r;
    out = juce::String::createStringFromData (mb.getData(), (int) mb.getSize());
    return juce::Result::ok();
}

juce::var Feedpak::readJson (const juce::String& path, juce::String& error) const
{
    juce::String text;
    if (auto r = readText (path, text); r.failed()) { error = r.getErrorMessage(); return {}; }
    if (path.endsWithIgnoreCase (".jsonc")) text = stripJsonComments (text);
    juce::var v;
    auto r = juce::JSON::parse (text, v);
    if (r.failed()) { error = path + ": " + r.getErrorMessage(); return {}; }
    return v;
}

std::unique_ptr<juce::AudioBuffer<float>> Feedpak::readAudio (const juce::String& path, double& sampleRate, juce::String& error) const
{
    juce::MemoryBlock mb;
    if (auto r = readBytes (path, mb); r.failed()) { error = r.getErrorMessage(); return nullptr; }
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    // by extension first (the spec dispatches on it), then let JUCE sniff
    for (int i = 0; i < fm.getNumKnownFormats() && reader == nullptr; ++i)
    {
        auto* format = fm.getKnownFormat (i);
        if (! format->canHandleFile (juce::File ("/x/" + path.fromLastOccurrenceOf ("/", false, false)))) continue;
        reader.reset (format->createReaderFor (new juce::MemoryInputStream (mb, false), true));
    }
    if (reader == nullptr)
        reader.reset (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (mb, false)));
    if (reader == nullptr)
    {
        error = juce::String (u8"No sé leer el audio ") + path + juce::String (u8" (formato no soportado en esta computadora)");
        return nullptr;
    }
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60 * 30));
    auto buf = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jlimit (1u, 2u, reader->numChannels), juce::jmax (1, len));
    reader->read (buf.get(), 0, len, 0, true, buf->getNumChannels() > 1);
    sampleRate = reader->sampleRate;
    return buf;
}

juce::var Feedpak::summary (bool withCover) const
{
    auto* o = new juce::DynamicObject();
    const auto& m = manifestVar;
    o->setProperty ("path", location.getFullPathName());
    o->setProperty ("title", m["title"]);
    o->setProperty ("artist", m["artist"]);
    o->setProperty ("album", m["album"]);
    o->setProperty ("year", m["year"]);
    o->setProperty ("duration", m["duration"]);
    juce::Array<juce::var> arrs;
    if (auto* a = m["arrangements"].getArray())
        for (auto& av : *a)
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("id", av["id"]);
            x->setProperty ("name", av["name"].isVoid() ? av["id"] : av["name"]);
            x->setProperty ("type", av["type"]);
            x->setProperty ("tuning", av["tuning"]);
            x->setProperty ("capo", av["capo"]);
            const bool fretted = av["file"].toString().isNotEmpty() && av["type"].toString() != "drums";
            x->setProperty ("playable", fretted);
            arrs.add (juce::var (x));
        }
    o->setProperty ("arrangements", arrs);
    juce::Array<juce::var> stems;
    if (auto* s = m["stems"].getArray())
        for (auto& sv : *s) stems.add (sv["id"]);
    o->setProperty ("stems", stems);
    if (withCover)
    {
        const auto cover = m["cover"].toString();
        juce::MemoryBlock mb;
        if (cover.isNotEmpty() && readBytes (cover, mb).wasOk() && mb.getSize() < 3 * 1024 * 1024)
        {
            const auto mime = cover.endsWithIgnoreCase (".png") ? "image/png" : "image/jpeg";
            o->setProperty ("cover", juce::String ("data:") + mime + ";base64," + juce::Base64::toBase64 (mb.getData(), mb.getSize()));
        }
    }
    return juce::var (o);
}

juce::var Feedpak::loadForGame (juce::String& error) const
{
    auto* o = new juce::DynamicObject();
    juce::var result (o);
    o->setProperty ("summary", summary (true));
    o->setProperty ("manifest", manifestVar);

    juce::var firstArrangement;
    auto* arrangements = new juce::DynamicObject();
    if (auto* a = manifestVar["arrangements"].getArray())
        for (auto& av : *a)
        {
            const auto file = av["file"].toString();
            if (file.isEmpty() || av["type"].toString() == "drums") continue;
            juce::String err;
            auto data = readJson (file, err);
            if (! data.isObject()) { if (error.isEmpty()) error = err; continue; }
            // manifest-level tuning / capo / centOffset override the ones inside the file
            if (auto* d = data.getDynamicObject())
            {
                for (auto key : { "tuning", "capo", "centOffset" })
                    if (! av[key].isVoid()) d->setProperty (key, av[key]);
                if (! av["name"].isVoid()) d->setProperty ("name", av["name"]);
                d->setProperty ("type", av["type"]);
            }
            if (firstArrangement.isVoid()) firstArrangement = data;
            arrangements->setProperty (av["id"].toString(), data);
        }
    o->setProperty ("arrangements", juce::var (arrangements));

    // song-wide beats and sections: song_timeline.json wins over the first arrangement's (spec §6.1)
    juce::var beats = firstArrangement["beats"], sections = firstArrangement["sections"], tempos, signatures;
    if (const auto tl = manifestVar["song_timeline"].toString(); tl.isNotEmpty())
    {
        juce::String err;
        auto t = readJson (tl, err);
        if (t["beats"].isArray()) beats = t["beats"];
        if (t["sections"].isArray()) sections = t["sections"];
        tempos = t["tempos"];
        signatures = t["time_signatures"];
    }
    o->setProperty ("beats", beats);
    o->setProperty ("sections", sections);
    o->setProperty ("tempos", tempos);
    o->setProperty ("timeSignatures", signatures);

    if (const auto lyr = manifestVar["lyrics"].toString(); lyr.isNotEmpty())
    {
        juce::String err;
        o->setProperty ("lyrics", readJson (lyr, err));
    }
    return result;
}

} // namespace galpon
