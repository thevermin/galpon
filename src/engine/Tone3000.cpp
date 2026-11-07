#include "Tone3000.h"
#include <juce_events/juce_events.h>
#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
#endif

namespace galpon
{

namespace
{
    juce::String form (std::initializer_list<std::pair<const char*, juce::String>> kv)
    {
        juce::StringArray parts;
        for (auto& p : kv) parts.add (juce::String (p.first) + "=" + juce::URL::addEscapeChars (p.second, true));
        return parts.joinIntoString ("&");
    }
    juce::String slug (juce::String s)
    {
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim().replaceCharacter (' ', '-');
        return s.substring (0, 60);
    }
}

Tone3000::Tone3000 (const juce::File& settings, const juce::File& downloadFolder)
    : juce::Thread ("Galpon TONE3000"), settingsFile (settings), folder (downloadFolder)
{
    loadSettings();
    startThread (juce::Thread::Priority::low);
}

Tone3000::~Tone3000()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void Tone3000::loadSettings()
{
    auto v = juce::JSON::parse (settingsFile.loadFileAsString());
    clientId = v["clientId"].toString();
    accessToken = v["access"].toString();
    refreshToken = v["refresh"].toString();
    expiresAtMs = (double) v["expires"];
    userName = v["user"].toString();
    if (auto* f = v["files"].getDynamicObject())
        for (auto& kv : f->getProperties())
            if (juce::File (kv.value.toString()).existsAsFile()) files[kv.name.toString()] = kv.value.toString();
    if (auto* t = v["tones"].getArray()) tones = *t;
}

void Tone3000::saveSettings() const
{
    auto* o = new juce::DynamicObject();
    {
        const juce::ScopedLock sl (lock);
        o->setProperty ("clientId", clientId);
        o->setProperty ("access", accessToken);
        o->setProperty ("refresh", refreshToken);
        o->setProperty ("expires", expiresAtMs);
        o->setProperty ("user", userName);
        auto* f = new juce::DynamicObject();
        for (auto& kv : files) f->setProperty (kv.first, kv.second);
        o->setProperty ("files", juce::var (f));
        o->setProperty ("tones", tones);
    }
    settingsFile.getParentDirectory().createDirectory();
    settingsFile.replaceWithText (juce::JSON::toString (juce::var (o)));
   #if JUCE_MAC || JUCE_LINUX
    ::chmod (settingsFile.getFullPathName().toRawUTF8(), 0600);     // the sign-in stays readable only by you
   #endif
}

juce::var Tone3000::status() const
{
    const juce::ScopedLock sl (lock);
    auto* o = new juce::DynamicObject();
    o->setProperty ("hasKey", clientId.isNotEmpty());
    o->setProperty ("connected", refreshToken.isNotEmpty() || accessToken.isNotEmpty());
    o->setProperty ("user", userName);
    o->setProperty ("busy", busy);
    o->setProperty ("error", error);
    if (loginPending)
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("code", userCode);
        p->setProperty ("url", verifyUrl);
        o->setProperty ("login", juce::var (p));
    }
    o->setProperty ("tones", tones);
    auto* f = new juce::DynamicObject();
    for (auto& kv : files) f->setProperty (kv.first, kv.second);
    o->setProperty ("files", juce::var (f));
    auto* d = new juce::DynamicObject();
    for (auto& kv : downloading) d->setProperty (kv.first, kv.second);
    o->setProperty ("downloading", juce::var (d));
    return juce::var (o);
}

void Tone3000::setError (const juce::String& e)
{
    const juce::ScopedLock sl (lock);
    error = e;
}

void Tone3000::post (std::function<void()> job)
{
    {
        const juce::ScopedLock sl (jobLock);
        jobs.push_back (std::move (job));
    }
    wake.signal();
}

void Tone3000::run()
{
    while (! threadShouldExit())
    {
        std::function<void()> job;
        {
            const juce::ScopedLock sl (jobLock);
            if (! jobs.empty()) { job = std::move (jobs.front()); jobs.pop_front(); }
        }
        if (job)
        {
            { const juce::ScopedLock sl (lock); busy = true; }
            job();
            { const juce::ScopedLock sl (lock); busy = false; }
        }
        else wake.wait (500);
    }
}

juce::var Tone3000::request (const juce::String& method, const juce::String& path, const juce::String& formBody, bool auth, int& statusCode)
{
    statusCode = 0;
    juce::URL url (path.startsWith ("http") ? path : juce::String (base) + path);
    juce::String headers;
    if (auth) { const juce::ScopedLock sl (lock); headers << "Authorization: Bearer " << accessToken << "\r\n"; }
    if (method == "POST")
    {
        url = url.withPOSTData (formBody);
        headers << "Content-Type: application/x-www-form-urlencoded\r\n";
    }
    headers << "Accept: application/json\r\nUser-Agent: Galpon\r\n";
    auto options = juce::URL::InputStreamOptions (method == "POST" ? juce::URL::ParameterHandling::inPostData : juce::URL::ParameterHandling::inAddress)
                       .withExtraHeaders (headers)
                       .withConnectionTimeoutMs (15000)
                       .withStatusCode (&statusCode)
                       .withHttpRequestCmd (method);
    auto stream = url.createInputStream (options);
    if (stream == nullptr) return {};
    return juce::JSON::parse (stream->readEntireStreamAsString());
}

