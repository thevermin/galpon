#include "RsXmlImport.h"
#include <map>

namespace galpon
{
namespace RsXmlImport
{
namespace
{
    using Xml = juce::XmlElement;

    juce::String childText (const Xml& root, const char* name)
    {
        if (auto* c = root.getChildByName (name)) return c->getAllSubText().trim();
        return {};
    }

    std::unique_ptr<Xml> load (const juce::File& f)
    {
        auto x = juce::XmlDocument::parse (f);
        return x;
    }

    /** What kind of XML this is, without parsing the whole file (they can be big). */
    juce::String rootTag (const juce::File& f)
    {
        juce::FileInputStream in (f);
        if (! in.openedOk()) return {};
        const auto head = in.readString().substring (0, 4096);
        int i = 0;
        while ((i = head.indexOfChar (i, '<')) >= 0)
        {
            const auto c = head[i + 1];
            if (c != '?' && c != '!')
                return head.substring (i + 1).upToFirstOccurrenceOf (">", false, false).upToFirstOccurrenceOf (" ", false, false).trim().toLowerCase();
            ++i;
        }
        return {};
    }

    double round3 (double v) { return std::round (v * 1000.0) / 1000.0; }

    /** A Rocksmith note (or chordNote) → the feedpak wire form; only what's set, to keep packs small. */
    juce::var noteWire (const Xml& n, bool withTime, bool chordFretHandMute)
    {
        auto* o = new juce::DynamicObject();
        if (withTime) o->setProperty ("t", round3 (n.getDoubleAttribute ("time")));
        o->setProperty ("s", n.getIntAttribute ("string"));
        o->setProperty ("f", n.getIntAttribute ("fret"));
        if (const auto sus = n.getDoubleAttribute ("sustain"); sus > 0.0) o->setProperty ("sus", round3 (sus));
        if (const int sl = n.getIntAttribute ("slideTo", -1); sl >= 0) o->setProperty ("sl", sl);
        if (const int slu = n.getIntAttribute ("slideUnpitchTo", -1); slu >= 0) o->setProperty ("slu", slu);
        double bend = n.getDoubleAttribute ("bend");
        if (auto* bv = n.getChildByName ("bendValues"))
            for (auto* b : bv->getChildIterator()) bend = juce::jmax (bend, b->getDoubleAttribute ("step"));
        if (bend > 0.0) o->setProperty ("bn", round3 (bend));
        auto flag = [&] (const char* attr, const char* key) { if (n.getIntAttribute (attr) != 0) o->setProperty (key, true); };
        flag ("hammerOn", "ho");
        flag ("pullOff", "po");
        flag ("harmonic", "hm");
        flag ("harmonicPinch", "hp");
        flag ("palmMute", "pm");
        flag ("mute", "mt");
        flag ("vibrato", "vb");
        flag ("tremolo", "tr");
        flag ("accent", "ac");
        flag ("tap", "tp");
        flag ("linkNext", "ln");
        flag ("ignore", "ig");
        if (chordFretHandMute) o->setProperty ("fhm", true);
        return juce::var (o);
    }

    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    }

    struct Arr
    {
        juce::String id, name, type;
        juce::Array<juce::var> tuning;
        int capo = 0, notes = 0, events = 0;
        juce::var json;
    };

