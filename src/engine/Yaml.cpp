#include "Yaml.h"
#include <limits>

namespace galpon
{
namespace
{
    struct Line
    {
        int indent = 0;
        juce::String text;     // without indentation and comments
        juce::String raw;      // the whole line, for block scalars
        int number = 0;
    };

    /** Walks a line keeping track of quotes the YAML way: a quote only opens at the start of a token
        (so the apostrophe in  it's  is just a letter), '' inside single quotes is an apostrophe,
        and \" inside double quotes doesn't close them. Calls f(i, c) for every character outside quotes. */
    template <typename F>
    void outsideQuotes (const juce::String& t, F&& f)
    {
        bool single = false, dbl = false;
        for (int i = 0; i < t.length(); ++i)
        {
            const auto c = t[i];
            if (dbl) { if (c == '\\') ++i; else if (c == '"') dbl = false; continue; }
            if (single) { if (c == '\'') { if (i + 1 < t.length() && t[i + 1] == '\'') ++i; else single = false; } continue; }
            const bool tokenStart = i == 0 || juce::String (" \t[{,:-?").containsChar (t[i - 1]);
            if (c == '"' && tokenStart) { dbl = true; continue; }
            if (c == '\'' && tokenStart) { single = true; continue; }
            if (! f (i, c)) return;
        }
    }

    /** Folds the line breaks inside a quoted scalar's raw text (escapes still in, for double quotes). */
    juce::String foldQuoted (const juce::String& raw, bool dq)
    {
        if (! raw.containsChar ('\n')) return raw;
        auto lines = juce::StringArray::fromTokens (raw, "\n", "");
        juce::String out = lines[0].trimEnd();
        int blank = 0;
        for (int k = 1; k < lines.size(); ++k)
        {
            const bool last = k == lines.size() - 1;
            auto l = last ? lines[k].trimStart() : lines[k].trim();
            if (l.isEmpty() && ! last) { ++blank; continue; }
            int slashes = 0;
            for (int i = out.length() - 1; i >= 0 && out[i] == '\\'; --i) ++slashes;
            if (dq && slashes % 2 == 1) out = out.dropLastCharacters (1);          // escaped break: joined as is
            else if (blank > 0) out << juce::String::repeatedString ("\n", blank);
            else out << " ";
            out << l;
            blank = 0;
        }
        return out;
    }

    /** Removes a trailing "# comment" (a # at the start or after a space, outside quotes). */
    juce::String stripComment (const juce::String& s)
    {
        int cut = -1;
        outsideQuotes (s, [&] (int i, juce::juce_wchar c)
        {
            if (c == '#' && (i == 0 || juce::CharacterFunctions::isWhitespace (s[i - 1]))) { cut = i; return false; }
            return true;
        });
        return (cut >= 0 ? s.substring (0, cut) : s).trimEnd();
    }

