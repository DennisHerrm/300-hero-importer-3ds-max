#include "eg3d_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eg3d {

// ============================================================
//  Matrix
// ============================================================

Matrix44 MatMul(const Matrix44& a, const Matrix44& b) {
    Matrix44 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += static_cast<double>(a.m[i][k]) * static_cast<double>(b.m[k][j]);
            r.m[i][j] = static_cast<float>(sum);
        }
    }
    return r;
}

// Gauss-Jordan mit Spaltenpivot. Bewusst nicht die
// Adjunkten-Formel: 263_skin5.model enthaelt eine Bind-Matrix,
// die sich nicht invertieren laesst (Fund aus dem Stapeltest des
// Addons ueber 49 Dateien), und die Determinante allein sagt
// nicht zuverlaessig, ab wann das Ergebnis Muell ist.
bool MatInvert(const Matrix44& in, Matrix44& out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) a[r][c] = in.m[r][c];
        for (int c = 0; c < 4; ++c) a[r][4 + c] = (r == c) ? 1.0 : 0.0;
    }

    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;

        if (std::fabs(a[pivot][col]) < 1e-12) return false;   // singulaer
        if (pivot != col)
            for (int c = 0; c < 8; ++c) { double t = a[col][c]; a[col][c] = a[pivot][c]; a[pivot][c] = t; }

        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;

        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double factor = a[r][col];
            if (factor == 0.0) continue;
            for (int c = 0; c < 8; ++c) a[r][c] -= factor * a[col][c];
        }
    }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out.m[r][c] = static_cast<float>(a[r][4 + c]);
    return true;
}

// Zeilenvektor-Konvention: die Rotationsbasis steht in den ersten
// drei ZEILEN, die Translation in der vierten. So liegt das
// Ergebnis direkt richtig fuer Matrix3::SetRow.
Matrix44 MatFromQuatPosScale(const Quat& q, const Vec3& t, const Vec3& s) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    const double len2 = x * x + y * y + z * z + w * w;
    const double k = (len2 > 1e-12) ? (2.0 / len2) : 0.0;

    const double xx = k * x * x, yy = k * y * y, zz = k * z * z;
    const double xy = k * x * y, xz = k * x * z, yz = k * y * z;
    const double wx = k * w * x, wy = k * w * y, wz = k * w * z;

    Matrix44 m;
    m.m[0][0] = static_cast<float>((1.0 - (yy + zz)) * s.x);
    m.m[0][1] = static_cast<float>((xy + wz) * s.x);
    m.m[0][2] = static_cast<float>((xz - wy) * s.x);
    m.m[0][3] = 0.0f;

    m.m[1][0] = static_cast<float>((xy - wz) * s.y);
    m.m[1][1] = static_cast<float>((1.0 - (xx + zz)) * s.y);
    m.m[1][2] = static_cast<float>((yz + wx) * s.y);
    m.m[1][3] = 0.0f;

    m.m[2][0] = static_cast<float>((xz + wy) * s.z);
    m.m[2][1] = static_cast<float>((yz - wx) * s.z);
    m.m[2][2] = static_cast<float>((1.0 - (xx + yy)) * s.z);
    m.m[2][3] = 0.0f;

    m.m[3][0] = t.x;
    m.m[3][1] = t.y;
    m.m[3][2] = t.z;
    m.m[3][3] = 1.0f;
    return m;
}

Vec3 MatTransformPoint(const Matrix44& m, const Vec3& p) {
    Vec3 r;
    r.x = static_cast<float>(static_cast<double>(p.x) * m.m[0][0] +
                             static_cast<double>(p.y) * m.m[1][0] +
                             static_cast<double>(p.z) * m.m[2][0] + m.m[3][0]);
    r.y = static_cast<float>(static_cast<double>(p.x) * m.m[0][1] +
                             static_cast<double>(p.y) * m.m[1][1] +
                             static_cast<double>(p.z) * m.m[2][1] + m.m[3][1]);
    r.z = static_cast<float>(static_cast<double>(p.x) * m.m[0][2] +
                             static_cast<double>(p.y) * m.m[1][2] +
                             static_cast<double>(p.z) * m.m[2][2] + m.m[3][2]);
    return r;
}

// ============================================================
//  Cursor
// ============================================================

void Cursor::RequireRange(size_t off, size_t count, const char* what) const {
    // Getrennte Pruefung statt off+count, weil die Summe
    // ueberlaufen kann - und ein Ueberlauf wuerde die Pruefung
    // gerade dann bestehen lassen, wenn sie greifen muesste.
    if (off > size_ || count > size_ - off) {
        char buf[192];
        std::snprintf(buf, sizeof buf,
                      "%s: %s ab %llu (%llu Byte) liegt ausserhalb (%llu Byte gesamt)",
                      what_, what, static_cast<unsigned long long>(off),
                      static_cast<unsigned long long>(count),
                      static_cast<unsigned long long>(size_));
        throw ParseError(buf);
    }
}

