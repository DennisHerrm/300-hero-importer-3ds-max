#include "eg3d_json.h"

// locale.h traegt auf beiden Seiten die Locale-Funktionen:
// _create_locale/_strtod_l unter Windows, newlocale/strtod_l
// unter glibc. (xlocale.h gab es nur bei alten glibc-Staenden und
// unter macOS; einbinden wuerde hier fehlschlagen.)
#include <clocale>
#include <locale.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace eg3d {

namespace {

// ============================================================
//  StrtodC - strtod, das immer im C-Locale liest
//
//  Es waere verlockend, hier einen eigenen Zahlenparser zu
//  schreiben (Ziffern sammeln, mal Zehnerpotenz). Das ist aber
//  NICHT gleichwertig: die Bibliotheksfassung rundet korrekt zum
//  naechsten double, eine Mantisse-mal-Potenz-Rechnung nicht. Bei
//  einem Projekt, dessen Abnahme auf byteweiser Uebereinstimmung
//  mit Python beruht, waere das ein Eigentor.
//
//  Deshalb dieselbe Bibliotheksfunktion, nur mit ausdruecklichem
//  C-Locale. Beide Betriebssysteme koennen das, nur unter
//  verschiedenen Namen.
// ============================================================
#ifdef _WIN32

double StrtodC(const char* text, char** end) {
    // _create_locale bleibt fuer die Prozesslaufzeit stehen. Das
    // ist Absicht: der Aufruf kostet, und ein Parser liest
    // Zehntausende Zahlen. Statisch heisst hier auch
    // threadsicher - C++11 garantiert, dass die Initialisierung
    // genau einmal laeuft, auch wenn ParallelFor mehrere Threads
    // gleichzeitig hier hereinlaesst.
    static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
    if (cLocale == nullptr) return std::strtod(text, end);   // Notnagel
    return _strtod_l(text, end, cLocale);
}

#else

double StrtodC(const char* text, char** end) {
    static const locale_t cLocale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    if (cLocale == static_cast<locale_t>(0)) return std::strtod(text, end);
    return strtod_l(text, end, cLocale);
}

#endif

}  // namespace

namespace {
const int kMaxDepth = 64;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

// ============================================================
//  JsonRef
// ============================================================

JsonType JsonRef::Type() const {
    if (!doc_ || index_ < 0 || static_cast<size_t>(index_) >= doc_->nodes_.size())
        return JsonType::Invalid;
    return doc_->nodes_[static_cast<size_t>(index_)].type;
}

size_t JsonRef::Size() const {
    if (!Valid()) return 0;
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::Array && n.type != JsonType::Object) return 0;
    return n.childCount;
}

JsonRef JsonRef::At(size_t index) const {
    if (!Valid()) return JsonRef();
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (index >= n.childCount) return JsonRef();
    return JsonRef(doc_, doc_->children_[n.firstChild + index]);
}

JsonRef JsonRef::Get(const char* key) const {
    if (!Valid() || !key) return JsonRef();
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::Object) return JsonRef();
    const size_t keyLen = std::strlen(key);
    for (uint32_t i = 0; i < n.childCount; ++i) {
        const uint32_t slot = n.firstChild + i;
        if (doc_->keyLength_[slot] != keyLen) continue;
        if (std::memcmp(doc_->strings_.data() + doc_->keyStart_[slot], key, keyLen) == 0)
            return JsonRef(doc_, doc_->children_[slot]);
    }
    return JsonRef();
}

std::string JsonRef::KeyAt(size_t index) const {
    if (!Valid()) return std::string();
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::Object || index >= n.childCount) return std::string();
    const uint32_t slot = n.firstChild + static_cast<uint32_t>(index);
    return doc_->strings_.substr(doc_->keyStart_[slot], doc_->keyLength_[slot]);
}

double JsonRef::Num(double fallback) const {
    if (!Valid()) return fallback;
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    return (n.type == JsonType::Number) ? n.number : fallback;
}

int64_t JsonRef::Int(int64_t fallback) const {
    if (!Valid()) return fallback;
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::Number) return fallback;
    return static_cast<int64_t>(n.number);
}

bool JsonRef::Bool(bool fallback) const {
    if (!Valid()) return fallback;
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type == JsonType::Bool) return n.boolean;
    if (n.type == JsonType::Number) return n.number != 0.0;
    return fallback;
}

std::string JsonRef::Str(const char* fallback) const {
    if (!Valid()) return std::string(fallback ? fallback : "");
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::String) return std::string(fallback ? fallback : "");
    return doc_->strings_.substr(n.stringStart, n.stringLength);
}

bool JsonRef::TryInt(int64_t& out) const {
    if (!Valid()) return false;
    const JsonDoc::Node& n = doc_->nodes_[static_cast<size_t>(index_)];
    if (n.type != JsonType::Number) return false;   // deckt auch null ab
    out = static_cast<int64_t>(n.number);
    return true;
}

// ============================================================
//  JsonDoc
// ============================================================

