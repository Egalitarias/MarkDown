#include "markdown.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <regex>
#include <vector>

using std::string;
using std::vector;

namespace {

string Esc(const string& s) {
    string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

string Trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

string LTrim(const string& s) {
    size_t a = 0;
    while (a < s.size() && isspace((unsigned char)s[a])) a++;
    return s.substr(a);
}

bool IsBlank(const string& s) {
    for (char c : s) if (!isspace((unsigned char)c)) return false;
    return true;
}

size_t Indent(const string& s) {
    size_t n = 0;
    while (n < s.size() && s[n] == ' ') n++;
    return n;
}

bool IsAlnum(char c) { return isalnum((unsigned char)c) != 0 || (unsigned char)c >= 0x80; }

string Lower(string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

string NormLabel(const string& s) {
    string o;
    bool sp = false;
    for (char c : s) {
        if (isspace((unsigned char)c)) { sp = true; continue; }
        if (sp && !o.empty()) o += ' ';
        sp = false;
        o += (char)tolower((unsigned char)c);
    }
    return o;
}

bool StartsWith(const string& s, size_t pos, const char* lit) {
    size_t n = strlen(lit);
    return s.compare(pos, n, lit) == 0;
}

struct Marker {
    bool ok = false;
    size_t indent = 0, content = 0;
    bool ordered = false;
    char type = 0;     // bullet char, or '.' / ')' for ordered
    int start = 1;
};

Marker ParseMarker(const string& l) {
    Marker m;
    size_t p = Indent(l);
    if (p > 3 || p >= l.size()) return m;
    size_t q = p;
    if (l[q] == '-' || l[q] == '*' || l[q] == '+') {
        m.type = l[q];
        q++;
    } else if (isdigit((unsigned char)l[q])) {
        size_t d = q;
        while (d < l.size() && isdigit((unsigned char)l[d])) d++;
        if (d - q > 9 || d >= l.size() || (l[d] != '.' && l[d] != ')')) return m;
        m.ordered = true;
        m.type = l[d];
        m.start = atoi(l.substr(q, d - q).c_str());
        q = d + 1;
    } else {
        return m;
    }
    if (q < l.size() && l[q] != ' ') return m;
    size_t sp = 0;
    while (q + sp < l.size() && l[q + sp] == ' ') sp++;
    if (q + sp >= l.size() || sp > 4) sp = 1;  // blank item or code-indented content
    m.ok = true;
    m.indent = p;
    m.content = q + sp;
    return m;
}

bool IsHr(const string& l) {
    static const std::regex re(R"(^ {0,3}(?:(?:\*[ \t]*){3,}|(?:-[ \t]*){3,}|(?:_[ \t]*){3,})$)");
    return std::regex_match(l, re);
}

bool IsAtx(const string& l, int& level, string& text) {
    size_t p = Indent(l);
    if (p > 3) return false;
    size_t q = p;
    while (q < l.size() && l[q] == '#') q++;
    int lv = (int)(q - p);
    if (lv < 1 || lv > 6) return false;
    if (q < l.size() && l[q] != ' ' && l[q] != '\t') return false;
    string t = Trim(l.substr(q));
    // strip optional closing sequence
    size_t e = t.size();
    while (e > 0 && t[e - 1] == '#') e--;
    if (e < t.size() && (e == 0 || t[e - 1] == ' ')) t = Trim(t.substr(0, e));
    level = lv;
    text = t;
    return true;
}

bool IsFence(const string& l, char& ch, size_t& n, string& info) {
    size_t p = Indent(l);
    if (p > 3 || p >= l.size()) return false;
    char c = l[p];
    if (c != '`' && c != '~') return false;
    size_t q = p;
    while (q < l.size() && l[q] == c) q++;
    if (q - p < 3) return false;
    string rest = Trim(l.substr(q));
    if (c == '`' && rest.find('`') != string::npos) return false;
    ch = c;
    n = q - p;
    info = rest;
    return true;
}

bool IsQuote(const string& l) {
    size_t p = Indent(l);
    return p <= 3 && p < l.size() && l[p] == '>';
}

bool IsDelimRow(const string& l) {
    static const std::regex re(R"(^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$)");
    return l.find('-') != string::npos && std::regex_match(l, re);
}

vector<string> SplitRow(const string& line) {
    string l = Trim(line);
    if (!l.empty() && l[0] == '|') l.erase(0, 1);
    if (!l.empty() && l.back() == '|' && (l.size() < 2 || l[l.size() - 2] != '\\')) l.pop_back();
    vector<string> cells;
    string cur;
    bool code = false;
    for (size_t i = 0; i < l.size(); i++) {
        char c = l[i];
        if (c == '\\' && i + 1 < l.size() && l[i + 1] == '|') { cur += '|'; i++; continue; }
        if (c == '`') code = !code;
        if (c == '|' && !code) { cells.push_back(Trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    cells.push_back(Trim(cur));
    return cells;
}

string SafeUrl(string url, bool image, const string& imageBase) {
    url = Trim(url);
    string out;
    for (char c : url) {
        if (c == ' ') out += "%20";
        else if (c == '\\') out += '/';
        else out += c;
    }
    size_t colon = out.find(':');
    size_t slash = out.find_first_of("/?#");
    bool hasScheme = colon != string::npos && colon > 0 && (slash == string::npos || colon < slash);
    if (hasScheme) {
        string scheme = Lower(out.substr(0, colon));
        if (scheme.size() == 1) return "#";  // drive letter path, not loadable
        if (scheme == "http" || scheme == "https" || (!image && (scheme == "mailto" || scheme == "tel"))) return out;
        if (image && scheme == "data" && StartsWith(Lower(out), 0, "data:image/") && Lower(out).find("svg") == string::npos) return out;
        return "#";
    }
    if (image && !out.empty() && out[0] != '/' && out[0] != '#' && !(out.size() > 1 && out[0] == '/' && out[1] == '/')) {
        if (StartsWith(out, 0, "./")) out.erase(0, 2);
        return imageBase + out;
    }
    return out;
}


bool DecodeEntity(const string& s, size_t i, string& out, size_t& end);

string DecodeEntities(const string& v) {
    string o;
    for (size_t i = 0; i < v.size();) {
        string dec; size_t e;
        if (v[i] == '&' && DecodeEntity(v, i, dec, e)) { o += dec; i = e; continue; }
        o += v[i++];
    }
    return o;
}

struct Tag {
    bool ok = false, closing = false, selfClose = false;
    string name;
    vector<std::pair<string, string>> attrs;
    size_t end = 0;
};

Tag ParseTag(const string& s, size_t i) {
    Tag t;
    size_t n = s.size();
    if (i >= n || s[i] != '<') return t;
    size_t p = i + 1;
    if (p < n && s[p] == '/') { t.closing = true; p++; }
    size_t b = p;
    if (p >= n || !isalpha((unsigned char)s[p])) return t;
    while (p < n && (isalnum((unsigned char)s[p]) || s[p] == '-')) p++;
    t.name = Lower(s.substr(b, p - b));
    while (true) {
        size_t wsStart = p;
        while (p < n && isspace((unsigned char)s[p])) p++;
        bool ws = p > wsStart;
        if (p >= n) return t;
        if (s[p] == '>') { p++; break; }
        if (s[p] == '/' && p + 1 < n && s[p + 1] == '>') { t.selfClose = true; p += 2; break; }
        if (t.closing || !ws) return t;
        size_t a = p;
        if (!(isalpha((unsigned char)s[p]) || s[p] == '_' || s[p] == ':')) return t;
        while (p < n && (isalnum((unsigned char)s[p]) || s[p] == '_' || s[p] == '.' || s[p] == ':' || s[p] == '-')) p++;
        string an = Lower(s.substr(a, p - a)), av;
        size_t q = p;
        while (q < n && isspace((unsigned char)s[q])) q++;
        if (q < n && s[q] == '=') {
            q++;
            while (q < n && isspace((unsigned char)s[q])) q++;
            if (q < n && (s[q] == '"' || s[q] == '\'')) {
                size_t e = s.find(s[q], q + 1);
                if (e == string::npos) return t;
                av = s.substr(q + 1, e - q - 1);
                q = e + 1;
            } else {
                size_t e = q;
                while (e < n && !isspace((unsigned char)s[e]) && !strchr("\"'=<>`", s[e])) e++;
                if (e == q) return t;
                av = s.substr(q, e - q);
                q = e;
            }
            p = q;
        }
        t.attrs.push_back({an, av});
    }
    t.ok = true;
    t.end = p;
    return t;
}

bool TagAllowed(const string& name) {
    static const std::set<string> ok = {
        "a", "abbr", "b", "blockquote", "br", "caption", "center", "cite", "code", "col", "colgroup", "dd", "del",
        "details", "div", "dl", "dt", "em", "figcaption", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "i",
        "img", "ins", "kbd", "li", "mark", "ol", "p", "picture", "pre", "q", "s", "samp", "small", "source", "span",
        "strike", "strong", "sub", "summary", "sup", "table", "tbody", "td", "tfoot", "th", "thead", "tr", "tt", "u",
        "ul", "var", "section", "article", "aside", "header", "footer", "nav", "main", "address"};
    return ok.count(name) > 0;
}

string SanitizeSrcset(const string& v, const string& imageBase) {
    string out;
    size_t i = 0;
    while (i <= v.size()) {
        size_t e = v.find(',', i);
        if (e == string::npos) e = v.size();
        string cand = Trim(v.substr(i, e - i));
        if (!cand.empty()) {
            size_t sp = cand.find_first_of(" \t\r\n");
            string url = cand.substr(0, sp), desc = sp == string::npos ? "" : Trim(cand.substr(sp));
            if (!out.empty()) out += ", ";
            out += SafeUrl(url, true, imageBase) + (desc.empty() ? "" : " " + desc);
        }
        i = e + 1;
    }
    return out;
}

string SanitizeTag(const Tag& t, const string& imageBase) {
    if (t.closing) return "</" + t.name + ">";
    static const std::set<string> plain = {"align", "valign", "width", "height", "alt", "title", "id", "name", "colspan",
                                           "rowspan", "open", "start", "border", "lang", "dir"};
    bool img = t.name == "img" || t.name == "source";
    string o = "<" + t.name;
    for (auto& kv : t.attrs) {
        const string& k = kv.first;
        const string v = DecodeEntities(kv.second);
        if (k == "href" && t.name == "a") o += " href=\"" + Esc(SafeUrl(v, false, imageBase)) + "\"";
        else if (k == "src" && img) o += " src=\"" + Esc(SafeUrl(v, true, imageBase)) + "\"";
        else if (k == "srcset" && img) o += " srcset=\"" + Esc(SanitizeSrcset(v, imageBase)) + "\"";
        else if (k == "media" && t.name == "source") o += " media=\"" + Esc(v) + "\"";
        else if (plain.count(k)) o += " " + k + "=\"" + Esc(v) + "\"";
    }
    return o + ">";
}

// Sanitises a chunk of raw HTML: allowed tags keep safe attributes, everything else is escaped.
string SanitizeHtml(const string& h, const string& imageBase) {
    string out;
    size_t n = h.size();
    for (size_t i = 0; i < n;) {
        char c = h[i];
        if (c == '<') {
            if (StartsWith(h, i, "<!--")) {
                size_t e = h.find("-->", i + 4);
                i = e == string::npos ? n : e + 3;
                continue;
            }
            Tag t = ParseTag(h, i);
            if (t.ok && TagAllowed(t.name)) { out += SanitizeTag(t, imageBase); i = t.end; continue; }
            out += "&lt;"; i++;
        } else if (c == '>') { out += "&gt;"; i++; }
        else if (c == '&') {
            size_t e = i + 1;
            while (e < n && e - i < 10 && (isalnum((unsigned char)h[e]) || h[e] == '#')) e++;
            out += (e < n && h[e] == ';' && e > i + 1) ? "&" : "&amp;";
            i++;
        } else { out += c; i++; }
    }
    return out;
}

// 1 = comment, 2 = <pre>, 6 = block-level tag, 7 = a lone allowed tag on its line, 0 = not an HTML block
int HtmlBlockKind(const string& l) {
    size_t p = Indent(l);
    if (p > 3 || p >= l.size() || l[p] != '<') return 0;
    if (StartsWith(l, p, "<!--")) return 1;
    size_t q = p + 1;
    if (q < l.size() && l[q] == '/') q++;
    size_t b = q;
    while (q < l.size() && (isalnum((unsigned char)l[q]) || l[q] == '-')) q++;
    if (q == b) return 0;
    string name = Lower(l.substr(b, q - b));
    if (q < l.size() && !isspace((unsigned char)l[q]) && l[q] != '>' && l[q] != '/') return 0;
    static const std::set<string> blocks = {
        "address", "article", "aside", "blockquote", "center", "dd", "details", "div", "dl", "dt", "figcaption", "figure",
        "footer", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li", "main", "nav", "ol", "p", "section",
        "summary", "table", "tbody", "td", "tfoot", "th", "thead", "tr", "ul"};
    if (name == "pre" && l[p + 1] != '/') return 2;
    if (blocks.count(name)) return 6;
    Tag t = ParseTag(l, p);
    if (t.ok && TagAllowed(t.name) && IsBlank(l.substr(t.end))) return 7;
    return 0;
}


void AppendUtf8(string& o, unsigned long cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

// Decodes an HTML character reference at s[i] ('&'). Returns false if it is not a known reference.
bool DecodeEntity(const string& s, size_t i, string& out, size_t& end) {
    static const std::map<string, unsigned long> names = {
        {"amp", 38}, {"lt", 60}, {"gt", 62}, {"quot", 34}, {"apos", 39}, {"nbsp", 160}, {"iexcl", 161}, {"cent", 162},
        {"pound", 163}, {"curren", 164}, {"yen", 165}, {"brvbar", 166}, {"sect", 167}, {"uml", 168}, {"copy", 169},
        {"ordf", 170}, {"laquo", 171}, {"not", 172}, {"shy", 173}, {"reg", 174}, {"macr", 175}, {"deg", 176},
        {"plusmn", 177}, {"sup2", 178}, {"sup3", 179}, {"acute", 180}, {"micro", 181}, {"para", 182}, {"middot", 183},
        {"cedil", 184}, {"sup1", 185}, {"ordm", 186}, {"raquo", 187}, {"frac14", 188}, {"frac12", 189}, {"frac34", 190},
        {"iquest", 191}, {"times", 215}, {"divide", 247}, {"Auml", 196}, {"Ouml", 214}, {"Uuml", 220}, {"auml", 228},
        {"ouml", 246}, {"uuml", 252}, {"szlig", 223}, {"eacute", 233}, {"egrave", 232}, {"agrave", 224}, {"aacute", 225},
        {"ccedil", 231}, {"ntilde", 241}, {"oacute", 243}, {"iacute", 237}, {"uacute", 250}, {"Eacute", 201},
        {"ndash", 8211}, {"mdash", 8212}, {"lsquo", 8216}, {"rsquo", 8217}, {"sbquo", 8218}, {"ldquo", 8220},
        {"rdquo", 8221}, {"bdquo", 8222}, {"dagger", 8224}, {"Dagger", 8225}, {"bull", 8226}, {"hellip", 8230},
        {"permil", 8240}, {"prime", 8242}, {"Prime", 8243}, {"lsaquo", 8249}, {"rsaquo", 8250}, {"euro", 8364},
        {"trade", 8482}, {"larr", 8592}, {"uarr", 8593}, {"rarr", 8594}, {"darr", 8595}, {"harr", 8596},
        {"lArr", 8656}, {"rArr", 8658}, {"hArr", 8660}, {"forall", 8704}, {"part", 8706}, {"exist", 8707},
        {"empty", 8709}, {"nabla", 8711}, {"isin", 8712}, {"sum", 8721}, {"minus", 8722}, {"radic", 8730},
        {"infin", 8734}, {"and", 8743}, {"or", 8744}, {"cap", 8745}, {"cup", 8746}, {"int", 8747}, {"asymp", 8776},
        {"ne", 8800}, {"equiv", 8801}, {"le", 8804}, {"ge", 8805}, {"hearts", 9829}, {"spades", 9824},
        {"clubs", 9827}, {"diams", 9830}, {"alpha", 945}, {"beta", 946}, {"gamma", 947}, {"delta", 948},
        {"epsilon", 949}, {"lambda", 955}, {"mu", 956}, {"pi", 960}, {"sigma", 963}, {"omega", 969}, {"Omega", 937},
        {"Delta", 916}, {"Sigma", 931}, {"ensp", 8194}, {"emsp", 8195}, {"thinsp", 8201}, {"check", 10003},
        {"Dcaron", 270}, {"HilbertSpace", 8459}, {"DifferentialD", 8518}, {"ClockwiseContourIntegral", 8754}};
    size_t n = s.size(), p = i + 1;
    if (p < n && s[p] == '#') {
        p++;
        bool hex = p < n && (s[p] == 'x' || s[p] == 'X');
        if (hex) p++;
        size_t b = p;
        while (p < n && (hex ? isxdigit((unsigned char)s[p]) : isdigit((unsigned char)s[p])) && p - b < 9) p++;
        size_t digits = p - b;
        if (!digits || digits > (hex ? 6u : 7u) || p >= n || s[p] != ';') return false;
        unsigned long cp = strtoul(s.substr(b, digits).c_str(), nullptr, hex ? 16 : 10);
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        AppendUtf8(out, cp);
        end = p + 1;
        return true;
    }
    size_t b = p;
    while (p < n && isalnum((unsigned char)s[p]) && p - b < 32) p++;
    if (p == b || p >= n || s[p] != ';') return false;
    auto it = names.find(s.substr(b, p - b));
    if (it == names.end()) return false;
    AppendUtf8(out, it->second);
    end = p + 1;
    return true;
}

// Resolves backslash escapes and character references (used for link destinations and titles).
string Unescape(const string& s) {
    string o;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size() && ispunct((unsigned char)s[i + 1])) { o += s[i + 1]; i += 2; continue; }
        if (s[i] == '&') {
            string dec; size_t e;
            if (DecodeEntity(s, i, dec, e)) { o += dec; i = e; continue; }
        }
        o += s[i++];
    }
    return o;
}

// Parses a link reference definition at lines[i]; returns the number of lines it spans (0 if there is none).
size_t ParseRefDef(const vector<string>& lines, size_t i, string& label, string& url, string& title) {
    size_t end = i;
    string text;
    vector<size_t> starts;
    while (end < lines.size() && !IsBlank(lines[end])) {
        starts.push_back(text.size());
        text += lines[end] + "\n";
        end++;
    }
    auto lineOf = [&](size_t pos) {
        size_t k = 0;
        while (k + 1 < starts.size() && starts[k + 1] <= pos) k++;
        return k;
    };
    size_t p = 0, n = text.size(), ind = 0;
    while (p < n && text[p] == ' ' && ind < 3) { p++; ind++; }
    if (p >= n || text[p] != '[') return 0;
    p++;
    size_t lb = p;
    while (p < n && text[p] != ']') {
        if (text[p] == '\\' && p + 1 < n) p++;
        else if (text[p] == '[') return 0;
        p++;
    }
    if (p >= n) return 0;
    label = text.substr(lb, p - lb);
    if (IsBlank(label) || label.size() > 999) return 0;
    p++;
    if (p >= n || text[p] != ':') return 0;
    p++;
    while (p < n && isspace((unsigned char)text[p])) p++;
    if (p >= n) return 0;
    string dest;
    if (text[p] == '<') {
        p++;
        size_t b = p;
        while (p < n && text[p] != '>') {
            if (text[p] == '\n' || text[p] == '<') return 0;
            if (text[p] == '\\' && p + 1 < n) p++;
            p++;
        }
        if (p >= n) return 0;
        dest = text.substr(b, p - b);
        p++;
    } else {
        size_t b = p;
        int par = 0;
        while (p < n && !isspace((unsigned char)text[p]) && !iscntrl((unsigned char)text[p])) {
            if (text[p] == '\\' && p + 1 < n && ispunct((unsigned char)text[p + 1])) { p += 2; continue; }
            if (text[p] == '(') par++;
            else if (text[p] == ')') { if (par == 0) break; par--; }
            p++;
        }
        if (p == b || par != 0) return 0;
        dest = text.substr(b, p - b);
    }
    size_t eol = text.find('\n', p);
    bool restBlank = IsBlank(text.substr(p, eol - p));
    size_t defEnd = eol;
    string t;
    bool haveTitle = false;
    size_t q = p;
    bool sawSpace = false;
    while (q < n && isspace((unsigned char)text[q])) { sawSpace = true; q++; }
    if (sawSpace && q < n && (text[q] == '"' || text[q] == '\'' || text[q] == '(')) {
        char open = text[q], close = open == '(' ? ')' : open;
        size_t r = q + 1;
        bool ok = false;
        while (r < n) {
            if (text[r] == '\\' && r + 1 < n) { r += 2; continue; }
            if (text[r] == close) { ok = true; break; }
            if (open == '(' && text[r] == '(') break;
            r++;
        }
        if (ok) {
            size_t e2 = text.find('\n', r + 1);
            if (IsBlank(text.substr(r + 1, e2 - r - 1))) { t = text.substr(q + 1, r - q - 1); haveTitle = true; defEnd = e2; }
        }
    }
    if (!haveTitle && !restBlank) return 0;
    url = Unescape(dest);
    title = Unescape(t);
    return lineOf(defEnd) + 1;
}

struct Node {
    string text;                 // literal HTML for non-delimiters
    bool delim = false;
    char ch = 0;
    int count = 0, orig = 0;     // remaining / original run length
    bool open = false, close = false;
    string before, after;        // markup emitted around the remaining literal delimiter chars
};

// CommonMark "process emphasis": pairs up delimiter runs and records the tags to emit.
void ProcessEmphasis(vector<Node>& nodes) {
    for (size_t c = 0; c < nodes.size(); c++) {
        if (!nodes[c].delim || !nodes[c].close) continue;
        while (nodes[c].count > 0 && nodes[c].close) {
            size_t o = c;
            bool found = false;
            while (o-- > 0) {
                Node& op = nodes[o];
                if (!op.delim || !op.open || op.count == 0 || op.ch != nodes[c].ch) continue;
                if (op.ch == '~') { if (op.count != nodes[c].count) continue; }
                else if ((nodes[c].open || op.close) && (op.orig + nodes[c].orig) % 3 == 0 &&
                         !(op.orig % 3 == 0 && nodes[c].orig % 3 == 0)) continue;
                found = true;
                break;
            }
            if (!found) { nodes[c].close = false; break; }
            Node& op = nodes[o];
            Node& cl = nodes[c];
            int use = op.ch == '~' ? op.count : (op.count >= 2 && cl.count >= 2 ? 2 : 1);
            const char* tag = op.ch == '~' ? "del" : use == 2 ? "strong" : "em";
            op.after = string("<") + tag + ">" + op.after;
            cl.before += string("</") + tag + ">";
            op.count -= use;
            cl.count -= use;
            for (size_t k = o + 1; k < c; k++) nodes[k].open = nodes[k].close = false;  // delimiters in between become literal
            if (op.count == 0) op.open = false;
        }
    }
}

class Renderer {
public:
    explicit Renderer(string imageBase) : imageBase_(std::move(imageBase)) {}

    string Run(const string& md) {
        vector<string> lines;
        {
            string cur;
            for (size_t i = 0; i < md.size(); i++) {
                char c = md[i];
                if (c == '\r') { if (i + 1 < md.size() && md[i + 1] == '\n') i++; lines.push_back(cur); cur.clear(); }
                else if (c == '\n') { lines.push_back(cur); cur.clear(); }
                else cur += c;
            }
            lines.push_back(cur);
        }
        if (!lines.empty() && lines[0].size() >= 3 && (unsigned char)lines[0][0] == 0xEF && (unsigned char)lines[0][1] == 0xBB && (unsigned char)lines[0][2] == 0xBF)
            lines[0].erase(0, 3);
        for (auto& l : lines) {  // expand leading tabs
            size_t t = 0;
            while (t < l.size() && (l[t] == '\t' || l[t] == ' ')) t++;
            if (l.find('\t') < t) {
                string lead;
                for (size_t k = 0; k < t; k++) lead += (l[k] == '\t') ? string("    ") : string(" ");
                l = lead + l.substr(t);
            }
        }
        lines = ExtractRefs(lines);
        string body = Blocks(lines, false);
        return body + RenderFootnotes();
    }

private:
    string imageBase_;
    std::map<string, std::pair<string, string>> refs_;

    struct Foot {
        vector<string> lines;
        int num = 0, refs = 0;
    };
    std::map<string, Foot> foot_;
    vector<string> footOrder_;

    // Removes link reference definitions and footnote definitions from the line list.
    vector<string> ExtractRefs(const vector<string>& in) {
        static const std::regex re(R"re(^ {0,3}\[([^\]\^][^\]]*)\]:\s*(<[^>]*>|\S+)(?:\s+(?:"([^"]*)"|'([^']*)'|\(([^)]*)\)))?\s*$)re");
        static const std::regex fre(R"re(^ {0,3}\[\^([^\]\s]+)\]:\s?(.*)$)re");
        vector<string> out;
        char fch = 0; size_t fn = 0;
        bool lastWasDef = false;
        for (size_t i = 0; i < in.size(); i++) {
            const string& l = in[i];
            bool afterDef = lastWasDef;
            lastWasDef = false;
            char c; size_t n; string inf;
            if (fch) {
                if (IsFence(l, c, n, inf) && c == fch && n >= fn && inf.empty()) fch = 0;
                out.push_back(l);
                continue;
            }
            if (IsFence(l, c, n, inf)) { fch = c; fn = n; out.push_back(l); continue; }
            std::smatch m;
            if (std::regex_match(l, m, fre)) {
                Foot f;
                f.lines.push_back(m[2].str());
                size_t j = i + 1;
                while (j < in.size()) {
                    if (IsBlank(in[j])) {
                        size_t k = j;
                        while (k < in.size() && IsBlank(in[k])) k++;
                        if (k < in.size() && Indent(in[k]) >= 2) { for (; j < k; j++) f.lines.push_back(""); continue; }
                        break;
                    }
                    if (Indent(in[j]) >= 2) { f.lines.push_back(in[j].substr(std::min<size_t>(4, Indent(in[j])))); j++; continue; }
                    break;
                }
                foot_.emplace(NormLabel(m[1].str()), std::move(f));
                i = j - 1;
                continue;
            }
            if (afterDef || i == 0 || IsBlank(in[i - 1])) {
                string lab, u, t;
                size_t cnt = ParseRefDef(in, i, lab, u, t);
                if (cnt) {
                    refs_.emplace(NormLabel(lab), std::make_pair(u, t));
                    i += cnt - 1;
                    lastWasDef = true;
                    continue;
                }
            }
            out.push_back(l);
        }
        return out;
    }

    string RenderFootnotes() {
        vector<string> bodies;
        for (size_t k = 0; k < footOrder_.size(); k++)  // bodies may reference further footnotes, growing the list
            bodies.push_back(Blocks(foot_[footOrder_[k]].lines, false));
        if (bodies.empty()) return "";
        string out = "<section class=\"footnotes\">\n<hr>\n<ol>\n";
        for (size_t k = 0; k < bodies.size(); k++) {
            const Foot& f = foot_[footOrder_[k]];
            string num = std::to_string(f.num), back;
            for (int r = 1; r <= f.refs; r++) {
                string id = "fnref-" + num + (r > 1 ? "-" + std::to_string(r) : "");
                back += " <a class=\"fn-back\" href=\"#" + id + "\">\xE2\x86\xA9" + (r > 1 ? "<sup>" + std::to_string(r) + "</sup>" : "") + "</a>";
            }
            string& h = bodies[k];
            size_t p = h.rfind("</p>");
            if (p != string::npos) h.insert(p, back); else h += "<p>" + back + "</p>\n";
            out += "<li id=\"fn-" + num + "\">\n" + h + "</li>\n";
        }
        return out + "</ol>\n</section>\n";
    }

    static bool BlockStart(const string& l) {
        char c; size_t n; string inf; int lv; string t;
        if (IsFence(l, c, n, inf) || IsAtx(l, lv, t) || IsQuote(l) || IsHr(l)) return true;
        { int hk = HtmlBlockKind(l); if (hk && hk != 7) return true; }
        Marker m = ParseMarker(l);
        if (m.ok && m.content < l.size() && !IsBlank(l.substr(m.content)) && (!m.ordered || m.start == 1)) return true;
        return false;
    }

    string Blocks(const vector<string>& lines, bool tight) {
        string out;
        size_t i = 0, n = lines.size();
        while (i < n) {
            const string& l = lines[i];
            if (IsBlank(l)) { i++; continue; }

            char fc; size_t fl; string info;
            int level; string htext;

            if (IsFence(l, fc, fl, info)) {
                size_t ind = Indent(l);
                string code;
                i++;
                while (i < n) {
                    char c2; size_t n2; string i2;
                    if (IsFence(lines[i], c2, n2, i2) && c2 == fc && n2 >= fl && i2.empty()) { i++; break; }
                    string row = lines[i];
                    size_t strip = std::min(ind, Indent(row));
                    code += row.substr(strip) + "\n";
                    i++;
                }
                string lang = info.substr(0, info.find_first_of(" \t{"));
                out += "<pre><code";
                if (!lang.empty()) out += " class=\"language-" + Esc(lang) + "\"";
                out += ">" + Esc(code) + "</code></pre>\n";
                continue;
            }
            if (IsAtx(l, level, htext)) {
                out += "<h" + std::to_string(level) + ">" + Inline(htext, false) + "</h" + std::to_string(level) + ">\n";
                i++;
                continue;
            }
            if (IsHr(l)) { out += "<hr>\n"; i++; continue; }

            if (int hk = HtmlBlockKind(l)) {
                string raw;
                while (i < n) {
                    raw += lines[i] + "\n";
                    bool endNow = (hk == 1 && lines[i].find("-->") != string::npos) ||
                                  (hk == 2 && Lower(lines[i]).find("</pre>") != string::npos);
                    i++;
                    if (endNow) break;
                    if (hk >= 6 && (i >= n || IsBlank(lines[i]))) break;
                }
                out += SanitizeHtml(raw, imageBase_) + "\n";
                continue;
            }

            if (IsQuote(l)) {
                vector<string> inner;
                while (i < n) {
                    const string& q = lines[i];
                    if (IsQuote(q)) {
                        size_t p = Indent(q) + 1;
                        if (p < q.size() && q[p] == ' ') p++;
                        inner.push_back(q.substr(p));
                    } else if (!IsBlank(q) && !inner.empty() && !IsBlank(inner.back()) && !BlockStart(q)) {
                        inner.push_back(q);  // lazy continuation
                    } else break;
                    i++;
                }
                string cls, title;
                if (!inner.empty()) {
                    static const std::regex alert(R"(^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$)", std::regex::icase);
                    std::smatch m;
                    if (std::regex_match(inner[0], m, alert)) {
                        title = m[1].str();
                        string low = Lower(title);
                        cls = " class=\"alert alert-" + low + "\"";
                        title[0] = (char)toupper((unsigned char)title[0]);
                        for (size_t k = 1; k < title.size(); k++) title[k] = (char)tolower((unsigned char)title[k]);
                        inner.erase(inner.begin());
                    }
                }
                out += "<blockquote" + cls + ">\n";
                if (!title.empty()) out += "<p class=\"alert-title\">" + title + "</p>\n";
                out += Blocks(inner, false) + "</blockquote>\n";
                continue;
            }

            // table
            if (l.find('|') != string::npos && i + 1 < n && IsDelimRow(lines[i + 1]) && lines[i + 1].find('|') != string::npos) {
                vector<string> head = SplitRow(l), delim = SplitRow(lines[i + 1]);
                if (head.size() == delim.size()) {
                    vector<string> align;
                    for (auto& d : delim) {
                        bool L = !d.empty() && d.front() == ':', R = !d.empty() && d.back() == ':';
                        align.push_back(L && R ? "center" : R ? "right" : L ? "left" : "");
                    }
                    auto cell = [&](const char* tag, const string& txt, size_t col) {
                        string a = col < align.size() && !align[col].empty() ? string(" style=\"text-align:") + align[col] + "\"" : "";
                        return string("<") + tag + a + ">" + Inline(txt, false) + "</" + tag + ">";
                    };
                    out += "<table>\n<thead><tr>";
                    for (size_t c = 0; c < head.size(); c++) out += cell("th", head[c], c);
                    out += "</tr></thead>\n<tbody>\n";
                    i += 2;
                    while (i < n && !IsBlank(lines[i]) && lines[i].find('|') != string::npos) {
                        vector<string> row = SplitRow(lines[i]);
                        out += "<tr>";
                        for (size_t c = 0; c < head.size(); c++) out += cell("td", c < row.size() ? row[c] : "", c);
                        out += "</tr>\n";
                        i++;
                    }
                    out += "</tbody></table>\n";
                    continue;
                }
            }

            Marker m0 = ParseMarker(l);
            if (m0.ok) {
                i = List(lines, i, m0, out);
                continue;
            }

            if (Indent(l) >= 4) {
                string code;
                size_t blanks = 0;
                while (i < n) {
                    if (IsBlank(lines[i])) { blanks++; i++; continue; }
                    if (Indent(lines[i]) < 4) break;
                    code += string(blanks, '\n') + lines[i].substr(4) + "\n";
                    blanks = 0;
                    i++;
                }
                out += "<pre><code>" + Esc(code) + "</code></pre>\n";
                continue;
            }

            // paragraph / setext heading
            string text;
            size_t start = i;
            int setext = 0;
            while (i < n) {
                const string& p = lines[i];
                if (IsBlank(p)) break;
                if (i > start) {
                    static const std::regex ul(R"(^ {0,3}(=+|-+)[ \t]*$)");
                    std::smatch m;
                    if (std::regex_match(p, m, ul)) { setext = m[1].str()[0] == '=' ? 1 : 2; i++; break; }
                    if (BlockStart(p)) break;
                    if (p.find('|') != string::npos && i + 1 < n && IsDelimRow(lines[i + 1])) break;
                }
                if (i > start) text += "\n";
                text += (i == start) ? LTrim(p) : LTrim(p);
                i++;
            }
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (setext) {
                out += "<h" + std::to_string(setext) + ">" + Inline(text, false) + "</h" + std::to_string(setext) + ">\n";
            } else if (tight) {
                out += Inline(text, false) + "\n";
            } else {
                out += "<p>" + Inline(text, false) + "</p>\n";
            }
        }
        return out;
    }

    size_t List(const vector<string>& lines, size_t i, const Marker& first, string& out) {
        size_t n = lines.size();
        vector<vector<string>> items;
        bool loose = false;
        bool endAll = false;

        while (i < n && !endAll) {
            Marker m = ParseMarker(lines[i]);
            if (!m.ok || m.ordered != first.ordered || m.type != first.type) break;
            if (!items.empty() && IsHr(lines[i])) break;
            vector<string> item;
            size_t cont = m.content;
            item.push_back(m.content <= lines[i].size() ? lines[i].substr(m.content) : "");
            i++;
            while (i < n) {
                const string& l = lines[i];
                if (IsBlank(l)) {
                    size_t j = i;
                    while (j < n && IsBlank(lines[j])) j++;
                    if (j >= n) { i = j; endAll = true; break; }
                    if (Indent(lines[j]) >= cont) {
                        for (size_t k = i; k < j; k++) item.push_back("");
                        // blank line followed by a non-list paragraph line makes the list loose
                        if (Indent(lines[j]) < cont + 2) loose = true;  // a direct child block after a blank line
                        i = j;
                        continue;
                    }
                    Marker nm = ParseMarker(lines[j]);
                    if (nm.ok && nm.ordered == first.ordered && nm.type == first.type && !IsHr(lines[j])) {
                        loose = true;
                        i = j;
                    } else {
                        i = j;
                        endAll = true;
                    }
                    break;
                }
                if (Indent(l) >= cont) {
                    item.push_back(l.substr(cont));
                    i++;
                    continue;
                }
                Marker nm = ParseMarker(l);
                if (nm.ok || BlockStart(l)) break;
                if (!item.empty() && !IsBlank(item.back())) { item.push_back(LTrim(l)); i++; continue; }  // lazy continuation
                break;
            }
            while (!item.empty() && IsBlank(item.back())) item.pop_back();
            items.push_back(std::move(item));
        }

        out += first.ordered ? (first.start != 1 ? "<ol start=\"" + std::to_string(first.start) + "\">\n" : string("<ol>\n")) : "<ul>\n";
        for (auto& item : items) {
            string cls, box;
            if (!item.empty()) {
                const string& f = item[0];
                if (f.size() >= 3 && f[0] == '[' && (f[1] == ' ' || f[1] == 'x' || f[1] == 'X') && f[2] == ']' && (f.size() == 3 || f[3] == ' ')) {
                    bool done = f[1] != ' ';
                    box = string("<input type=\"checkbox\" disabled") + (done ? " checked" : "") + "> ";
                    cls = " class=\"task\"";
                    item[0] = f.size() > 4 ? f.substr(4) : "";
                }
            }
            out += "<li" + cls + ">" + box + Blocks(item, !loose) + "</li>\n";
        }
        out += first.ordered ? "</ol>\n" : "</ul>\n";
        return i;
    }

    // ---- inline ----

    bool ParseLink(const string& s, size_t open, bool noAuto, string& text, string& url, string& title, size_t& end) {
        size_t n = s.size();
        int depth = 0;
        size_t j = open;
        for (; j < n; j++) {
            char c = s[j];
            if (c == '\\') { j++; continue; }
            if (c == '`') {
                size_t k = j;
                while (k < n && s[k] == '`') k++;
                string run = s.substr(j, k - j);
                size_t f = s.find(run, k);
                if (f != string::npos) { j = f + run.size() - 1; continue; }
                j = k - 1;
                continue;
            }
            if (c == '[') depth++;
            else if (c == ']') { if (--depth == 0) break; }
        }
        if (j >= n) return false;
        text = s.substr(open + 1, j - open - 1);
        size_t p = j + 1;
        if (p < n && s[p] == '(') {
            size_t q = p + 1;
            while (q < n && (s[q] == ' ' || s[q] == '\n')) q++;
            string dest;
            if (q < n && s[q] == '<') {
                size_t e = s.find('>', q);
                if (e == string::npos) return false;
                dest = s.substr(q + 1, e - q - 1);
                q = e + 1;
            } else {
                int par = 0;
                while (q < n) {
                    char c = s[q];
                    if (c == '\\' && q + 1 < n) { dest += s[q + 1]; q += 2; continue; }
                    if (c == ' ' || c == '\n') break;
                    if (c == '(') par++;
                    if (c == ')') { if (par == 0) break; par--; }
                    dest += c;
                    q++;
                }
            }
            while (q < n && (s[q] == ' ' || s[q] == '\n')) q++;
            string t;
            if (q < n && (s[q] == '"' || s[q] == '\'' || s[q] == '(')) {
                char close = s[q] == '(' ? ')' : s[q];
                size_t e = q + 1;
                while (e < n && s[e] != close) { if (s[e] == '\\') e++; e++; }
                if (e >= n) return false;
                t = s.substr(q + 1, e - q - 1);
                q = e + 1;
                while (q < n && (s[q] == ' ' || s[q] == '\n')) q++;
            }
            if (q >= n || s[q] != ')') return false;
            url = Unescape(dest); title = Unescape(t); end = q + 1;
            return true;
        }
        string label;
        size_t endPos = j + 1;
        if (p < n && s[p] == '[') {
            size_t e = s.find(']', p);
            if (e == string::npos) return false;
            label = s.substr(p + 1, e - p - 1);
            if (label.empty()) label = text;
            endPos = e + 1;
        } else {
            label = text;
        }
        auto it = refs_.find(NormLabel(label));
        if (it == refs_.end()) return false;
        url = it->second.first; title = it->second.second; end = endPos;
        (void)noAuto;
        return true;
    }

    string Inline(const string& s, bool noAuto) {
        string out;
        size_t n = s.size(), i = 0;
        vector<Node> nodes;
        auto flush = [&]() {
            if (out.empty()) return;
            Node t;
            t.text = std::move(out);
            out.clear();
            nodes.push_back(std::move(t));
        };
        while (i < n) {
            char c = s[i];

            if (c == '`') {
                size_t k = i;
                while (k < n && s[k] == '`') k++;
                string run = s.substr(i, k - i);
                size_t f = s.find(run, k);
                while (f != string::npos && f + run.size() < n && s[f + run.size()] == '`') {
                    size_t g = f;
                    while (g < n && s[g] == '`') g++;
                    f = s.find(run, g);
                }
                if (f != string::npos) {
                    string code = s.substr(k, f - k);
                    for (auto& ch : code) if (ch == '\n') ch = ' ';
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' && !IsBlank(code)) code = code.substr(1, code.size() - 2);
                    out += "<code>" + Esc(code) + "</code>";
                    i = f + run.size();
                } else {
                    out += run;
                    i = k;
                }
                continue;
            }
            if (c == '\\' && i + 1 < n) {
                if (ispunct((unsigned char)s[i + 1])) { out += Esc(string(1, s[i + 1])); i += 2; continue; }
                if (s[i + 1] == '\n') { out += "<br>\n"; i += 2; continue; }
            }
            if (c == '\n') {
                size_t sp = 0;
                while (sp < out.size() && out[out.size() - 1 - sp] == ' ') sp++;
                out.resize(out.size() - sp);
                out += sp >= 2 ? "<br>\n" : "\n";
                i++;
                continue;
            }
            if (c == '!' && i + 1 < n && s[i + 1] == '[') {
                string text, url, title; size_t end;
                if (ParseLink(s, i + 1, noAuto, text, url, title, end)) {
                    out += "<img src=\"" + Esc(SafeUrl(url, true, imageBase_)) + "\" alt=\"" + Esc(text) + "\"" +
                           (title.empty() ? "" : " title=\"" + Esc(title) + "\"") + ">";
                    i = end;
                    continue;
                }
            }
            if (c == '[' && i + 1 < n && s[i + 1] == '^') {
                size_t e = s.find(']', i + 2);
                if (e != string::npos) {
                    auto it = foot_.find(NormLabel(s.substr(i + 2, e - i - 2)));
                    if (it != foot_.end()) {
                        Foot& f = it->second;
                        if (!f.num) { footOrder_.push_back(it->first); f.num = (int)footOrder_.size(); }
                        f.refs++;
                        string num = std::to_string(f.num);
                        string id = "fnref-" + num + (f.refs > 1 ? "-" + std::to_string(f.refs) : "");
                        out += "<sup class=\"fn-ref\"><a id=\"" + id + "\" href=\"#fn-" + num + "\">" + num + "</a></sup>";
                        i = e + 1;
                        continue;
                    }
                }
            }
            if (c == '[') {
                string text, url, title; size_t end;
                if (ParseLink(s, i, noAuto, text, url, title, end)) {
                    out += "<a href=\"" + Esc(SafeUrl(url, false, imageBase_)) + "\"" +
                           (title.empty() ? "" : " title=\"" + Esc(title) + "\"") + ">" + Inline(text, true) + "</a>";
                    i = end;
                    continue;
                }
            }
            if (c == '<') {
                if (StartsWith(s, i, "<!--")) {
                    size_t ce = s.find("-->", i + 4);
                    if (ce != string::npos) { i = ce + 3; continue; }
                }
                {
                    Tag t = ParseTag(s, i);
                    if (t.ok && TagAllowed(t.name)) { out += SanitizeTag(t, imageBase_); i = t.end; continue; }
                }
                size_t e = s.find('>', i);
                if (e != string::npos) {
                    string inner = s.substr(i + 1, e - i - 1);
                    static const std::regex url(R"(^[A-Za-z][A-Za-z0-9+.-]{1,31}:[^\s<>]*$)");
                    static const std::regex mail(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
                    string scheme = Lower(inner.substr(0, inner.find(':')));
                    if (std::regex_match(inner, url) && scheme != "javascript" && scheme != "data" && scheme != "vbscript" && scheme != "file") {
                        string href;
                        for (char ch : inner) {
                            if (strchr("\\[]`^{|}", ch)) { char buf[4]; snprintf(buf, sizeof buf, "%%%02X", (unsigned char)ch); href += buf; }
                            else href += ch;
                        }
                        out += "<a href=\"" + Esc(href) + "\">" + Esc(inner) + "</a>";
                        i = e + 1; continue;
                    }
                    if (std::regex_match(inner, mail)) {
                        out += "<a href=\"mailto:" + Esc(inner) + "\">" + Esc(inner) + "</a>";
                        i = e + 1; continue;
                    }
                }
                out += "&lt;"; i++; continue;
            }
            if (!noAuto && c == 'h' && (StartsWith(s, i, "http://") || StartsWith(s, i, "https://")) && (i == 0 || !IsAlnum(s[i - 1]))) {
                size_t e = i;
                while (e < n && !isspace((unsigned char)s[e]) && s[e] != '<') e++;
                while (e > i && strchr(".,;:!?'\"*_~", s[e - 1])) e--;
                if (e > i && s[e - 1] == ')' && s.substr(i, e - i).find('(') == string::npos) e--;
                string u = s.substr(i, e - i);
                out += "<a href=\"" + Esc(u) + "\">" + Esc(u) + "</a>";
                i = e;
                continue;
            }
            if (c == '*' || c == '_' || c == '~') {
                size_t r = 0;
                while (i + r < n && s[i + r] == c) r++;
                unsigned char prev = i > 0 ? (unsigned char)s[i - 1] : ' ';
                unsigned char next = i + r < n ? (unsigned char)s[i + r] : ' ';
                auto isSp = [](unsigned char x) { return x == ' ' || x == '\n' || x == '\t' || x == '\r' || x == '\f' || x == '\v'; };
                auto isPu = [](unsigned char x) { return x < 128 && ispunct(x); };
                bool leftFlank = !isSp(next) && (!isPu(next) || isSp(prev) || isPu(prev));
                bool rightFlank = !isSp(prev) && (!isPu(prev) || isSp(next) || isPu(next));
                Node d;
                d.delim = true; d.ch = c; d.count = d.orig = (int)r;
                if (c == '_') {
                    d.open = leftFlank && (!rightFlank || isPu(prev));
                    d.close = rightFlank && (!leftFlank || isPu(next));
                } else {
                    d.open = leftFlank;
                    d.close = rightFlank;
                    if (c == '~' && r > 2) d.open = d.close = false;
                }
                flush();
                nodes.push_back(d);
                i += r;
                continue;
            }
            if (c == '&') {
                string dec; size_t e;
                if (DecodeEntity(s, i, dec, e)) { out += Esc(dec); i = e; continue; }
                out += "&amp;"; i++; continue;
            }
            if (c == '>') { out += "&gt;"; i++; continue; }
            if (c == '"') { out += "&quot;"; i++; continue; }
            out += c;
            i++;
        }
        flush();
        ProcessEmphasis(nodes);
        string result;
        for (auto& nd : nodes) {
            if (!nd.delim) { result += nd.text; continue; }
            result += nd.before + string(nd.count, nd.ch) + nd.after;
        }
        return result;
    }
};

}  // namespace

std::string RenderMarkdown(const std::string& markdown, const std::string& imageBase) {
    Renderer r(imageBase);
    return r.Run(markdown);
}