    /** A plain (unquoted) scalar, typed the way PyYAML does (YAML 1.1): that's what feedBack reads packs with. */
    juce::var plainScalar (const juce::String& raw)
    {
        auto s = raw.trim();
        if (s.isEmpty() || s == "~" || s == "null" || s == "Null" || s == "NULL") return {};
        for (auto t : { "true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON" }) if (s == t) return true;
        for (auto f : { "false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF" }) if (s == f) return false;
        const auto digits = s.removeCharacters ("_");
        int i = 0;
        if (digits[0] == '-' || digits[0] == '+') ++i;
        const auto body = digits.substring (i);
        auto allDigits = [] (const juce::String& x) { if (x.isEmpty()) return false; for (auto c : x) if (! juce::CharacterFunctions::isDigit (c)) return false; return true; };
        // int: 0 or [1-9][0-9]*; 0x..
        if (allDigits (body) && (body == "0" || body[0] != '0'))
        {
            const auto v = digits.getLargeIntValue();
            if (body.length() < 19)
            {
                if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) return (int) v;
                return (juce::int64) v;
            }
            return digits.getDoubleValue();
        }
        if (body.startsWith ("0x") && body.length() > 2 && body.substring (2).containsOnly ("0123456789abcdefABCDEF"))
            return (juce::int64) ((digits[0] == '-' ? -1 : 1) * body.substring (2).getHexValue64());
        // float: [0-9]*.[0-9]* with an optional e[+-]N (YAML 1.1 wants the sign), or .inf / .nan
        if (body == ".inf" || body == ".Inf" || body == ".INF") return digits[0] == '-' ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        if (body == ".nan" || body == ".NaN" || body == ".NAN") return std::numeric_limits<double>::quiet_NaN();
        const int dot = body.indexOfChar ('.');
        if (dot >= 0)
        {
            auto mant = body, exp = juce::String();
            const int e = body.indexOfAnyOf ("eE");
            if (e >= 0) { mant = body.substring (0, e); exp = body.substring (e + 1); }
            const auto intPart = mant.substring (0, mant.indexOfChar ('.')), frac = mant.substring (mant.indexOfChar ('.') + 1);
            const bool mantOk = (intPart.isEmpty() || allDigits (intPart)) && (frac.isEmpty() || allDigits (frac)) && (intPart.isNotEmpty() || frac.isNotEmpty())
                                && mant.indexOfChar ('.') == mant.lastIndexOfChar ('.');
            const bool expOk = e < 0 || (exp.length() >= 2 && (exp[0] == '+' || exp[0] == '-') && allDigits (exp.substring (1)));
            if (mantOk && expOk && (intPart.isNotEmpty() || frac.isNotEmpty())) return digits.getDoubleValue();
        }
        return s;
    }

    juce::String unquoteDouble (const juce::String& body)
    {
        juce::String out;
        auto hex = [&body] (int from, int count) { return (juce::juce_wchar) body.substring (from, from + count).getHexValue32(); };
        for (int i = 0; i < body.length(); ++i)
        {
            auto c = body[i];
            if (c != '\\' || i + 1 >= body.length()) { out << juce::String::charToString (c); continue; }
            const auto e = body[++i];
            switch (e)
            {
                case 'n': out << "\n"; break;
                case 't': case '\t': out << "\t"; break;
                case 'r': out << "\r"; break;
                case '0': out << juce::String::charToString ((juce::juce_wchar) 0); break;
                case 'a': out << juce::String::charToString ((juce::juce_wchar) 7); break;
                case 'b': out << juce::String::charToString ((juce::juce_wchar) 8); break;
                case 'e': out << juce::String::charToString ((juce::juce_wchar) 0x1b); break;
                case 'f': out << juce::String::charToString ((juce::juce_wchar) 12); break;
                case 'v': out << juce::String::charToString ((juce::juce_wchar) 11); break;
                case 'N': out << juce::String::charToString ((juce::juce_wchar) 0x85); break;
                case '_': out << juce::String::charToString ((juce::juce_wchar) 0xa0); break;
                case 'L': out << juce::String::charToString ((juce::juce_wchar) 0x2028); break;
                case 'P': out << juce::String::charToString ((juce::juce_wchar) 0x2029); break;
                case 'x': if (i + 2 < body.length() + 0) { out << juce::String::charToString (hex (i + 1, 2)); i += 2; } break;
                case 'u': if (i + 4 < body.length() + 0) { out << juce::String::charToString (hex (i + 1, 4)); i += 4; } break;
                case 'U': if (i + 8 < body.length() + 0) { out << juce::String::charToString (hex (i + 1, 8)); i += 8; } break;
                default: out << juce::String::charToString (e); break;      // \" \\ \/ and "\ "
            }
        }
        return out;
    }

    /** Flow style: [a, b], {k: v}, scalars. */
    struct Flow
    {
        const juce::String& s;
        int pos = 0;
        juce::String error;

        void skip() { while (pos < s.length() && juce::CharacterFunctions::isWhitespace (s[pos])) ++pos; }

