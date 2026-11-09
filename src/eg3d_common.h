// ============================================================
//  EG3D Import - gemeinsame Basis
//
//  Enthaelt, was beide Formate brauchen und was NICHT vom Max SDK
//  abhaengt: Vektortypen, ein bereichsgepruefter Lesecursor und
//  die deterministische Zahlenausgabe fuer die Dumps.
//
//  Diese Datei darf KEINEN Max-Header einbinden. Der ganze
//  Parser wird auch von eg3ddump.exe benutzt, das ohne SDK baut -
//  und genau das ist der Grund, warum ein Formatfehler hier in
//  Sekunden auffaellt statt nach zwoelf SDK-Builds.
// ============================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>

namespace eg3d {

// ------------------------------------------------------------
//  Fehler beim Lesen. Wird im Parser geworfen und an der
//  Aussengrenze (ReadModelFile / ReadXFile) gefangen - in den
//  Max-Prozess darf keine Ausnahme entkommen.
// ------------------------------------------------------------
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& what) : std::runtime_error(what) {}
};

// ------------------------------------------------------------
//  Vektoren
//
//  Bewusst eigene Typen statt Point3/Quat aus dem SDK: der
//  Parser laeuft auch ohne Max, und die Umrechnung in Max-Typen
//  gehoert in die Max-Schicht, nicht hierher.
// ------------------------------------------------------------
struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

// Quaternion in der Reihenfolge, in der BEIDE Formate sie
// speichern: x, y, z, w. Nicht w zuerst.
struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };

// ------------------------------------------------------------
//  4x4-Matrix in ZEILENVEKTOR-Konvention
//
//  Beide Formate legen die Translation in die letzte ZEILE, so
//  wie 3ds Max es auch tut. m[r][c] ist also direkt das, was
//  Matrix3::SetRow(r, ...) erwartet - transponiert wird nirgends.
//
//  Das Blender-Addon transponiert an dieser Stelle, weil Blender
//  die andere Konvention hat. Diese Transposition ist hier
//  ersatzlos weg; wer den Addon-Code danebenlegt, muss das im
//  Kopf haben.
// ------------------------------------------------------------
struct Matrix44 {
    float m[4][4];

    Matrix44() {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[r][c] = (r == c) ? 1.0f : 0.0f;
    }
};

Matrix44 MatMul(const Matrix44& a, const Matrix44& b);   // a dann b (Zeilenvektor)
bool     MatInvert(const Matrix44& in, Matrix44& out);   // false = singulaer
Matrix44 MatFromQuatPosScale(const Quat& q, const Vec3& t, const Vec3& s);
Vec3     MatTransformPoint(const Matrix44& m, const Vec3& p);

// ------------------------------------------------------------
//  Bereichsgepruefter Lesecursor
//
//  Jede Leseoperation prueft vorher. Eine kaputte oder fremde
//  Datei soll eine Meldung ergeben, keinen Absturz im
//  Max-Prozess - dort nimmt ein Zugriff daneben die ganze Sitzung
//  samt ungespeicherter Szene mit.
// ------------------------------------------------------------
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size, const char* what = "Daten")
        : data_(data), size_(size), pos_(0), what_(what) {}

    size_t Pos() const  { return pos_; }
    size_t Size() const { return size_; }
    void   Seek(size_t pos);
    void   Skip(size_t count) { Seek(pos_ + count); }
    bool   AtEnd() const { return pos_ >= size_; }

    uint8_t  U8();
    uint16_t U16();
    uint32_t U32();
    int16_t  I16();
    int32_t  I32();
    float    F32();

    // Direktzugriff mit Bereichspruefung, ohne den Cursor zu bewegen.
    uint8_t  U8At(size_t off) const;
    uint16_t U16At(size_t off) const;
    uint32_t U32At(size_t off) const;
    int16_t  I16At(size_t off) const;
    float    F32At(size_t off) const;

    void ReadBytes(void* dest, size_t count);
    void RequireRange(size_t off, size_t count, const char* what) const;

    const uint8_t* Raw() const { return data_; }

    // Nullterminierte Zeichenkette ab off, hoechstens limit Byte.
    // Leer, wenn off ausserhalb liegt.
    std::string CStrAt(size_t off, size_t limit = 512) const;

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
    const char*    what_;
};

// ------------------------------------------------------------
//  Zeichenkodierung
//
//  Die .x-Dateien tragen chinesische Bone- und Meshnamen in GBK.
//  Das Addon probiert ascii, dann gbk, dann gb18030. Hier wird
//  dasselbe gemacht, aber ohne Windows-API - der Parser soll auch
//  unter Linux gegen die Python-Referenz laufen.
//
//  Rueckgabe ist immer UTF-8. Die Umwandlung nach UTF-16 fuer
//  Max passiert erst in der Max-Schicht.
// ------------------------------------------------------------
std::string GbkToUtf8(const std::string& raw);

// Datei komplett einlesen. Unter Windows ueber _wfopen, weil
// fopen dort die ANSI-Codepage nimmt und an Pfaden mit Umlauten
// oder Kanji scheitert (Fund aus BAF Toolkit 0.2.0).
bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out,
                   std::string& error);

// ------------------------------------------------------------
//  Deterministische Zahlenausgabe fuer die Dumps
//
//  NICHT printf und NICHT std::to_chars: keines von beiden legt
//  fest, wohin ein exakter Rundungs-Gleichstand faellt. Der
//  C++-Standard sagt zu to_chars(fixed) nur "wie printf im
//  C-Locale", und die Tie-Regel von printf ist Sache der
//  Implementierung - die UCRT von Microsoft darf anders runden
//  als die glibc, gegen die die Python-Referenz laeuft.
//
//  In den BAF-Daten lagen 473 Werte exakt auf der Grenze der
//  6. Nachkommastelle. Da wuerde der Vergleich Abweichungen
//  melden, an denen nichts falsch ist. Also wird selbst
//  gerechnet: skalieren, Betrag, +0.5, abrunden. pydump.py macht
//  Zeile fuer Zeile dasselbe.
//
//  Nebeneffekt: kein <charconv>. Damit bauen alle Max-Jahrgaenge
//  2016-2027 durch denselben Code (to_chars fuer float gibt es in
//  MSVC erst ab VS 2019 16.4).
// ------------------------------------------------------------
std::string FormatFloat(double value, int decimals = 6);
std::string Vec3Text(const Vec3& v);
std::string Vec2Text(const Vec2& v);
std::string QuatText(const Quat& q);

// FNV-1a 64 Bit. Damit passt ein ganzer Puffer in eine
// Vergleichszeile, statt ihn als Hexdump auszuschreiben.
uint64_t    Fnv1a(const uint8_t* data, size_t size);
std::string Hex16(uint64_t value);

} // namespace eg3d
