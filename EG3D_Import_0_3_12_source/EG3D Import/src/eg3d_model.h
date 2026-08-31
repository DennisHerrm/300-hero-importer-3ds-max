// ============================================================
//  EG3D Import - das .model-Format (EG3D / ezgame)
//
//  DIE EINE SACHE, DIE ZAEHLT
//
//  Jeder Offset im JSON ist RELATIV ZUM BINAERBLOCK, und der
//  beginnt bei Dateibyte 12 (hinter 'EG3D', u32 version,
//  u32 mainSize):
//
//      absolut = 12 + offsetAusJson
//
//  Fruehere Versuche haben die Offsets als absolute Dateioffsets
//  gelesen. Das sind genau 12 Byte daneben, und 12 Byte sind je
//  nach Attribut 1,5 bis 6 Elemente - das Mesh wird also aus
//  fremden Vertices gebaut. Die Symptome davon (Loecher,
//  Bridge-Triangles, |q| = 0,14 beim Skelett, Gewichte != 1, ein
//  scheinbarer 6-u16-Muellkopf vor jedem Indexpuffer) haben
//  jahrelang wie sechs verschiedene Probleme ausgesehen. Es war
//  eines.
//
//  VerifyLayout() prueft genau das: mit der richtigen Basis endet
//  jeder Puffer exakt dort, wo der naechste beginnt. Wenn alle
//  Luecken denselben Wert != 0 haben, ist die Basis falsch.
//
//  KONVENTIONEN
//
//  - Bind-Matrizen liegen in Zeilenvektor-Konvention vor
//    (Translation in der letzten ZEILE) - dieselbe wie Max.
//    Es wird NICHT transponiert. Das Blender-Addon transponiert,
//    weil Blender die andere Konvention hat.
//  - Die Bone-Indices im Vertex sind LOKALE Indices in die
//    Palette, nicht globale Knotenindices.
//  - meta[5] ist NICHT die Bind-Pose, sondern Frame 0 des
//    autoPlay-Clips. Die Bind-Pose steckt allein in den inversen
//    Bind-Matrizen.
// ============================================================
#pragma once

#include "eg3d_common.h"
#include "eg3d_json.h"

#include <map>
#include <string>
#include <vector>

namespace eg3d {

// Der Binaerblock beginnt bei Byte 12. Siehe oben.
const size_t kBlockBase = 12;

// Attribut-Kennungen aus dem Submesh-Deskriptor.
enum AttrId {
    kAttrPosition = 0,
    kAttrNormal   = 1,   // 4 Byte: int16 Hoehe + int16 Azimut, siehe DecodeNormal
    kAttrTangent  = 2,   // float16 x4
    kAttrColor    = 3,   // unorm8 x4 RGBA
    kAttrBones    = 4,   // uint8 x4, LOKALER Palettenindex
    kAttrWeights  = 5,   // float16 x4
    kAttrUV0      = 6,   // uint16 x2, quantisiert
    kAttrUV1      = 7    // uint16 x2, quantisiert
};

// Typcodes. 4 und 5 sind beide float16 - der Unterschied liegt
// nicht in der Kodierung.
enum AttrTypeCode {
    kTypeUint8   = 0,
    kTypeUnorm8  = 1,
    kTypeUint16  = 2,
    kTypeInt8    = 3,
    kTypeHalfA   = 4,
    kTypeHalfB   = 5
};

struct AttrDesc {
    int      id = -1;
    int      typeCode = 0;
    int      qualifier = 0;    // Feld 2: 5 fuer alles ausser Bone-Indices (dort 2)
    int      components = 0;
    int      normalizeFlag = 0;
    int64_t  dataOffset = 0;   // relativ zum Binaerblock
    bool     hasQuant = false;
    int64_t  scaleOffset = 0;
    int64_t  biasOffset = 0;
    float    scale[4] = {0, 0, 0, 0};
    float    bias[4]  = {0, 0, 0, 0};