        juce::var value()
        {
            skip();
            if (pos >= s.length()) return {};
            const auto c = s[pos];
            if (c == '[')
            {
                ++pos;
                juce::Array<juce::var> a;
                skip();
                if (pos < s.length() && s[pos] == ']') { ++pos; return a; }
                while (pos < s.length())
                {
                    a.add (value());
                    skip();
                    if (pos < s.length() && s[pos] == ',') { ++pos; skip(); if (pos < s.length() && s[pos] == ']') { ++pos; return a; } continue; }
                    if (pos < s.length() && s[pos] == ']') { ++pos; return a; }
                    error = "falta ] en una lista"; return a;
                }
                error = "falta ] en una lista";
                return a;
            }
            if (c == '{')
            {
                ++pos;
                auto* o = new juce::DynamicObject();
                juce::var result (o);
                skip();
                if (pos < s.length() && s[pos] == '}') { ++pos; return result; }
                while (pos < s.length())
                {
                    skip();
                    auto key = scalar (true).toString();
                    skip();
                    if (pos < s.length() && s[pos] == ':') ++pos;
                    o->setProperty (key, value());
                    skip();
                    if (pos < s.length() && s[pos] == ',') { ++pos; continue; }
                    if (pos < s.length() && s[pos] == '}') { ++pos; return result; }
                    error = "falta } en un objeto"; return result;
                }
                return result;
            }
            return scalar (false);
        }

        juce::var scalar (bool isKey)
        {
            skip();
            if (pos < s.length() && (s[pos] == '"' || s[pos] == '\''))
            {
                const auto q = s[pos++];
                juce::String body;
                while (pos < s.length())
                {
                    if (q == '\'' && s[pos] == '\'' && pos + 1 < s.length() && s[pos + 1] == '\'') { body << "'"; pos += 2; continue; }
                    if (q == '"' && s[pos] == '\\' && pos + 1 < s.length()) { body << s.substring (pos, pos + 2); pos += 2; continue; }
                    if (s[pos] == q) { ++pos; break; }
                    body << juce::String::charToString (s[pos++]);
                }
                body = foldQuoted (body, q == '"');
                return q == '"' ? juce::var (unquoteDouble (body)) : juce::var (body);
            }
            const int start = pos;
            while (pos < s.length() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && ! (isKey && s[pos] == ':')) ++pos;
            auto text = s.substring (start, pos).replaceCharacters ("\n\r", "  ");
            while (text.contains ("  ")) text = text.replace ("  ", " ");
            return isKey ? juce::var (text.trim()) : plainScalar (text);
        }
    };

    class Parser
    {
    public:
        explicit Parser (const juce::String& text)
        {
            int n = 0;
            for (auto raw : juce::StringArray::fromLines (text))
            {
                ++n;
                raw = raw.replace ("\t", "  ");
                Line l;
                l.raw = raw;
                l.number = n;
                int i = 0;
                while (i < raw.length() && raw[i] == ' ') ++i;
                l.indent = i;
                l.text = stripComment (raw.substring (i));
                all.push_back (l);
            }
        }

        juce::var parse()
        {
            skipBlank();
            if (cur >= (int) all.size()) return juce::var (new juce::DynamicObject());
            if (all[(size_t) cur].text == "---") { ++cur; skipBlank(); }
            auto v = node (all[(size_t) juce::jmin (cur, (int) all.size() - 1)].indent);
            return v;
        }

        juce::String error;

    private:
        std::vector<Line> all;
        int cur = 0;

        void skipBlank()
        {
            while (cur < (int) all.size() && all[(size_t) cur].text.isEmpty()) ++cur;
        }

        static bool isSeqItem (const juce::String& t) { return t == "-" || t.startsWith ("- "); }

        /** Position of the "key: " separator (outside quotes and brackets), or -1. */
        static int keySeparator (const juce::String& t)
        {
            int depth = 0, at = -1;
            outsideQuotes (t, [&] (int i, juce::juce_wchar c)
            {
                if (c == '[' || c == '{') ++depth;
                else if (c == ']' || c == '}') --depth;
                else if (c == ':' && depth == 0 && (i + 1 == t.length() || t[i + 1] == ' ')) { at = i; return false; }
                return true;
            });
            return at;
        }