void JsonDoc::SkipWhitespace(const char* text, size_t length, size_t& pos) {
    while (pos < length) {
        const char c = text[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
        else break;
    }
}

bool JsonDoc::ParseString(const char* text, size_t length, size_t& pos,
                          uint32_t& start, uint32_t& len, std::string& error) {
    if (pos >= length || text[pos] != '"') { error = "Zeichenkette erwartet"; return false; }
    ++pos;

    const size_t begin = strings_.size();
    while (pos < length) {
        const unsigned char c = static_cast<unsigned char>(text[pos]);
        if (c == '"') {
            ++pos;
            start = static_cast<uint32_t>(begin);
            len = static_cast<uint32_t>(strings_.size() - begin);
            return true;
        }
        if (c != '\\') { strings_ += static_cast<char>(c); ++pos; continue; }

        ++pos;
        if (pos >= length) { error = "Zeichenkette endet nach einem Backslash"; return false; }
        const char esc = text[pos++];
        switch (esc) {
        case '"':  strings_ += '"';  break;
        case '\\': strings_ += '\\'; break;
        case '/':  strings_ += '/';  break;
        case 'b':  strings_ += '\b'; break;
        case 'f':  strings_ += '\f'; break;
        case 'n':  strings_ += '\n'; break;
        case 'r':  strings_ += '\r'; break;
        case 't':  strings_ += '\t'; break;
        case 'u': {
            if (pos + 4 > length) { error = "\\u ohne vier Hexstellen"; return false; }
            uint32_t cp = 0;
            for (int i = 0; i < 4; ++i) {
                const int d = HexDigit(text[pos + i]);
                if (d < 0) { error = "\\u mit ungueltiger Hexstelle"; return false; }
                cp = (cp << 4) | static_cast<uint32_t>(d);
            }
            pos += 4;
            // Ersatzpaar zusammensetzen. Die Materialnamen des
            // Spiels sind chinesisch; ausserhalb der BMP liegen
            // sie zwar nicht, aber ein halbes Paar auszugeben
            // ergaebe ungueltiges UTF-8.
            if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= length &&
                text[pos] == '\\' && text[pos + 1] == 'u') {
                uint32_t low = 0;
                bool okLow = true;
                for (int i = 0; i < 4; ++i) {
                    const int d = HexDigit(text[pos + 2 + i]);
                    if (d < 0) { okLow = false; break; }
                    low = (low << 4) | static_cast<uint32_t>(d);
                }
                if (okLow && low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    pos += 6;
                }
            }
            AppendUtf8(strings_, cp);
            break;
        }
        default:
            error = "unbekannte Escape-Folge";
            return false;
        }
    }
    error = "Zeichenkette wird nicht geschlossen";
    return false;
}

