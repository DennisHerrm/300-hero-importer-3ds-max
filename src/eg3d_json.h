// ============================================================
//  EG3D Import - JSON
//
//  Die .model-Datei traegt ihre gesamte Struktur als JSON hinter
//  dem Binaerblock: Materialien, Geometriedeskriptoren,
//  Bone-Paletten, Knotenbaum, Texturen, Animationsclips. Ohne
//  JSON-Parser gibt es kein .model.
//
//  WARUM EIN EIGENER
//
//  Eine fertige Bibliothek waere schnell eingebunden, muesste
//  aber gegen zwoelf Max-Jahrgaenge und zwei Toolsets bauen und
//  liegt danach als Fremdcode im Paket. Der Umfang hier ist
//  ueberschaubar: Arrays, Objekte, Zahlen, Zeichenketten, null.
//  Genau das braucht das Format, mehr steht nicht drin.
//
//  ZWEI PUNKTE, DIE HIER WIRKLICH ZAEHLEN
//
//  1. null ist NICHT 0. Ein Knoten mit translationOffset = null
//     hat keine Translation (Identitaet); einer mit 0 hat eine,
//     die am Anfang des Binaerblocks liegt. Wer null zu 0 macht,
//     liest zwoelf fremde Bytes als Position.
//
//  2. Hinter dem JSON koennen noch Bytes stehen. Die Python-
//     Referenz benutzt raw_decode, das genau einen Wert liest und
//     den Rest liegen laesst. Parse() meldet deshalb, wie viele
//     Zeichen verbraucht wurden - fuenf der 48 Testdateien haben
//     tatsaechlich einen Rest.
//
//  Aufbau: alle Knoten liegen in einem Vektor, Kinder werden
//  ueber Indizes verkettet. JsonRef ist nur ein Griff darauf und
//  kostet beim Kopieren nichts.
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eg3d {

class JsonDoc;

enum class JsonType : uint8_t { Invalid, Null, Bool, Number, String, Array, Object };

class JsonRef {
public:
    JsonRef() : doc_(nullptr), index_(-1) {}
    JsonRef(const JsonDoc* doc, int index) : doc_(doc), index_(index) {}

    bool Valid() const    { return doc_ != nullptr && index_ >= 0; }
    JsonType Type() const;

    bool IsNull() const   { return Type() == JsonType::Null; }
    bool IsNumber() const { return Type() == JsonType::Number; }
    bool IsString() const { return Type() == JsonType::String; }
    bool IsArray() const  { return Type() == JsonType::Array; }
    bool IsObject() const { return Type() == JsonType::Object; }

    // Elementzahl von Array oder Objekt, sonst 0.
    size_t Size() const;

    // Array-Element. Ungueltiger Index gibt einen ungueltigen Ref
    // zurueck, keine Ausnahme - der Aufrufer prueft mit Valid().
    JsonRef At(size_t index) const;

    // Objekt-Mitglied nach Namen.
    JsonRef Get(const char* key) const;

    // Name des i-ten Objekt-Mitglieds.
    std::string KeyAt(size_t index) const;

    double      Num(double fallback = 0.0) const;
    int64_t     Int(int64_t fallback = 0) const;
    bool        Bool(bool fallback = false) const;
    std::string Str(const char* fallback = "") const;

    // "Zahl ODER null": liefert false, wenn der Wert fehlt oder
    // null ist. Genau die Unterscheidung aus Punkt 1 oben.
    bool TryInt(int64_t& out) const;

private:
    const JsonDoc* doc_;
    int            index_;
    friend class JsonDoc;
};

class JsonDoc {
public:
    // Liest GENAU EINEN Wert ab text und meldet in consumed, wie
    // viele Zeichen dafuer verbraucht wurden. Ein Rest dahinter
    // ist kein Fehler.
    bool Parse(const char* text, size_t length, size_t& consumed, std::string& error);

    JsonRef Root() const { return JsonRef(this, nodes_.empty() ? -1 : 0); }

private:
    struct Node {
        JsonType type = JsonType::Invalid;
        double   number = 0.0;
        bool     boolean = false;
        uint32_t stringStart = 0;   // in strings_
        uint32_t stringLength = 0;
        uint32_t firstChild = 0;    // in children_
        uint32_t childCount = 0;
    };

    std::vector<Node>     nodes_;
    std::vector<int>      children_;   // Knotenindizes
    std::vector<uint32_t> keyStart_;   // parallel zu children_, nur bei Objekten
    std::vector<uint32_t> keyLength_;
    std::string           strings_;

    friend class JsonRef;

    // Rekursiver Abstieg. depth begrenzt die Schachtelung: ein
    // beschaedigter Kopf mit 100.000 offenen Klammern wuerde sonst
    // den Stapel sprengen, und ein Stapelueberlauf im Max-Prozess
    // nimmt die Sitzung mit.
    int  ParseValue(const char* text, size_t length, size_t& pos, int depth,
                    std::string& error);
    bool ParseString(const char* text, size_t length, size_t& pos,
                     uint32_t& start, uint32_t& len, std::string& error);
    static void SkipWhitespace(const char* text, size_t length, size_t& pos);
};

} // namespace eg3d