    /** One arrangement XML (<song>) → the arrangement JSON. Dynamic difficulty is flattened to the
        full chart: each phrase takes the notes of its hardest level. */
    Arr convertArrangement (const Xml& song, const juce::String& fileStem)
    {
        Arr a;
        a.name = childText (song, "arrangement");
        // the file's last part says which one it is (song_lead.xml, song_bass2.xml) when the XML doesn't
        auto tail = fileStem.fromLastOccurrenceOf ("_", false, false).toLowerCase();
        if (tail == fileStem.toLowerCase()) tail = {};
        auto* props = song.getChildByName ("arrangementProperties");
        const bool bass = (props != nullptr && props->getIntAttribute ("pathBass") != 0) || a.name.containsIgnoreCase ("bass") || tail.startsWith ("bass");
        a.type = bass ? "bass" : "guitar";
        if (a.name.isEmpty())
        {
            for (auto known : { "lead", "rhythm", "combo", "bass" })
                if (tail.startsWith (known))
                    a.name = juce::String (known).substring (0, 1).toUpperCase() + juce::String (known).substring (1) + tail.substring ((int) strlen (known));
            if (a.name.isEmpty()) a.name = bass ? "Bass" : (props != nullptr && props->getIntAttribute ("pathRhythm") != 0) ? "Rhythm" : "Lead";
        }

        // id: that same last part, or the arrangement's name
        if (tail.isEmpty()) tail = a.name.toLowerCase();
        a.id = tail.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
        if (a.id.isEmpty()) a.id = bass ? "bass" : "guitar";

        if (auto* t = song.getChildByName ("tuning"))
            for (int i = 0; i < 6; ++i) a.tuning.add (t->getIntAttribute ("string" + juce::String (i)));
        else
            for (int i = 0; i < 6; ++i) a.tuning.add (0);
        a.capo = childText (song, "capo").getIntValue();

        // levels: difficulty → element
        std::map<int, const Xml*> levels;
        if (auto* lv = song.getChildByName ("levels"))
            for (auto* l : lv->getChildWithTagNameIterator ("level")) levels[l->getIntAttribute ("difficulty")] = l;
        std::vector<int> maxDiff;
        if (auto* ph = song.getChildByName ("phrases"))
            for (auto* p : ph->getChildWithTagNameIterator ("phrase")) maxDiff.push_back (p->getIntAttribute ("maxDifficulty"));
        struct Span { double from, to; const Xml* level; };
        std::vector<Span> spans;
        if (auto* its = song.getChildByName ("phraseIterations"); its != nullptr && levels.size() > 1)
        {
            std::vector<std::pair<double, int>> list;
            for (auto* it : its->getChildWithTagNameIterator ("phraseIteration"))
                list.push_back ({ it->getDoubleAttribute ("time"), it->getIntAttribute ("phraseId") });
            for (size_t i = 0; i < list.size(); ++i)
            {
                const int pid = list[i].second;
                const int want = pid >= 0 && pid < (int) maxDiff.size() ? maxDiff[(size_t) pid] : levels.rbegin()->first;
                const Xml* use = nullptr;
                for (auto& [d, l] : levels) if (d <= want) use = l;     // hardest level that exists
                if (use == nullptr) use = levels.begin()->second;
                spans.push_back ({ i == 0 ? -1.0e9 : list[i].first, i + 1 < list.size() ? list[i + 1].first : 1.0e9, use });
            }
        }
        if (spans.empty() && ! levels.empty()) spans.push_back ({ -1.0e9, 1.0e9, levels.rbegin()->second });

        // chord templates
        juce::Array<juce::var> templates;
        std::vector<std::array<int, 6>> templateFrets;
        if (auto* ct = song.getChildByName ("chordTemplates"))
            for (auto* c : ct->getChildWithTagNameIterator ("chordTemplate"))
            {
                juce::Array<juce::var> frets, fingers;
                std::array<int, 6> fr {};
                for (int i = 0; i < 6; ++i)
                {
                    fr[(size_t) i] = c->getIntAttribute ("fret" + juce::String (i), -1);
                    frets.add (fr[(size_t) i]);
                    fingers.add (c->getIntAttribute ("finger" + juce::String (i), -1));
                }
                templateFrets.push_back (fr);
                auto name = c->getStringAttribute ("chordName");
                if (name.isEmpty()) name = c->getStringAttribute ("displayName").upToFirstOccurrenceOf ("-", false, false);
                templates.add (obj ({ { "name", name }, { "frets", frets }, { "fingers", fingers } }));
            }

        juce::Array<juce::var> notes, chords, anchors, handshapes;
        for (auto& sp : spans)
        {
            auto in = [&] (double t) { return t >= sp.from && t < sp.to; };
            if (auto* ns = sp.level->getChildByName ("notes"))
                for (auto* n : ns->getChildWithTagNameIterator ("note"))
                    if (in (n->getDoubleAttribute ("time"))) notes.add (noteWire (*n, true, false));
            if (auto* cs = sp.level->getChildByName ("chords"))
                for (auto* c : cs->getChildWithTagNameIterator ("chord"))
                {
                    const double t = c->getDoubleAttribute ("time");
                    if (! in (t)) continue;
                    const int id = c->getIntAttribute ("chordId");
                    const bool fhm = c->getIntAttribute ("fretHandMute") != 0;
                    juce::Array<juce::var> cn;
                    for (auto* n : c->getChildWithTagNameIterator ("chordNote")) cn.add (noteWire (*n, false, fhm));
                    if (cn.isEmpty() && id >= 0 && id < (int) templateFrets.size())
                        for (int s = 0; s < 6; ++s)
                            if (const int f = templateFrets[(size_t) id][(size_t) s]; f >= 0)
                            {
                                auto nv = obj ({ { "s", s }, { "f", f } });
                                if (c->getIntAttribute ("palmMute") != 0) nv.getDynamicObject()->setProperty ("pm", true);
                                if (fhm) nv.getDynamicObject()->setProperty ("fhm", true);
                                cn.add (nv);
                            }
                    auto cv = obj ({ { "t", round3 (t) }, { "id", id }, { "notes", cn } });
                    if (c->getIntAttribute ("highDensity") != 0) cv.getDynamicObject()->setProperty ("hd", true);
                    chords.add (cv);
                }
            if (auto* as = sp.level->getChildByName ("anchors"))
                for (auto* x : as->getChildWithTagNameIterator ("anchor"))
                    if (in (x->getDoubleAttribute ("time")))
                        anchors.add (obj ({ { "time", round3 (x->getDoubleAttribute ("time")) }, { "fret", x->getIntAttribute ("fret") }, { "width", x->getIntAttribute ("width", 4) } }));
            if (auto* hs = sp.level->getChildByName ("handShapes"))
                for (auto* x : hs->getChildWithTagNameIterator ("handShape"))
                    if (in (x->getDoubleAttribute ("startTime")))
                        handshapes.add (obj ({ { "chord_id", x->getIntAttribute ("chordId") }, { "start_time", round3 (x->getDoubleAttribute ("startTime")) }, { "end_time", round3 (x->getDoubleAttribute ("endTime")) } }));
        }
        auto byTime = [] (const char* key)
        {
            return [key] (const juce::var& x, const juce::var& y) { return (double) x[key] < (double) y[key]; };
        };
        std::stable_sort (notes.begin(), notes.end(), byTime ("t"));
        std::stable_sort (chords.begin(), chords.end(), byTime ("t"));
        std::stable_sort (anchors.begin(), anchors.end(), byTime ("time"));

        juce::Array<juce::var> beats, sections;
        if (auto* eb = song.getChildByName ("ebeats"))
            for (auto* b : eb->getChildWithTagNameIterator ("ebeat"))
                beats.add (obj ({ { "time", round3 (b->getDoubleAttribute ("time")) }, { "measure", b->getIntAttribute ("measure", -1) } }));
        if (auto* ss = song.getChildByName ("sections"))
            for (auto* s : ss->getChildWithTagNameIterator ("section"))
                sections.add (obj ({ { "name", s->getStringAttribute ("name") }, { "time", round3 (s->getDoubleAttribute ("startTime")) }, { "number", s->getIntAttribute ("number", 1) } }));

        int chordNotes = 0;
        for (auto& c : chords) chordNotes += c["notes"].size();
        a.notes = notes.size() + chordNotes;
        a.events = notes.size() + chords.size();

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", a.name);
        o->setProperty ("tuning", a.tuning);
        o->setProperty ("capo", a.capo);
        o->setProperty ("notes", notes);
        o->setProperty ("chords", chords);
        o->setProperty ("anchors", anchors);
        o->setProperty ("handshapes", handshapes);
        o->setProperty ("templates", templates);
        o->setProperty ("beats", beats);
        o->setProperty ("sections", sections);
        o->setProperty ("stats", obj ({ { "events", a.events }, { "notes", a.notes } }));

        // the tone names and where they change (the game shows them; the amp can follow them)
        const auto base = childText (song, "tonebase");
        juce::Array<juce::var> changes;
        if (auto* ts = song.getChildByName ("tones"))
            for (auto* t : ts->getChildWithTagNameIterator ("tone"))
                changes.add (obj ({ { "t", round3 (t->getDoubleAttribute ("time")) }, { "name", t->getStringAttribute ("name") } }));
        if (base.isNotEmpty() || ! changes.isEmpty())
            o->setProperty ("tones", obj ({ { "base", base }, { "changes", changes } }));
        a.json = juce::var (o);
        return a;
    }