int JsonDoc::ParseValue(const char* text, size_t length, size_t& pos, int depth,
                        std::string& error) {
    if (depth > kMaxDepth) { error = "JSON ist zu tief geschachtelt"; return -1; }
    SkipWhitespace(text, length, pos);
    if (pos >= length) { error = "Wert erwartet, Text ist zu Ende"; return -1; }

    const char c = text[pos];

    if (c == '{' || c == '[') {
        const bool isObject = (c == '{');
        ++pos;

        // Der eigene Knoten wird JETZT angelegt, die Kinder erst
        // danach. nodes_ kann durch die Rekursion wachsen und
        // umziehen - deshalb wird ueberall mit dem INDEX
        // gearbeitet und nie mit einer Referenz ueber einen
        // rekursiven Aufruf hinweg.
        const int self = static_cast<int>(nodes_.size());
        nodes_.push_back(Node());
        nodes_[static_cast<size_t>(self)].type =
            isObject ? JsonType::Object : JsonType::Array;

        std::vector<int>      childIndices;
        std::vector<uint32_t> childKeyStart, childKeyLength;

        SkipWhitespace(text, length, pos);
        if (pos < length && text[pos] == (isObject ? '}' : ']')) {
            ++pos;
        } else {
            for (;;) {
                SkipWhitespace(text, length, pos);
                uint32_t keyStart = 0, keyLen = 0;
                if (isObject) {
                    if (!ParseString(text, length, pos, keyStart, keyLen, error)) return -1;
                    SkipWhitespace(text, length, pos);
                    if (pos >= length || text[pos] != ':') { error = "Doppelpunkt erwartet"; return -1; }
                    ++pos;
                }
                const int child = ParseValue(text, length, pos, depth + 1, error);
                if (child < 0) return -1;
                childIndices.push_back(child);
                childKeyStart.push_back(keyStart);
                childKeyLength.push_back(keyLen);

                SkipWhitespace(text, length, pos);
                if (pos >= length) { error = "Klammer wird nicht geschlossen"; return -1; }
                if (text[pos] == ',') { ++pos; continue; }
                if (text[pos] == (isObject ? '}' : ']')) { ++pos; break; }
                error = "Komma oder schliessende Klammer erwartet";
                return -1;
            }
        }

        const uint32_t first = static_cast<uint32_t>(children_.size());
        for (size_t i = 0; i < childIndices.size(); ++i) {
            children_.push_back(childIndices[i]);
            keyStart_.push_back(childKeyStart[i]);
            keyLength_.push_back(childKeyLength[i]);
        }
        nodes_[static_cast<size_t>(self)].firstChild = first;
        nodes_[static_cast<size_t>(self)].childCount =
            static_cast<uint32_t>(childIndices.size());
        return self;
    }

    if (c == '"') {
        const int self = static_cast<int>(nodes_.size());
        nodes_.push_back(Node());
        uint32_t start = 0, len = 0;
        if (!ParseString(text, length, pos, start, len, error)) return -1;
        nodes_[static_cast<size_t>(self)].type = JsonType::String;
        nodes_[static_cast<size_t>(self)].stringStart = start;
        nodes_[static_cast<size_t>(self)].stringLength = len;
        return self;
    }

    if (c == 't' || c == 'f' || c == 'n') {
        const char* word = (c == 't') ? "true" : (c == 'f') ? "false" : "null";
        const size_t wordLen = std::strlen(word);
        if (pos + wordLen > length || std::memcmp(text + pos, word, wordLen) != 0) {
            error = "unbekanntes Schluesselwort";
            return -1;
        }
        pos += wordLen;
        const int self = static_cast<int>(nodes_.size());
        nodes_.push_back(Node());
        if (c == 'n') {
            nodes_[static_cast<size_t>(self)].type = JsonType::Null;
        } else {
            nodes_[static_cast<size_t>(self)].type = JsonType::Bool;
            nodes_[static_cast<size_t>(self)].boolean = (c == 't');
        }
        return self;
    }

    if (c == '-' || IsDigit(c)) {
        const size_t begin = pos;
        if (text[pos] == '-') ++pos;
        while (pos < length && IsDigit(text[pos])) ++pos;
        if (pos < length && text[pos] == '.') {
            ++pos;
            while (pos < length && IsDigit(text[pos])) ++pos;
        }
        if (pos < length && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < length && (text[pos] == '+' || text[pos] == '-')) ++pos;
            while (pos < length && IsDigit(text[pos])) ++pos;
        }
        // ------------------------------------------------------
        //  StrtodC, NICHT strtod.
        //
        //  strtod liest im Locale des PROZESSES. 3ds Max setzt
        //  das Locale auf die Systemsprache - auf einem deutschen
        //  Windows ist der Dezimaltrenner dann ein Komma, und
        //  strtod("0.5") liest die 0, bleibt am Punkt stehen und
        //  meldet ERFOLG. Der Rueckgabewert ist 0.0, end zeigt
        //  hinter die Ziffer, also schlaegt auch die Pruefung
        //  unten nicht an.
        //
        //  Das ist genau so passiert: der Dump aus dem Plugin
        //  zeigte in 58 von 192 matvec-Zeilen Nullen, wo das
        //  Linux-Werkzeug echte Farben las. Betroffen war
        //  ausschliesslich, was einen Dezimalpunkt hat -
        //  Ganzzahlen kamen richtig durch, Vertexdaten sowieso,
        //  weil die binaer gelesen werden. Ein Fehler, der sich
        //  in genau den Zeilen versteckt, die niemand nachrechnet.
        //
        //  Die Zahl von Hand zu begrenzen (steht oben) reicht
        //  NICHT: das schuetzt davor, dass strtod ueber das Ende
        //  hinauslaeuft, nicht davor, dass es den Punkt nicht
        //  kennt.
        // ------------------------------------------------------
        std::string numberText(text + begin, pos - begin);
        char* end = nullptr;
        const double value = StrtodC(numberText.c_str(), &end);
        if (end == numberText.c_str()) { error = "Zahl laesst sich nicht lesen"; return -1; }

        const int self = static_cast<int>(nodes_.size());
        nodes_.push_back(Node());
        nodes_[static_cast<size_t>(self)].type = JsonType::Number;
        nodes_[static_cast<size_t>(self)].number = value;
        return self;
    }

    error = std::string("unerwartetes Zeichen '") + c + "'";
    return -1;
}

bool JsonDoc::Parse(const char* text, size_t length, size_t& consumed,
                    std::string& error) {
    nodes_.clear();
    children_.clear();
    keyStart_.clear();
    keyLength_.clear();
    strings_.clear();
    consumed = 0;
    error.clear();
    if (!text) { error = "kein Text"; return false; }

    // Die Zeichenketten wachsen waehrend des Lesens; ohne
    // Vorbelegung zieht der Puffer bei 500 KB Metadaten mehrfach
    // um. nodes_ genauso.
    strings_.reserve(length / 4 + 64);
    nodes_.reserve(length / 16 + 16);

    size_t pos = 0;
    // Ein UTF-8-BOM vor dem JSON ist kein Fehler, aber auch kein
    // gueltiger Wertanfang.
    if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        pos = 3;
    }

    const int root = ParseValue(text, length, pos, 0, error);
    if (root < 0) { nodes_.clear(); return false; }
    if (root != 0) {
        // Kann nicht passieren, weil der erste angelegte Knoten
        // immer Index 0 hat. Steht hier, damit Root() sich darauf
        // verlassen darf.
        error = "innerer Fehler: Wurzelknoten ist nicht Index 0";
        nodes_.clear();
        return false;
    }
    consumed = pos;
    return true;
}

} // namespace eg3d