void Cursor::Seek(size_t pos) {
    if (pos > size_) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: Sprung auf %llu, Datei hat %llu Byte",
                      what_, static_cast<unsigned long long>(pos),
                      static_cast<unsigned long long>(size_));
        throw ParseError(buf);
    }
    pos_ = pos;
}

uint8_t Cursor::U8() {
    RequireRange(pos_, 1, "uint8");
    return data_[pos_++];
}

uint16_t Cursor::U16() { uint16_t v = U16At(pos_); pos_ += 2; return v; }
uint32_t Cursor::U32() { uint32_t v = U32At(pos_); pos_ += 4; return v; }
int16_t  Cursor::I16() { int16_t  v = I16At(pos_); pos_ += 2; return v; }
int32_t  Cursor::I32() { return static_cast<int32_t>(U32()); }
float    Cursor::F32() { float    v = F32At(pos_); pos_ += 4; return v; }

uint8_t Cursor::U8At(size_t off) const {
    RequireRange(off, 1, "uint8");
    return data_[off];
}

uint16_t Cursor::U16At(size_t off) const {
    RequireRange(off, 2, "uint16");
    return static_cast<uint16_t>(data_[off] | (data_[off + 1] << 8));
}

uint32_t Cursor::U32At(size_t off) const {
    RequireRange(off, 4, "uint32");
    return static_cast<uint32_t>(data_[off]) |
           (static_cast<uint32_t>(data_[off + 1]) << 8) |
           (static_cast<uint32_t>(data_[off + 2]) << 16) |
           (static_cast<uint32_t>(data_[off + 3]) << 24);
}

int16_t Cursor::I16At(size_t off) const {
    return static_cast<int16_t>(U16At(off));
}

float Cursor::F32At(size_t off) const {
    // Ueber memcpy und nicht per Zeigercast: ein reinterpret_cast
    // auf float* verletzt die Aliasregeln und die Ausrichtung.
    // Die Puffer der .x-Dateien liegen nachweislich unaligned.
    const uint32_t bits = U32At(off);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void Cursor::ReadBytes(void* dest, size_t count) {
    RequireRange(pos_, count, "Block");
    std::memcpy(dest, data_ + pos_, count);
    pos_ += count;
}

std::string Cursor::CStrAt(size_t off, size_t limit) const {
    if (off >= size_) return std::string();
    const size_t maxLen = (size_ - off < limit) ? (size_ - off) : limit;
    size_t len = 0;
    while (len < maxLen && data_[off + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(data_ + off), len);
}

// ============================================================
//  GBK -> UTF-8
// ============================================================

namespace {

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

bool IsPureAscii(const std::string& raw) {
    return std::none_of(raw.begin(), raw.end(),
                        [](unsigned char c) { return c >= 0x80; });
}

} // namespace

std::string GbkToUtf8(const std::string& rawIn) {
    // Reines ASCII bleibt, wie es ist - das ist der Normalfall.
    // Die Umwandlung darf einen Namen wie "Bip001 L Hand" nicht
    // anfassen.
    if (IsPureAscii(rawIn)) return rawIn;

    // Namen koennen auf einem ABGESCHNITTENEN Doppelbyte enden
    // (020_skin2.x: Mesh "阿尔" + ein einzelnes Leitbyte). Windows
    // macht daraus ein "?", das Blender-Addon laesst es weg. Hier
    // wie im Addon: das unvollstaendige Ende abschneiden.
    std::string raw = rawIn;
    for (size_t i = 0; i < raw.size();) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c < 0x80) { ++i; continue; }
        if (i + 1 >= raw.size()) { raw.resize(i); break; }
        i += 2;
    }
    if (raw.empty() || IsPureAscii(raw)) return raw;

#ifdef _WIN32
    // Unter Windows die echte Codepage 936 benutzen. Sie kennt
    // die Herstellererweiterungen, die eine handgeschriebene
    // Tabelle nicht haette.
    const int wideLen = MultiByteToWideChar(936, 0, raw.c_str(),
                                            static_cast<int>(raw.size()), nullptr, 0);
    if (wideLen > 0) {
        std::wstring wide(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(936, 0, raw.c_str(), static_cast<int>(raw.size()),
                            &wide[0], wideLen);
        const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                                                nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string out(static_cast<size_t>(utf8Len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLen,
                                &out[0], utf8Len, nullptr, nullptr);
            return out;
        }
    }
#endif

    // ------------------------------------------------------------
    //  Ohne Windows-API: die Doppelbytefolgen werden erkannt und
    //  als eigener Bereich in die Privatzone abgebildet.
    //
    //  Das ist KEINE echte GBK-Umwandlung - es gibt keine
    //  Zeichentabelle im Programm. Es ist eine umkehrbare
    //  Abbildung, damit der Name nicht verlorengeht, die Laenge
    //  reproduzierbar ist und der Dumpvergleich gegen Python
    //  aufgeht. Unter Windows, wo der Name im Viewport landet,
    //  greift der Zweig darueber.
    //
    //  Python macht in pydump.py dasselbe (siehe gbk_marker dort),
    //  damit beide Seiten zeichengleich bleiben.
    // ------------------------------------------------------------
    std::string out;
    size_t i = 0;
    while (i < raw.size()) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        if (i + 1 < raw.size() && c >= 0x81 && c <= 0xFE) {
            const unsigned char d = static_cast<unsigned char>(raw[i + 1]);
            if (d >= 0x40 && d <= 0xFE && d != 0x7F) {
                AppendUtf8(out, 0xE000u + (static_cast<uint32_t>(c - 0x81) * 191u) +
                                (d >= 0x7F ? (d - 0x41u) : (d - 0x40u)));
                i += 2;
                continue;
            }
        }
        AppendUtf8(out, 0xE000u + 0xB000u + c);   // Einzelbyte, das keine Folge bildet
        ++i;
    }
    return out;
}

