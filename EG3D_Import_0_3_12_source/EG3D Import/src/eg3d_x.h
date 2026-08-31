// ============================================================
//  EG3D Import - das .x-Format (JUMPX V5.01)
//
//  Trotz der Endung ist das KEINE DirectX-X-Datei. Signatur ist
//  "JUMPX V5.01", Hersteller jumpw.com. Erkannt wird an der
//  Signatur, nicht an der Endung.
//
//  AUFBAU
//
//    0x00  "JUMPX V5.01" plus Werbetexte, NUL-gefuellt
//    0x50  u32 = 8, u32 = 300 (Groesse der TOC)
//    0x58  TOC: [4-Byte-Tag][u32 groesse=4][u32 wert]
//          n<x> = Anzahl, a<x> = START-Offset in Block 1
//          Tags: tex mtl geo bon bgp att rib prt act ...
//    ....  u32 x4 = roh1, roh2, gepackt1, gepackt2
//          zlib-Stream 1 -> Block 1 (Deskriptoren)
//          zlib-Stream 2 -> Block 2 (Nutzdaten)
//
//  Die zlib-Kompression ist der Grund, warum das Format seit 2015
//  als ungeloest galt - hinter dem Header sieht alles nach
//  Zufallsbytes aus.
//
//  Jeder 4-Byte-Zeiger in Block 1 ist eine Adresse mit Basis
//  1.000.000.000; abziehen ergibt den Offset in Block 2. Die
//  Zeiger liegen UNALIGNED.
//
//  DREI FALLEN, DIE ES WIRKLICH GIBT
//
//  1. Jeder Puffer hat seine EIGENE Adresse im Deskriptor. Die
//     Offsets aus den Groessen hochzurechnen geht nur, solange
//     keine optionalen Puffer vorkommen - 156_skin10.x hat bei 14
//     von 21 Meshes einen zusaetzlichen Puffer und bei sieben
//     nicht, wodurch sich der Indexpuffer verschiebt.
//
//  2. Die Blockgroesse je Bone ist NICHT konstant: 28 Byte je
//     Frame (Position + Rotation), 32 (+ Sichtbarkeitsflag), 40
//     (+ Skalierung) oder 44 (beides). Sie muss je Bone gelesen
//     werden.
//
//  3. Das Quaternion ist KONJUGIERT gespeichert. Bei der
//     Bind-Pose kuerzt sich das weg, es ist also in jedem Test
//     unsichtbar, der nur Frame 0 anschaut. Sichtbar wird es
//     erst im Maximum der Kantenlaengen waehrend der Animation:
//     ohne Konjugation streckt sich die schlimmste Kante von
//     020_skin2.x auf das 25,5-fache.
//
//  FRAME 0 IST DIE BIND-POSE. Es gibt keine separate
//  Bind-Matrix; inverseBind = invers(Frame 0).
// ============================================================
#pragma once

#include "eg3d_common.h"
#include "eg3d_model.h"   // ReadResult

#include <map>
#include <string>
#include <vector>

namespace eg3d {

const uint32_t kXAddrBase   = 1000000000u;
const size_t   kXBoneStride = 172;
const size_t   kXGeoStride  = 124;
const size_t   kXActStride  = 90;
const size_t   kXMtlStride  = 48;
const size_t   kXTexStride  = 8;

struct XBone {
    int         index = 0;
    std::string name;
    int         parent = -1;
    std::vector<int> children;

    size_t bytesPerFrame = 28;
    std::vector<Vec3> pos;      // je Frame, Weltposition
    std::vector<Quat> quat;     // je Frame, KONJUGIERT gespeichert
    std::vector<Vec3> scale;    // leer, wenn die Datei keine Skalierung fuehrt
    std::vector<char> visible;  // leer, wenn kein Sichtbarkeitsflag vorhanden

    bool HasScale() const   { return !scale.empty(); }
    bool HasVisible() const { return !visible.empty(); }
};

struct XMesh {
    std::string name;
    int         material = 0;
    int         vertexCount = 0;
    int         triangleCount = 0;

    std::vector<Vec3>     positions;
    std::vector<Vec3>     normals;     // schon Einheitsvektoren
    std::vector<Vec2>     uvs;
    std::vector<uint8_t>  paletteIndex; // ein uint8 je Vertex, saettigt bei 255
    std::vector<uint32_t> indices;      // Dreiecksliste
    std::vector<uint8_t>  boneIndices;  // 4 je Vertex
    std::vector<float>    weights;      // 4 je Vertex

    int droppedTriangles = 0;   // 0xFFFF-Fuellung
    int clearedBoneSlots = 0;   // Index 255 = kein Bone
};

struct XClip {
    std::string name;
    int  start = 0;
    int  end = 0;
    bool synthetic = false;   // nicht in der Datei deklariert
};

struct XMaterial {
    int texture = -1;
};

struct XFile {
    std::string path;
    std::string name;
    size_t      fileSize = 0;
    std::string signature;

    std::map<std::string, uint32_t> tags;
    size_t block1Size = 0;
    size_t block2Size = 0;

    int frames = 0;
    double frameSolveDeviation = 0.0;   // Medianabweichung der Siegerhypothese
    double frameSolveUnitShare = 0.0;
    std::vector<int> rejectedFrameCounts;

    std::vector<std::string> textures;
    std::vector<XMaterial>   materials;
    std::vector<XMesh>       meshes;
    std::vector<XBone>       bones;
    std::vector<XClip>       clips;

    int nanBones = 0, nanFrames = 0;
    int denormBones = 0, denormKeys = 0;
    int hierarchyLinks = 0;

    int VertexCount() const;
    int TriangleCount() const;
};

bool LooksLikeJumpx(const uint8_t* data, size_t size);

ReadResult ReadXBuffer(const uint8_t* data, size_t size,
                       const std::string& source, XFile& out);
ReadResult ReadXFile(const std::string& path, XFile& out);

// Weltmatrix eines Bones in einem Frame, Zeilenvektor-Konvention.
//
// HIER wird das Quaternion konjugiert - an genau einer Stelle,
// an der Grenze zwischen Rohdaten und Geometrie. Wer die
// Rohwerte will (etwa fuer den Dumpvergleich), nimmt bone.quat
// direkt; wer eine Pose will, nimmt diese Funktion.
Matrix44 XBoneWorld(const XBone& bone, int frame);

} // namespace eg3d