    size_t   stride = 0;       // Byte je Vertex
    size_t   absStart = 0;
    size_t   absEnd = 0;
};

struct Submesh {
    int    group = 0;
    int    indexInGroup = 0;
    int    material = 0;
    int    vertexCount = 0;
    int64_t idxOffset = 0;
    int    idxCount = 0;

    std::vector<AttrDesc> attrs;      // in Dateireihenfolge

    std::vector<Vec3>     positions;
    std::vector<Vec3>     normals;    // aus Attribut 1 dekodiert, leer wenn nicht vorhanden
    std::vector<Vec4>     tangents;
    std::vector<Vec4>     colors;
    std::vector<Vec2>     uv0;
    std::vector<Vec2>     uv1;
    std::vector<uint8_t>  boneIndices; // 4 je Vertex
    std::vector<float>    weights;     // 4 je Vertex
    std::vector<uint8_t>  rawNormalBytes;  // 4 je Vertex, undekodiert (Forschung)

    std::vector<uint32_t> indices;
    int                   outOfRangeIndices = 0;

    std::string Name() const;
    bool Has(int attrId) const;
};

struct Palette {
    std::vector<int>      bones;     // globale Knotenindices
    std::vector<Matrix44> invBind;   // inverse Bind-Matrix je Eintrag
    size_t                absStart = 0;
    size_t                absEnd = 0;
    int                   singular = 0;   // nicht invertierbare Matrizen
};

struct ModelNode {
    int         index = 0;
    std::string name;
    Vec3        translation;
    Quat        rotation;
    Vec3        scale{1.0f, 1.0f, 1.0f};
    bool        hasTranslation = false;
    bool        hasRotation = false;
    bool        hasScale = false;
    bool        implausibleScale = false;
    float       quatLength = 1.0f;
    std::vector<int> children;
    int         parent = -1;
    int         meshGroup = -1;   // Feld 6
    int         palette = -1;     // Feld 7 - NICHT identisch mit meshGroup!
};

// Wo eine Geometriegruppe platziert ist. Eine Gruppe kann an
// MEHREREN Knoten haengen (039_skin8.model instanziert Waffe und
// Umhang je zweimal).
struct Instance {
    int group = 0;
    int node = 0;
    int palette = -1;   // -1 = starr, reitet auf dem Knoten
};

enum ChannelKind {
    kChannelLocation = 0,
    kChannelRotation = 1,
    kChannelScale    = 2,
    kChannelOther    = 99   // benannte Kanaele: active, material._Color, ...
};

// Werttypen aus dem Kanaldeskriptor (Feld 6, zweiter Eintrag).
enum ChannelValueType {
    kValueTypeFloat = 0,    // vier Byte je Komponente
    kValueTypeBool  = 6     // ein Byte je Komponente, nur 0 oder 1
};

struct Channel {
    int         node = 0;
    int         kind = kChannelOther;
    std::string kindName;      // bei kChannelOther der Name aus der Datei
    int         field2 = 0;
    int         keyCount = 0;
    int         components = 0;
    int64_t     timeOffset = 0;
    int         timeType = 0;
    int64_t     valueOffset = 0;
    int         valueType = 0;