    juce::String yamlString (const juce::String& s)
    {
        return "\"" + s.replace ("\\", "\\\\").replace ("\"", "\\\"").replace ("\n", " ") + "\"";
    }

    juce::String audioExt (const juce::File& f) { return f.getFileExtension().toLowerCase(); }
    bool isAudio (const juce::File& f) { return f.hasFileExtension ("ogg;wav;mp3;flac;m4a;aif;aiff;opus"); }

    // ---- DDS (the cover art): DXT1/3/5 block decoding
    juce::Colour rgb565 (int c) { return juce::Colour ((juce::uint8) (((c >> 11) & 31) * 255 / 31), (juce::uint8) (((c >> 5) & 63) * 255 / 63), (juce::uint8) ((c & 31) * 255 / 31)); }
}

juce::Image decodeDds (const juce::MemoryBlock& data)
{
    const auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 128 || std::memcmp (d, "DDS ", 4) != 0) return {};
    auto u32 = [&] (size_t at) { return (juce::uint32) d[at] | ((juce::uint32) d[at + 1] << 8) | ((juce::uint32) d[at + 2] << 16) | ((juce::uint32) d[at + 3] << 24); };
    const int h = (int) u32 (12), w = (int) u32 (16);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return {};
    const juce::String four = juce::String::fromUTF8 ((const char*) d + 84, 4);
    const juce::uint32 flags = u32 (80), bits = u32 (88);
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
    size_t at = 128;
    if ((flags & 0x4) == 0 && bits == 32)       // plain BGRA
    {
        if (n < at + (size_t) w * (size_t) h * 4) return {};
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x, at += 4)
                px.setPixelColour (x, y, juce::Colour (d[at + 2], d[at + 1], d[at], d[at + 3]));
        return img;
    }
    const int kind = four == "DXT1" ? 1 : four == "DXT3" ? 3 : four == "DXT5" ? 5 : 0;
    if (kind == 0) return {};
    const size_t blockSize = kind == 1 ? 8 : 16;
    for (int by = 0; by < (h + 3) / 4; ++by)
        for (int bx = 0; bx < (w + 3) / 4; ++bx, at += blockSize)
        {
            if (at + blockSize > n) return img;
            juce::uint8 alpha[16];
            std::fill (alpha, alpha + 16, (juce::uint8) 255);
            size_t c = at;
            if (kind == 3)
            {
                for (int i = 0; i < 16; ++i) alpha[i] = (juce::uint8) (((d[at + (size_t) i / 2] >> ((i & 1) * 4)) & 15) * 17);
                c = at + 8;
            }
            else if (kind == 5)
            {
                const int a0 = d[at], a1 = d[at + 1];
                int pal[8] = { a0, a1 };
                if (a0 > a1) for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                else { for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5; pal[6] = 0; pal[7] = 255; }
                juce::uint64 bitsA = 0;
                for (int i = 0; i < 6; ++i) bitsA |= (juce::uint64) d[at + 2 + (size_t) i] << (8 * i);
                for (int i = 0; i < 16; ++i) alpha[i] = (juce::uint8) pal[(bitsA >> (3 * i)) & 7];
                c = at + 8;
            }
            const int c0 = d[c] | (d[c + 1] << 8), c1 = d[c + 2] | (d[c + 3] << 8);
            juce::Colour pal[4] = { rgb565 (c0), rgb565 (c1) };
            if (c0 > c1 || kind != 1) { pal[2] = pal[0].interpolatedWith (pal[1], 1.0f / 3.0f); pal[3] = pal[0].interpolatedWith (pal[1], 2.0f / 3.0f); }
            else { pal[2] = pal[0].interpolatedWith (pal[1], 0.5f); pal[3] = juce::Colours::transparentBlack; }
            const juce::uint32 idx = (juce::uint32) d[c + 4] | ((juce::uint32) d[c + 5] << 8) | ((juce::uint32) d[c + 6] << 16) | ((juce::uint32) d[c + 7] << 24);
            for (int i = 0; i < 16; ++i)
            {
                const int x = bx * 4 + (i & 3), y = by * 4 + i / 4;
                if (x >= w || y >= h) continue;
                auto col = pal[(idx >> (2 * i)) & 3];
                if (kind != 1) col = col.withAlpha (alpha[i]);
                px.setPixelColour (x, y, col);
            }
        }
    return img;
}