bool Tone3000::ensureToken()
{
    juce::String refresh, id;
    {
        const juce::ScopedLock sl (lock);
        if (accessToken.isNotEmpty() && juce::Time::currentTimeMillis() < expiresAtMs - 60000) return true;
        refresh = refreshToken; id = clientId;
    }
    if (refresh.isEmpty()) return false;
    int code = 0;
    auto r = request ("POST", "/oauth/token", form ({ { "grant_type", "refresh_token" }, { "refresh_token", refresh }, { "client_id", id } }), false, code);
    if (code == 200 && r["access_token"].toString().isNotEmpty())
    {
        {
            const juce::ScopedLock sl (lock);
            accessToken = r["access_token"].toString();
            if (r["refresh_token"].toString().isNotEmpty()) refreshToken = r["refresh_token"].toString();
            expiresAtMs = (double) juce::Time::currentTimeMillis() + 1000.0 * (double) r["expires_in"];
        }
        saveSettings();
        return true;
    }
    if (code == 400 || code == 401)
    {
        { const juce::ScopedLock sl (lock); accessToken.clear(); refreshToken.clear(); }
        saveSettings();
        setError (juce::String (u8"Se venció tu sesión de TONE3000: conéctate de nuevo."));
    }
    else setError (juce::String (u8"No pude hablar con TONE3000 (¿hay internet?)."));
    return false;
}

juce::Result Tone3000::connect (const juce::String& key)
{
    {
        const juce::ScopedLock sl (lock);
        if (key.trim().isNotEmpty()) clientId = key.trim();
        if (clientId.isEmpty()) return juce::Result::fail (juce::String (u8"Falta la clave pública de TONE3000"));
        if (! clientId.startsWith ("t3k_pub_"))
            return juce::Result::fail (juce::String (u8"Esa no parece la clave pública (empieza con t3k_pub_). Nunca pegues la secreta."));
        error.clear();
    }
    saveSettings();
    post ([this]
    {
        int code = 0;
        juce::String id;
        { const juce::ScopedLock sl (lock); id = clientId; }
        auto r = request ("POST", "/oauth/device_authorization", form ({ { "client_id", id }, { "scope", "read" } }), false, code);
        if (code != 200 || r["device_code"].toString().isEmpty())
        {
            setError (code == 0 ? juce::String (u8"No pude llegar a TONE3000 (¿hay internet?).")
                                : juce::String (u8"TONE3000 no aceptó la clave (") + juce::String (code) + "). Revisa que sea la pública.");
            return;
        }
        {
            const juce::ScopedLock sl (lock);
            loginPending = true;
            userCode = r["user_code"].toString();
            verifyUrl = r["verification_uri_complete"].toString().isNotEmpty() ? r["verification_uri_complete"].toString() : r["verification_uri"].toString();
        }
        juce::MessageManager::callAsync ([u = verifyUrl] { juce::URL (u).launchInDefaultBrowser(); });
        pollDeviceLogin (r["device_code"].toString(), juce::jmax (2, (int) r["interval"]),
                         juce::Time::getMillisecondCounterHiRes() + 1000.0 * juce::jmax (60, (int) r["expires_in"]));
    });
    return juce::Result::ok();
}