        static juce::String keyText (const juce::String& k)
        {
            auto s = k.trim();
            if (s.length() >= 2 && ((s.startsWithChar ('"') && s.endsWithChar ('"')) || (s.startsWithChar ('\'') && s.endsWithChar ('\''))))
                return s.startsWithChar ('"') ? unquoteDouble (s.substring (1, s.length() - 1)) : s.substring (1, s.length() - 1).replace ("''", "'");
            return s;
        }

        juce::var node (int indent)
        {
            skipBlank();
            if (cur >= (int) all.size()) return {};
            const auto& l = all[(size_t) cur];
            if (isSeqItem (l.text)) return sequence (l.indent);
            if (keySeparator (l.text) >= 0) return mapping (l.indent);
            juce::ignoreUnused (indent);
            ++cur;
            return inlineValue (l.text, l.indent);
        }

        juce::var sequence (int indent)
        {
            juce::Array<juce::var> items;
            while (true)
            {
                skipBlank();
                if (cur >= (int) all.size()) break;
                auto& l = all[(size_t) cur];
                if (l.indent != indent || ! isSeqItem (l.text)) break;
                auto rest = l.text.substring (1);
                const int pad = rest.length() - rest.trimStart().length();
                rest = rest.trimStart();
                if (rest.isEmpty())
                {
                    ++cur;
                    skipBlank();
                    if (cur < (int) all.size() && all[(size_t) cur].indent > indent) items.add (node (all[(size_t) cur].indent));
                    else items.add ({});
                    continue;
                }
                if (isSeqItem (rest) || keySeparator (rest) >= 0)
                {
                    // "- key: value" starts a mapping (or nested list) indented at the item's content
                    l.indent = indent + 1 + pad;
                    l.text = rest;
                    items.add (node (l.indent));
                    continue;
                }
                ++cur;
                items.add (inlineValue (rest, indent));
            }
            return items;
        }

        juce::var mapping (int indent)
        {
            auto* o = new juce::DynamicObject();
            juce::var result (o);
            while (true)
            {
                skipBlank();
                if (cur >= (int) all.size()) break;
                const auto l = all[(size_t) cur];
                if (l.indent != indent) break;
                const int sep = keySeparator (l.text);
                if (sep < 0 || isSeqItem (l.text))
                {
                    if (l.indent == indent && error.isEmpty())
                        error = "línea " + juce::String (l.number) + ": se esperaba \"clave: valor\"";
                    break;
                }
                const auto key = keyText (l.text.substring (0, sep));
                const auto rest = l.text.substring (sep + 1).trim();
                ++cur;
                if (rest.isEmpty())
                {
                    skipBlank();
                    if (cur < (int) all.size() && (all[(size_t) cur].indent > indent
                                                   || (all[(size_t) cur].indent == indent && isSeqItem (all[(size_t) cur].text))))
                        o->setProperty (key, node (all[(size_t) cur].indent));
                    else
                        o->setProperty (key, juce::var());
                }
                else if (rest.startsWithChar ('|') || rest.startsWithChar ('>'))
                    o->setProperty (key, blockScalar (indent, rest));
                else
                    o->setProperty (key, inlineValue (rest, indent));
            }
            return result;
        }

        juce::var blockScalar (int parentIndent, const juce::String& header)
        {
            const bool folded = header.startsWithChar ('>');
            const bool keep = header.containsChar ('+'), strip = header.containsChar ('-');
            juce::StringArray lines;
            int blockIndent = -1;
            while (cur < (int) all.size())
            {
                const auto& l = all[(size_t) cur];
                const bool blank = l.raw.trim().isEmpty();
                if (! blank && l.indent <= parentIndent) break;
                if (! blank && blockIndent < 0) blockIndent = l.indent;
                lines.add (blank ? juce::String() : l.raw.substring (juce::jmax (0, blockIndent)));
                ++cur;
            }
            while (! keep && ! lines.isEmpty() && lines[lines.size() - 1].isEmpty()) lines.remove (lines.size() - 1);
            juce::String out;
            if (folded)
            {
                for (int i = 0; i < lines.size(); ++i)
                {
                    if (lines[i].isEmpty()) out << "\n";
                    else out << (out.isEmpty() || out.endsWith ("\n") ? "" : " ") << lines[i];
                }
            }
            else out = lines.joinIntoString ("\n");
            if (! strip) out << "\n";
            return out;
        }