juce::Array<juce::File> findSongs (const juce::File& root)
{
    // group arrangement XMLs by song: …/songs/arr/x.xml belongs to the folder above "songs"
    juce::Array<juce::File> songs;
    for (auto& f : root.findChildFiles (juce::File::findFiles, true, "*.xml", juce::File::FollowSymlinks::no))
    {
        if (rootTag (f) != "song") continue;
        auto dir = f.getParentDirectory();
        if (dir.getFileName().equalsIgnoreCase ("arr") && dir.getParentDirectory().getFileName().equalsIgnoreCase ("songs"))
            dir = dir.getParentDirectory().getParentDirectory();
        songs.addIfNotAlreadyThere (dir);
    }
    return songs;
}

Result convert (const juce::File& folder, const juce::File& destFolder)
{
    Result r;
    r.source = folder;
    std::vector<Arr> arrs;
    juce::var lyrics;
    juce::String title, artist, album;
    int year = 0;
    double duration = 0;
    juce::StringArray usedIds;

    auto xmls = folder.findChildFiles (juce::File::findFiles, true, "*.xml", juce::File::FollowSymlinks::no);
    xmls.sort();
    for (auto& f : xmls)
    {
        const auto tag = rootTag (f);
        if (tag == "vocals")
        {
            auto x = load (f);
            if (x == nullptr) continue;
            juce::Array<juce::var> words;
            for (auto* v : x->getChildWithTagNameIterator ("vocal"))
                words.add (obj ({ { "t", round3 (v->getDoubleAttribute ("time")) }, { "d", round3 (v->getDoubleAttribute ("length")) }, { "w", v->getStringAttribute ("lyric") } }));
            if (! words.isEmpty() && lyrics.isVoid()) lyrics = words;
            continue;
        }
        if (tag != "song") continue;
        auto x = load (f);
        if (x == nullptr) { r.warnings.add (f.getFileName() + juce::String (u8": XML dañado")); continue; }
        if (title.isEmpty()) title = childText (*x, "title");
        if (artist.isEmpty()) artist = childText (*x, "artistName");
        if (album.isEmpty()) album = childText (*x, "albumName");
        if (year == 0) year = childText (*x, "albumYear").getIntValue();
        duration = juce::jmax (duration, childText (*x, "songLength").getDoubleValue());
        auto a = convertArrangement (*x, f.getFileNameWithoutExtension());
        if (a.events == 0) { r.warnings.add (f.getFileName() + juce::String (u8": sin notas, no la agrego")); continue; }
        auto id = a.id;
        for (int k = 2; usedIds.contains (id); ++k) id = a.id + juce::String (k);
        a.id = id;
        usedIds.add (id);
        arrs.push_back (std::move (a));
    }
    // XMLs converted from the game's binary charts come without the song's name: the toolkit leaves it
    // in the unpacked manifests (…/manifests/…/*.json → Entries → id → Attributes)
    if (title.isEmpty() || artist.isEmpty())
        for (auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.json;*.hsan", juce::File::FollowSymlinks::no))
        {
            if (f.getSize() > 4 * 1024 * 1024) continue;
            auto v = juce::JSON::parse (f.loadFileAsString());
            auto* entries = v["Entries"].getDynamicObject();
            if (entries == nullptr) continue;
            for (auto& e : entries->getProperties())
            {
                auto at = e.value["Attributes"];
                if (! at.isObject()) continue;
                if (title.isEmpty()) title = at["SongName"].toString();
                if (artist.isEmpty()) artist = at["ArtistName"].toString();
                if (album.isEmpty()) album = at["AlbumName"].toString();
                if (year == 0) year = (int) at["SongYear"];
            }
            if (title.isNotEmpty() && artist.isNotEmpty()) break;
        }
    if (arrs.empty()) { r.error = juce::String (u8"No encontré partes (XML de Rocksmith) en esta carpeta"); return r; }
    std::stable_sort (arrs.begin(), arrs.end(), [] (const Arr& p, const Arr& q)
    {
        auto rank = [] (const Arr& a) { return a.id.startsWith ("lead") ? 0 : a.id.startsWith ("rhythm") ? 1 : a.id.startsWith ("combo") ? 2 : a.type == "bass" ? 3 : 4; };
        return rank (p) < rank (q);
    });

    // audio: the longest decoded file is the song, a shorter one the preview
    juce::Array<juce::File> audio;
    bool onlyWem = false;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, true, "*", juce::File::FollowSymlinks::no))
    {
        if (isAudio (f)) audio.add (f);
        else if (f.hasFileExtension ("wem")) onlyWem = true;
    }
    std::sort (audio.begin(), audio.end(), [] (const juce::File& p, const juce::File& q) { return p.getSize() > q.getSize(); });
    if (audio.isEmpty())
    {
        r.error = onlyWem ? juce::String (u8"El audio está en formato WEM: desempaqueta otra vez con la opción «Decode audio» del toolkit para que quede en OGG")
                          : juce::String (u8"No encontré el audio de la canción (OGG, WAV, MP3 o FLAC)");
        return r;
    }
    const auto song = audio[0];
    juce::File preview;
    for (int i = 1; i < audio.size(); ++i) if (audio[i].getSize() < song.getSize() / 2) { preview = audio[i]; break; }

    // cover: the biggest image (album art is a .dds in unpacked songs)
    juce::Image cover;
    {
        juce::Array<juce::File> imgs = folder.findChildFiles (juce::File::findFiles, true, "*.dds;*.png;*.jpg;*.jpeg", juce::File::FollowSymlinks::no);
        std::sort (imgs.begin(), imgs.end(), [] (const juce::File& p, const juce::File& q) { return p.getSize() > q.getSize(); });
        for (auto& f : imgs)
        {
            if (f.hasFileExtension ("dds")) { juce::MemoryBlock mb; f.loadFileAsData (mb); cover = decodeDds (mb); }
            else cover = juce::ImageFileFormat::loadFrom (f);
            if (cover.isValid()) break;
        }
    }

    if (title.isEmpty())
    {
        title = folder.getFileName();
        for (auto suffix : { "_p_Pc", "_p_Mac", "_m_Pc", "_m_Mac", "_p" })
            if (title.endsWithIgnoreCase (suffix)) { title = title.dropLastCharacters ((int) strlen (suffix)); break; }
        title = title.replaceCharacter ('_', ' ').trim();
    }
    r.title = title;
    r.artist = artist;

    // the pack
    juce::ZipFile::Builder zip;
    juce::OwnedArray<juce::MemoryBlock> keep;     // the builder streams them when writing
    auto addText = [&] (const juce::String& path, const juce::String& text)
    {
        auto* mb = keep.add (new juce::MemoryBlock());
        mb->append (text.toRawUTF8(), text.getNumBytesAsUTF8());
        zip.addEntry (new juce::MemoryInputStream (*mb, false), 9, path, juce::Time::getCurrentTime());
    };
    juce::String y;
    y << "feedpak_version: 1.14.0\n";
    y << "title: " << yamlString (title) << "\n";
    if (artist.isNotEmpty()) y << "artist: " << yamlString (artist) << "\n";
    if (album.isNotEmpty()) y << "album: " << yamlString (album) << "\n";
    if (year > 0) y << "year: " << year << "\n";
    if (duration > 0) y << "duration: " << juce::String (duration, 3) << "\n";
    const auto stemFile = "stems/full" + audioExt (song);
    y << "stems:\n- id: full\n  file: " << stemFile << "\n  default: true\n";
    y << "arrangements:\n";
    for (auto& a : arrs)
    {
        y << "- id: " << a.id << "\n  name: " << yamlString (a.name) << "\n  file: arrangements/" << a.id << ".json\n  type: " << a.type << "\n  tuning:\n";
        for (auto& t : a.tuning) y << "  - " << (int) t << "\n";
        y << "  capo: " << a.capo << "\n  event_count: " << a.events << "\n  note_count: " << a.notes << "\n";
        addText ("arrangements/" + a.id + ".json", juce::JSON::toString (a.json, true));
        r.arrangements.add (a.name);
    }
    if (! lyrics.isVoid())
    {
        y << "lyrics: lyrics.json\nlyrics_source: authored\n";
        addText ("lyrics.json", juce::JSON::toString (lyrics, true));
    }
    // the song's timeline: the first arrangement's beats and sections
    addText ("song_timeline.json", juce::JSON::toString (obj ({ { "version", 1 }, { "beats", arrs.front().json["beats"] }, { "sections", arrs.front().json["sections"] } }), true));
    y << "song_timeline: song_timeline.json\n";
    if (cover.isValid())
    {
        auto* mb = keep.add (new juce::MemoryBlock());
        juce::MemoryOutputStream mo (*mb, false);
        juce::PNGImageFormat png;
        if (png.writeImageToStream (cover, mo))
        {
            mo.flush();
            zip.addEntry (new juce::MemoryInputStream (*mb, false), 0, "cover.png", juce::Time::getCurrentTime());
            y << "cover: cover.png\n";
        }
    }
    else r.warnings.add (juce::String (u8"Sin tapa"));
    zip.addFile (song, 0, stemFile);
    if (preview.existsAsFile())
    {
        zip.addFile (preview, 0, "preview" + audioExt (preview));
        y << "preview: preview" << audioExt (preview) << "\n";
    }
    addText ("manifest.yaml", y);

    destFolder.createDirectory();
    auto name = juce::File::createLegalFileName ((artist.isNotEmpty() ? artist + " - " : juce::String()) + title);
    auto target = destFolder.getChildFile (name + ".feedpak");
    if (target.exists()) target = destFolder.getNonexistentChildFile (name, ".feedpak", false);
    juce::TemporaryFile tmp (target);
    {
        juce::FileOutputStream out (tmp.getFile());
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr)) { r.error = "No se pudo escribir " + target.getFullPathName(); return r; }
    }
    if (! tmp.overwriteTargetFileWithTemporary()) { r.error = "No se pudo guardar " + target.getFullPathName(); return r; }
    r.created = target;
    return r;
}

} // namespace RsXmlImport
} // namespace galpon