void Tone3000::pollDeviceLogin (juce::String deviceCode, int interval, double expiresAt)
{
    juce::String id;
    { const juce::ScopedLock sl (lock); id = clientId; }
    while (! threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < expiresAt)
    {
        {
            const juce::ScopedLock sl (lock);
            if (! loginPending) return;           // cancelled
        }
        wait (interval * 1000);
        int code = 0;
        auto r = request ("POST", "/oauth/token", form ({ { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
                                                          { "device_code", deviceCode }, { "client_id", id } }), false, code);
        if (code == 200 && r["access_token"].toString().isNotEmpty())
        {
            {
                const juce::ScopedLock sl (lock);
                accessToken = r["access_token"].toString();
                refreshToken = r["refresh_token"].toString();
                expiresAtMs = (double) juce::Time::currentTimeMillis() + 1000.0 * (double) r["expires_in"];
                loginPending = false;
            }
            int c2 = 0;
            auto user = request ("GET", "/user", {}, true, c2);
            {
                const juce::ScopedLock sl (lock);
                userName = user["display_name"].toString().isNotEmpty() ? user["display_name"].toString() : user["username"].toString();
            }
            saveSettings();
            fetchLists();
            return;
        }
        const auto err = r["error"].toString();
        if (err == "slow_down") interval += 5;
        else if (err != "authorization_pending" && err.isNotEmpty())
        {
            { const juce::ScopedLock sl (lock); loginPending = false; }
            setError (err == "access_denied" ? juce::String (u8"Cancelaste el permiso en TONE3000.") : juce::String (u8"No se pudo conectar con TONE3000: ") + err);
            return;
        }
    }
    { const juce::ScopedLock sl (lock); loginPending = false; }
}

void Tone3000::disconnect()
{
    {
        const juce::ScopedLock sl (lock);
        accessToken.clear(); refreshToken.clear(); userName.clear(); loginPending = false; tones.clear(); error.clear();
    }
    saveSettings();
}

void Tone3000::refresh() { post ([this] { fetchLists(); }); }

void Tone3000::fetchLists()
{
    if (! ensureToken()) return;
    juce::Array<juce::var> all;
    juce::StringArray seen;
    for (auto list : { "favorited", "downloaded", "created" })
        for (int page = 1; page <= 5; ++page)
        {
            int code = 0;
            auto r = request ("GET", juce::String ("/tones/") + list + "?page_size=100&page=" + juce::String (page), {}, true, code);
            if (code != 200) { if (code == 401) setError (juce::String (u8"TONE3000 pidió que te conectes de nuevo.")); break; }
            if (auto* data = r["data"].getArray())
                for (auto& t : *data)
                {
                    const auto id = t["id"].toString();
                    if (seen.contains (id)) continue;
                    seen.add (id);
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("id", id);
                    o->setProperty ("title", t["title"]);
                    o->setProperty ("gear", t["gear"]);
                    juce::StringArray makes, tags;
                    if (auto* m = t["makes"].getArray()) for (auto& x : *m) makes.add (x["name"].toString());
                    if (auto* g = t["tags"].getArray()) for (auto& x : *g) tags.add (x.isObject() ? x["name"].toString() : x.toString());
                    o->setProperty ("makes", makes.joinIntoString (", "));
                    o->setProperty ("tags", tags.joinIntoString (", "));
                    o->setProperty ("favorite", juce::String (list) == "favorited");
                    o->setProperty ("url", t["url"]);
                    all.add (juce::var (o));
                }
            if ((int) r["total_pages"] <= page) break;
        }
    {
        const juce::ScopedLock sl (lock);
        tones = all;
        error.clear();
    }
    saveSettings();
}

void Tone3000::download (const juce::String& toneId)
{
    {
        const juce::ScopedLock sl (lock);
        if (downloading.count (toneId) && downloading[toneId].isEmpty()) return;
        downloading[toneId] = "";
    }
    post ([this, toneId] { fetchModel (toneId); });
}

void Tone3000::fetchModel (const juce::String& toneId)
{
    auto failWith = [this, &toneId] (const juce::String& e) { const juce::ScopedLock sl (lock); downloading[toneId] = e; };
    if (! ensureToken()) { failWith (juce::String (u8"Conéctate a TONE3000")); return; }
    int code = 0;
    // A1 models first (every NAM player opens them), then A2
    juce::var chosen;
    for (auto arch : { "", "&architecture=2" })
    {
        auto r = request ("GET", "/models?tone_id=" + toneId + "&page_size=100" + arch, {}, true, code);
        if (auto* list = r["data"].getArray())
        {
            for (auto& m : *list)
            {
                const auto url = m["model_url"].toString(), name = m["name"].toString().toLowerCase();
                if (url.isEmpty() || ! (url.containsIgnoreCase (".nam") || name.endsWith (".nam") || ! m["architecture_version"].isVoid())) continue;
                // prefer the standard size: lighter ones lose detail, bigger ones cost CPU
                const bool standard = name.contains ("standard") || m["size"].toString().containsIgnoreCase ("standard");
                if (chosen.isVoid() || standard) chosen = m;
                if (standard) break;
            }
        }
        if (! chosen.isVoid()) break;
    }
    if (chosen.isVoid()) { failWith (juce::String (u8"Ese tono no tiene un modelo NAM para descargar")); return; }

    juce::String title = toneId;
    {
        const juce::ScopedLock sl (lock);
        for (auto& t : tones) if (t["id"].toString() == toneId) title = t["title"].toString();
    }
    folder.createDirectory();
    auto file = folder.getChildFile (toneId + "-" + slug (title) + ".nam");
    juce::String headers;
    { const juce::ScopedLock sl (lock); headers << "Authorization: Bearer " << accessToken << "\r\nUser-Agent: Galpon\r\n"; }
    int status = 0;
    auto stream = juce::URL (chosen["model_url"].toString()).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withExtraHeaders (headers).withConnectionTimeoutMs (20000).withStatusCode (&status));
    if (stream == nullptr || (status != 0 && status != 200)) { failWith (juce::String (u8"No se pudo bajar el modelo (") + juce::String (status) + ")"); return; }
    juce::MemoryBlock mb;
    stream->readIntoMemoryBlock (mb);
    if (mb.getSize() < 64) { failWith (juce::String (u8"El modelo llegó vacío")); return; }
    auto tmp = file.getSiblingFile (file.getFileName() + ".part");
    if (! tmp.replaceWithData (mb.getData(), mb.getSize()) || ! tmp.moveFileTo (file)) { failWith ("No se pudo guardar el modelo"); return; }
    {
        const juce::ScopedLock sl (lock);
        files[toneId] = file.getFullPathName();
        downloading.erase (toneId);
    }
    saveSettings();
}

} // namespace galpon