        /** A quoted scalar that may go on over the next lines (line breaks fold into spaces, blank lines into breaks). */
        juce::var quoted (const juce::String& first)
        {
            const auto q = first[0];
            // the first line comes comment-stripped; take it from the raw line instead
            juce::String text = first;
            if (cur > 0)
            {
                const auto& raw = all[(size_t) cur - 1].raw;
                const int at = raw.lastIndexOf (first.substring (0, juce::jmin (first.length(), 12)));
                if (at >= 0) text = raw.substring (at);
            }
            auto closed = [q] (const juce::String& t)
            {
                for (int i = 1; i < t.length(); ++i)
                {
                    if (q == '"' && t[i] == '\\') { ++i; continue; }
                    if (t[i] == q) { if (q == '\'' && i + 1 < t.length() && t[i + 1] == '\'') { ++i; continue; } return true; }
                }
                return false;
            };
            while (! closed (text) && cur < (int) all.size()) text << "\n" << all[(size_t) cur++].raw;
            Flow f { text, 0, {} };
            return f.scalar (false);
        }

        juce::var inlineValue (juce::String text, int indent)
        {
            text = text.trim();
            if (text.startsWithChar ('[') || text.startsWithChar ('{'))
            {
                // a flow collection may continue on the next lines
                auto balance = [] (const juce::String& t)
                {
                    int d = 0;
                    outsideQuotes (t, [&] (int, juce::juce_wchar c) { if (c == '[' || c == '{') ++d; else if (c == ']' || c == '}') --d; return true; });
                    // an unclosed quote also keeps the collection open
                    bool sq = false, dq = false;
                    for (int i = 0; i < t.length(); ++i)
                    {
                        const auto c = t[i];
                        if (dq) { if (c == '\\') ++i; else if (c == '"') dq = false; continue; }
                        if (sq) { if (c == '\'') { if (i + 1 < t.length() && t[i + 1] == '\'') ++i; else sq = false; } continue; }
                        const bool start = i == 0 || juce::String (" \t[{,:-?").containsChar (t[i - 1]);
                        if (c == '"' && start) dq = true; else if (c == '\'' && start) sq = true;
                    }
                    return (sq || dq) ? juce::jmax (1, d) : d;
                };
                while (balance (text) > 0 && cur < (int) all.size()) text << "\n" << all[(size_t) cur++].raw;
                Flow f { text, 0, {} };
                auto v = f.value();
                if (f.error.isNotEmpty() && error.isEmpty()) error = f.error;
                return v;
            }
            if (text.startsWithChar ('"') || text.startsWithChar ('\''))
                return quoted (text);
            // plain multi-line scalars: following more-indented lines continue it (blank lines become line breaks)
            int breaks = 0;
            while (cur < (int) all.size())
            {
                const auto& n = all[(size_t) cur];
                if (n.raw.trim().isEmpty()) { ++breaks; ++cur; continue; }
                if (n.indent <= indent || n.text.isEmpty()) break;
                text << (breaks > 0 ? juce::String::repeatedString ("\n", breaks) : juce::String (" ")) << n.text;
                breaks = 0;
                ++cur;
            }
            if (breaks > 0)
            {
                // the blank lines belonged to what follows: give them back
                cur -= breaks;
            }
            return plainScalar (text);
        }
    };
}

juce::var parseYaml (const juce::String& text, juce::String& error)
{
    Parser p (text);
    auto v = p.parse();
    error = p.error;
    return v;
}

} // namespace galpon