// ============================================================
//  Datei lesen
// ============================================================

namespace {

std::FILE* OpenBinaryRead(const std::string& path) {
#ifdef _WIN32
    // fopen nimmt unter Windows die ANSI-Codepage. Ein Pfad mit
    // Umlaut oder Kanji faellt damit durch. Also selbst nach
    // UTF-16 wandeln und _wfopen benutzen. (Fund aus BAF Toolkit,
    // dort mit Rundlauf gegen Umlaute, Kana, Kanji und Emoji
    // geprueft.)
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                                            static_cast<int>(path.size()), nullptr, 0);
    if (wideLen <= 0) return nullptr;
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
                        &wide[0], wideLen);
    return _wfopen(wide.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

} // namespace

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out,
                   std::string& error) {
    out.clear();
    std::FILE* file = OpenBinaryRead(path);
    if (!file) { error = path + ": laesst sich nicht oeffnen"; return false; }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        error = path + ": laesst sich nicht bis zum Ende lesen";
        return false;
    }
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        std::fclose(file);
        error = path + ": leere Datei";
        return false;
    }

    out.resize(static_cast<size_t>(length));
    const size_t got = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    if (got != out.size()) {
        out.clear();
        error = path + ": nicht vollstaendig lesbar";
        return false;
    }
    return true;
}

// ============================================================
//  Zahlenausgabe
// ============================================================

std::string FormatFloat(double value, int decimals) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return (value < 0.0) ? "-inf" : "inf";
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;

    static const double kPow10[10] = {
        1.0, 10.0, 100.0, 1000.0, 10000.0,
        100000.0, 1000000.0, 10000000.0, 100000000.0, 1000000000.0
    };
    static const uint64_t kPow10i[10] = {
        1ull, 10ull, 100ull, 1000ull, 10000ull,
        100000ull, 1000000ull, 10000000ull, 100000000ull, 1000000000ull
    };

    const double scaled = value * kPow10[decimals];
    const double magnitude = std::floor(std::fabs(scaled) + 0.5);

    // Ueber 2^63 gibt es keine sinnvolle Festkommaausgabe mehr.
    // In gesunden Daten kommt das nicht vor, eine beschaedigte
    // Datei darf hier aber keinen undefinierten Cast ausloesen.
    if (magnitude >= 9223372036854775808.0) return (value < 0.0) ? "-huge" : "huge";

    const uint64_t units = static_cast<uint64_t>(magnitude);
    const uint64_t whole = units / kPow10i[decimals];
    const uint64_t fraction = units % kPow10i[decimals];

    std::string text;
    // Das Vorzeichen haengt am Eingabewert, nicht am Ergebnis:
    // -0.0000001 soll "-0.000000" ergeben, so wie "%.6f" es tut.
    if (std::signbit(value)) text += '-';
    text += std::to_string(whole);
    if (decimals > 0) {
        text += '.';
        std::string digits = std::to_string(fraction);
        text.append(static_cast<size_t>(decimals) - digits.size(), '0');
        text += digits;
    }
    return text;
}

std::string Vec3Text(const Vec3& v) {
    return FormatFloat(v.x) + " " + FormatFloat(v.y) + " " + FormatFloat(v.z);
}

std::string Vec2Text(const Vec2& v) {
    return FormatFloat(v.x) + " " + FormatFloat(v.y);
}

std::string QuatText(const Quat& q) {
    return FormatFloat(q.x) + " " + FormatFloat(q.y) + " " +
           FormatFloat(q.z) + " " + FormatFloat(q.w);
}

uint64_t Fnv1a(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Hex16(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx",
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
}

} // namespace eg3d