    // Zeiten sind uint16-DELTAS in Millisekunden, erster Eintrag 0.
    // Hier stehen sie schon aufsummiert, also absolut.
    std::vector<double> timesMs;
    // keyCount * components float32. Gefuellt bei valueType 0 - das
    // sind die Kanaele 0/1/2 (Position, Rotation, Skalierung) UND die
    // benannten Materialkanaele (material._Color, _TintColor,
    // _TilingOffset), die dieselbe Kodierung benutzen.
    std::vector<float>  values;
    // keyCount * components uint8, jeder Wert 0 oder 1. Gefuellt bei
    // valueType 6 - die Sichtbarkeitskanaele active / enable /
    // rendererenable. Nachgemessen ueber 1245 Keys in 1099 Kanaelen:
    // ausschliesslich 0x00 und 0x01, nie ein anderer Wert.
    std::vector<uint8_t> boolValues;
    bool                decoded = false;
    // true, wenn die Werte in boolValues statt in values stehen.
    bool                isBool = false;
};

struct Clip {
    std::string          name;
    bool                 flag = false;   // meta[7][i][2], in allen Testdaten false
    std::vector<Channel> channels;
    double               durationMs = 0.0;
};

struct Material {
    std::string name;
    std::string type;                        // character / avmesh / ezParticle / trail
    std::map<std::string, int>   textures;   // Slotname -> Texturindex
    std::map<std::string, Vec4>  vectors;    // _TintColor, _TilingOffset, ...
    int  blendMode = 0;
    bool doubleSide = false;
    bool depthWrite = true;
    bool depthTest = true;
};

struct Texture {
    std::string uri;
};

struct LayoutBlock {
    std::string owner;
    size_t      start = 0;
    size_t      end = 0;
};

struct ModelFile {
    std::string path;
    std::string name;         // Dateiname ohne Endung
    size_t      fileSize = 0;
    uint32_t    version = 0;
    uint32_t    mainSize = 0;
    size_t      jsonStart = 0;
    size_t      jsonChars = 0;
    size_t      jsonTrailing = 0;

    std::string generator;
    std::string generatorVersion;
    bool        leftHandCoord = false;

    std::vector<Material>              materials;
    std::vector<Texture>               textures;
    std::vector<std::vector<Submesh> > groups;
    std::vector<Palette>               palettes;
    std::vector<ModelNode>             nodes;
    std::vector<Instance>              instances;
    std::vector<Clip>                  clips;
    std::vector<LayoutBlock>           layout;   // nach start sortiert

    // Gruppen, die von keinem Knoten benutzt werden. Bei
    // ezParticle-Material sind das Partikelvorlagen, die die
    // Engine zur Laufzeit erzeugt - keine platzierte Geometrie.
    std::vector<int> unplacedGroups;

    int SubmeshCount() const;
    int TriangleCount() const;
    int VertexCount() const;
    const Submesh* FindSubmesh(int group, int index) const;
};

struct ReadResult {
    bool                     ok = false;
    std::string              error;
    std::vector<std::string> warnings;
};

// Erkennung an der Signatur, nicht an der Endung.
bool LooksLikeEg3d(const uint8_t* data, size_t size);

ReadResult ReadModelBuffer(const uint8_t* data, size_t size,
                           const std::string& source, ModelFile& out);
ReadResult ReadModelFile(const std::string& path, ModelFile& out);

// Attribut 1 ist eine Normale als Hoehe plus Azimut, kein Vektor:
//     int16 c0 -> z   = c0 / 16384        (15 Bit, -1..1)
//     int16 c1 -> phi = c1 / 32768 * pi   (16 Bit, voller Kreis)
//     r = sqrt(1 - z*z)
//     n = (-r*sin(phi), -z, -r*cos(phi))
// Herleitung und Verifikation stehen im README des Addons.
Vec3 DecodeNormal(const uint8_t* fourBytes);

// float16 -> float32. Kein _cvtsh_ss und kein <stdfloat>: das
// eine braucht F16C, das andere C++23.
float HalfToFloat(uint16_t half);

// Fuer die Diagnose: jeder Puffer muss exakt dort enden, wo der
// naechste anfaengt. gapCount / nonZeroGaps / allSameGap sagen,
// ob die Basis stimmt.
struct LayoutReport {
    size_t blocks = 0;
    size_t gaps = 0;
    size_t nonZeroGaps = 0;
    long long maxGap = 0;
    bool   allNonZeroSame = false;
    long long commonGap = 0;
};
LayoutReport VerifyLayout(const ModelFile& file);

} // namespace eg3d
