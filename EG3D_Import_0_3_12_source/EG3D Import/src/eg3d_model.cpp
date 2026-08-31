#include "eg3d_model.h"
#include "eg3d_parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eg3d {

namespace {

const double kPi = 3.14159265358979323846;

std::string BaseName(const std::string& path) {
    size_t cut = path.find_last_of("/\\");
    std::string file = (cut == std::string::npos) ? path : path.substr(cut + 1);
    size_t dot = file.find_last_of('.');
    if (dot != std::string::npos && dot > 0) file.resize(dot);
    return file;
}

// Byte je Vertex fuer ein Attribut.
//
// int8 und uint8 mit weniger als 4 Komponenten sind auf 4 Byte
// aufgefuellt - das vierte Byte ist Fuellung beziehungsweise
// Haendigkeit. Genau das trifft Attribut 1 (int8 x3, aber 4 Byte),
// und genau diese 4 Byte sind in Wahrheit zwei int16.
size_t AttrStride(int typeCode, int components) {
    size_t elementSize = 0;
    switch (typeCode) {
    case kTypeUint8:  case kTypeUnorm8: case kTypeInt8: elementSize = 1; break;
    case kTypeUint16: case kTypeHalfA:  case kTypeHalfB: elementSize = 2; break;
    default: elementSize = 0; break;
    }
    if (elementSize == 0) return 0;
    if ((typeCode == kTypeUint8 || typeCode == kTypeInt8) && components < 4) return 4;
    return elementSize * static_cast<size_t>(components);
}

double NormalizeDenominator(int typeCode) {
    switch (typeCode) {
    case kTypeUint8: case kTypeUnorm8: return 255.0;
    case kTypeInt8:                    return 127.0;
    case kTypeUint16:                  return 65535.0;
    default:                           return 0.0;   // 0 = nicht normalisieren
    }
}

// Einen Rohwert lesen. Der Rueckgabewert ist bewusst double: die
// Quantisierung rechnet danach bias + roh * scale, und das soll
// nicht zweimal ueber float gehen.
double ReadComponent(const Cursor& cursor, size_t offset, int typeCode) {
    switch (typeCode) {
    case kTypeUint8:  case kTypeUnorm8: return cursor.U8At(offset);
    case kTypeInt8:   return static_cast<int8_t>(cursor.U8At(offset));
    case kTypeUint16: return cursor.U16At(offset);
    case kTypeHalfA:  case kTypeHalfB: return HalfToFloat(cursor.U16At(offset));
    default:          return 0.0;
    }
}

size_t ComponentSize(int typeCode) {
    switch (typeCode) {
    case kTypeUint8: case kTypeUnorm8: case kTypeInt8: return 1;
    case kTypeUint16: case kTypeHalfA: case kTypeHalfB: return 2;
    default: return 0;
    }
}

const char* AttrName(int id) {
    switch (id) {
    case kAttrPosition: return "POSITION";
    case kAttrNormal:   return "NORMAL";
    case kAttrTangent:  return "TANGENT";
    case kAttrColor:    return "COLOR";
    case kAttrBones:    return "BONE_INDICES";
    case kAttrWeights:  return "BONE_WEIGHTS";
    case kAttrUV0:      return "UV0";
    case kAttrUV1:      return "UV1";
    default:            return "ATTR";
    }
}

} // namespace

// ============================================================
//  Kleine Umrechnungen
// ============================================================

float HalfToFloat(uint16_t half) {
    const uint32_t sign = static_cast<uint32_t>(half >> 15) << 31;
    uint32_t exponent = (half >> 10) & 0x1F;
    uint32_t mantissa = half & 0x3FF;

    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;                       // +-0
        } else {
            // Subnormal: normalisieren, bis das fuehrende Bit
            // herausgeschoben ist.
            exponent = 1;
            while ((mantissa & 0x400) == 0) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FF;
            bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);   // inf / nan
    } else {
        bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }

    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

Vec3 DecodeNormal(const uint8_t* b) {
    const int16_t c0 = static_cast<int16_t>(
        static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
    const int16_t c1 = static_cast<int16_t>(
        static_cast<uint16_t>(b[2]) | (static_cast<uint16_t>(b[3]) << 8));

    const double z = static_cast<double>(c0) / 16384.0;
    const double phi = static_cast<double>(c1) / 32768.0 * kPi;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));

    double nx = -r * std::sin(phi);
    double ny = -z;
    double nz = -r * std::cos(phi);
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const double inv = 1.0 / std::max(length, 1e-9);

    Vec3 n;
    n.x = static_cast<float>(nx * inv);
    n.y = static_cast<float>(ny * inv);
    n.z = static_cast<float>(nz * inv);
    return n;
}

// ============================================================
//  Submesh / ModelFile
// ============================================================

std::string Submesh::Name() const {
    return "g" + std::to_string(group) + "_s" + std::to_string(indexInGroup);
}

bool Submesh::Has(int attrId) const {
    for (size_t i = 0; i < attrs.size(); ++i)
        if (attrs[i].id == attrId) return true;
    return false;
}

int ModelFile::SubmeshCount() const {
    int total = 0;
    for (size_t g = 0; g < groups.size(); ++g)
        total += static_cast<int>(groups[g].size());
    return total;
}

int ModelFile::TriangleCount() const {
    int total = 0;
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t s = 0; s < groups[g].size(); ++s)
            total += static_cast<int>(groups[g][s].indices.size() / 3);
    return total;
}

int ModelFile::VertexCount() const {
    int total = 0;
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t s = 0; s < groups[g].size(); ++s)
            total += groups[g][s].vertexCount;
    return total;
}

const Submesh* ModelFile::FindSubmesh(int group, int index) const {
    if (group < 0 || static_cast<size_t>(group) >= groups.size()) return nullptr;
    const std::vector<Submesh>& g = groups[static_cast<size_t>(group)];
    if (index < 0 || static_cast<size_t>(index) >= g.size()) return nullptr;
    return &g[static_cast<size_t>(index)];
}

bool LooksLikeEg3d(const uint8_t* data, size_t size) {
    return data && size >= 12 && std::memcmp(data, "EG3D", 4) == 0;
}

// ============================================================
//  Attribute
// ============================================================

namespace {

void ReadAttrDesc(const JsonRef& desc, AttrDesc& out) {
    if (!desc.IsArray() || desc.Size() < 6)
        throw ParseError("Attribut-Deskriptor hat weniger als 6 Felder");

    out.id            = static_cast<int>(desc.At(0).Int(-1));
    out.typeCode      = static_cast<int>(desc.At(1).Int(-1));
    out.qualifier     = static_cast<int>(desc.At(2).Int(0));
    out.components    = static_cast<int>(desc.At(3).Int(0));
    out.normalizeFlag = static_cast<int>(desc.At(4).Int(0));
    out.dataOffset    = desc.At(5).Int(0);

    // Felder 6 und 7 sind Offsets von Skalierung und Versatz. Sie
    // sind entweder BEIDE da oder gar nicht - und "null" ist hier
    // nicht dasselbe wie 0.
    if (desc.Size() >= 8) {
        int64_t scaleOffset = 0, biasOffset = 0;
        if (desc.At(6).TryInt(scaleOffset) && desc.At(7).TryInt(biasOffset)) {
            out.hasQuant = true;
            out.scaleOffset = scaleOffset;
            out.biasOffset = biasOffset;
        }
    }

    if (out.components <= 0 || out.components > 4)
        throw ParseError("Attribut " + std::to_string(out.id) +
                         ": unplausible Komponentenzahl " +
                         std::to_string(out.components));
    out.stride = AttrStride(out.typeCode, out.components);
    if (out.stride == 0)
        throw ParseError("Attribut " + std::to_string(out.id) +
                         ": unbekannter Typcode " + std::to_string(out.typeCode));
}

// Ein Attribut eines Submesh dekodieren. Laeuft im Arbeitsthread
// und fasst ausschliesslich diesen einen Submesh an.
void DecodeAttribute(const Cursor& cursor, AttrDesc& desc, Submesh& sub) {
    const size_t vertexCount = static_cast<size_t>(sub.vertexCount);
    const size_t base = kBlockBase + static_cast<size_t>(desc.dataOffset);

    desc.absStart = base;
    desc.absEnd = base + desc.stride * vertexCount;
    cursor.RequireRange(base, desc.stride * vertexCount,
                        std::string(AttrName(desc.id)).c_str());

    if (desc.hasQuant) {
        const size_t scaleAt = kBlockBase + static_cast<size_t>(desc.scaleOffset);
        const size_t biasAt  = kBlockBase + static_cast<size_t>(desc.biasOffset);
        for (int c = 0; c < desc.components; ++c) {
            desc.scale[c] = cursor.F32At(scaleAt + 4u * static_cast<size_t>(c));
            desc.bias[c]  = cursor.F32At(biasAt  + 4u * static_cast<size_t>(c));
        }
        // Skalierung und Versatz liegen direkt hinter den Daten;
        // fuer die Layoutpruefung endet der Block erst dahinter.
        desc.absEnd = biasAt + 4u * static_cast<size_t>(desc.components);
    }

    // Attribut 1 wird NICHT ueber den allgemeinen Weg dekodiert.
    // Die 4 Byte sind zwei int16, nicht drei int8 - der erklaerte
    // Typ (int8 x3) beschreibt nur die Ablage, nicht den Inhalt.
    if (desc.id == kAttrNormal) {
        sub.rawNormalBytes.resize(vertexCount * 4);
        sub.normals.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            const size_t at = base + desc.stride * v;
            uint8_t four[4];
            for (int i = 0; i < 4; ++i) four[i] = cursor.U8At(at + static_cast<size_t>(i));
            std::memcpy(&sub.rawNormalBytes[v * 4], four, 4);
            sub.normals[v] = DecodeNormal(four);
        }
        return;
    }

    const double denominator =
        (desc.normalizeFlag == 1) ? NormalizeDenominator(desc.typeCode) : 0.0;
    const size_t componentSize = ComponentSize(desc.typeCode);

    // Rohwert -> Wert. Reihenfolge wie in der Referenz:
    // erst normalisieren, dann bias + roh * scale.
    auto value = [&](size_t vertex, int component) -> double {
        const size_t at = base + desc.stride * vertex +
                          componentSize * static_cast<size_t>(component);
        double raw = ReadComponent(cursor, at, desc.typeCode);
        if (denominator > 0.0) raw /= denominator;
        if (desc.hasQuant)
            raw = static_cast<double>(desc.bias[component]) +
                  raw * static_cast<double>(desc.scale[component]);
        return raw;
    };

    switch (desc.id) {
    case kAttrPosition: {
        sub.positions.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            sub.positions[v].x = static_cast<float>(value(v, 0));
            sub.positions[v].y = static_cast<float>(value(v, 1));
            sub.positions[v].z = static_cast<float>(value(v, 2));
        }
        break;
    }
    case kAttrTangent: {
        sub.tangents.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            sub.tangents[v].x = static_cast<float>(value(v, 0));
            sub.tangents[v].y = static_cast<float>(value(v, 1));
            sub.tangents[v].z = static_cast<float>(value(v, 2));
            sub.tangents[v].w = (desc.components > 3) ? static_cast<float>(value(v, 3)) : 0.0f;
        }
        break;
    }
    case kAttrColor: {
        sub.colors.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            sub.colors[v].x = static_cast<float>(value(v, 0));
            sub.colors[v].y = static_cast<float>(value(v, 1));
            sub.colors[v].z = static_cast<float>(value(v, 2));
            sub.colors[v].w = (desc.components > 3) ? static_cast<float>(value(v, 3)) : 1.0f;
        }
        break;
    }
    case kAttrBones: {
        // Bewusst als ROHWERT und nicht ueber value(): ein
        // Palettenindex ist eine Nummer, keine Messgroesse. Ihn
        // durch 255 zu teilen, weil irgendwo ein Flag steht,
        // waere ein Fehler, den man erst am schiefen Skinning
        // merkt.
        sub.boneIndices.assign(vertexCount * 4, 0);
        for (size_t v = 0; v < vertexCount; ++v)
            for (int c = 0; c < 4 && c < desc.components; ++c)
                sub.boneIndices[v * 4 + static_cast<size_t>(c)] =
                    cursor.U8At(base + desc.stride * v + static_cast<size_t>(c));
        break;
    }
    case kAttrWeights: {
        sub.weights.assign(vertexCount * 4, 0.0f);
        for (size_t v = 0; v < vertexCount; ++v)
            for (int c = 0; c < 4 && c < desc.components; ++c)
                sub.weights[v * 4 + static_cast<size_t>(c)] =
                    static_cast<float>(value(v, c));
        break;
    }
    case kAttrUV0:
    case kAttrUV1: {
        std::vector<Vec2>& target = (desc.id == kAttrUV0) ? sub.uv0 : sub.uv1;
        target.resize(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            target[v].x = static_cast<float>(value(v, 0));
            target[v].y = static_cast<float>(value(v, 1));
        }
        break;
    }
    default:
        // Unbekanntes Attribut: Grenzen sind eingetragen, der
        // Inhalt bleibt liegen. Kein Fehler - es soll auffallen,
        // nicht abbrechen.
        break;
    }
}

void ParseGeometry(const Cursor& cursor, const JsonRef& meta3, ModelFile& out,
                   std::vector<std::string>& warnings) {
    // Erst alle Deskriptoren einlesen (einfaedig, weil JSON), dann
    // die Daten parallel dekodieren. Die Trennung ist wichtig: die
    // JsonDoc wird von mehreren Threads nur GELESEN, und das ist
    // sie ab hier auch nicht mehr.
    struct Slot { int group; int index; };
    std::vector<Slot> slots;

    for (size_t gi = 0; gi < meta3.Size(); ++gi) {
        const JsonRef group = meta3.At(gi);
        std::vector<Submesh> subs;
        for (size_t si = 0; si < group.Size(); ++si) {
            const JsonRef desc = group.At(si);
            if (!desc.IsArray() || desc.Size() < 4)
                throw ParseError("Submesh-Deskriptor hat weniger als 4 Felder");

            Submesh sub;
            sub.group = static_cast<int>(gi);
            sub.indexInGroup = static_cast<int>(si);
            sub.material = static_cast<int>(desc.At(0).Int(0));
            sub.vertexCount = static_cast<int>(desc.At(1).Int(0));
            if (sub.vertexCount < 0)
                throw ParseError(sub.Name() + ": negative Vertexzahl");

            const JsonRef attrList = desc.At(2);
            for (size_t ai = 0; ai < attrList.Size(); ++ai) {
                AttrDesc attr;
                ReadAttrDesc(attrList.At(ai), attr);
                sub.attrs.push_back(attr);
            }

            const JsonRef range = desc.At(3);
            if (range.Size() >= 2) {
                sub.idxOffset = range.At(0).Int(0);
                sub.idxCount = static_cast<int>(range.At(1).Int(0));
            }
            subs.push_back(sub);
            Slot slot = { static_cast<int>(gi), static_cast<int>(si) };
            slots.push_back(slot);
        }
        out.groups.push_back(subs);
    }

    // ---- der parallele Teil ----
    // Jeder Durchlauf fasst genau einen Submesh an. Keine
    // gemeinsame Variable, keine Reduktion, kein SDK.
    ParallelFor(slots.size(), [&](size_t i) {
        Submesh& sub = out.groups[static_cast<size_t>(slots[i].group)]
                                 [static_cast<size_t>(slots[i].index)];
        for (size_t a = 0; a < sub.attrs.size(); ++a)
            DecodeAttribute(cursor, sub.attrs[a], sub);

        // Indexpuffer: schlichte Dreiecksliste, uint16, KEIN Kopf
        // zum Ueberspringen. idxCount ist die Zahl der Indices,
        // nicht der Bytes.
        const size_t indexAt = kBlockBase + static_cast<size_t>(sub.idxOffset);
        cursor.RequireRange(indexAt, static_cast<size_t>(sub.idxCount) * 2, "Indexpuffer");
        const size_t usable = (static_cast<size_t>(sub.idxCount) / 3) * 3;
        sub.indices.resize(usable);
        for (size_t k = 0; k < usable; ++k)
            sub.indices[k] = cursor.U16At(indexAt + k * 2);

        for (size_t k = 0; k < usable; ++k)
            if (sub.indices[k] >= static_cast<uint32_t>(sub.vertexCount))
                ++sub.outOfRangeIndices;
    });

    // ---- ab hier wieder einfaedig ----
    for (size_t i = 0; i < slots.size(); ++i) {
        const Submesh& sub = out.groups[static_cast<size_t>(slots[i].group)]
                                 [static_cast<size_t>(slots[i].index)];
        for (size_t a = 0; a < sub.attrs.size(); ++a) {
            LayoutBlock block;
            block.owner = sub.Name() + "." + AttrName(sub.attrs[a].id);
            block.start = sub.attrs[a].absStart;
            block.end = sub.attrs[a].absEnd;
            out.layout.push_back(block);
        }
        LayoutBlock indexBlock;
        indexBlock.owner = sub.Name() + ".INDICES";
        indexBlock.start = kBlockBase + static_cast<size_t>(sub.idxOffset);
        indexBlock.end = indexBlock.start + static_cast<size_t>(sub.idxCount) * 2;
        out.layout.push_back(indexBlock);

        if (static_cast<size_t>(sub.idxCount) % 3 != 0)
            warnings.push_back(sub.Name() + ": Indexzahl " +
                               std::to_string(sub.idxCount) +
                               " ist kein Vielfaches von 3");
        if (sub.outOfRangeIndices > 0)
            warnings.push_back(sub.Name() + ": " +
                               std::to_string(sub.outOfRangeIndices) +
                               " Indices >= Vertexzahl - das ist das klassische "
                               "Zeichen fuer eine falsche Pufferbasis");
    }
}

void ParsePalettes(const Cursor& cursor, const JsonRef& meta4, ModelFile& out,
                   std::vector<std::string>& warnings) {
    for (size_t gi = 0; gi < meta4.Size(); ++gi) {
        const JsonRef entry = meta4.At(gi);
        if (!entry.IsArray() || entry.Size() < 2)
            throw ParseError("Paletteneintrag hat weniger als 2 Felder");

        Palette palette;
        const JsonRef bones = entry.At(0);
        palette.bones.reserve(bones.Size());
        for (size_t b = 0; b < bones.Size(); ++b)
            palette.bones.push_back(static_cast<int>(bones.At(b).Int(-1)));

        const size_t base = kBlockBase + static_cast<size_t>(entry.At(1).Int(0));
        palette.absStart = base;
        palette.absEnd = base + 64 * palette.bones.size();
        cursor.RequireRange(base, 64 * palette.bones.size(), "Bind-Matrizen");

        palette.invBind.resize(palette.bones.size());
        for (size_t b = 0; b < palette.bones.size(); ++b) {
            const size_t at = base + 64 * b;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    palette.invBind[b].m[r][c] =
                        cursor.F32At(at + static_cast<size_t>(r * 4 + c) * 4);
            Matrix44 test;
            if (!MatInvert(palette.invBind[b], test)) ++palette.singular;
        }
        if (palette.singular > 0)
            warnings.push_back("Palette " + std::to_string(gi) + ": " +
                               std::to_string(palette.singular) +
                               " nicht invertierbare Bind-Matrix/Matrizen "
                               "(kommt im Bestand vor, z.B. 263_skin5.model)");

        LayoutBlock block;
        block.owner = "palette" + std::to_string(gi) + ".BINDMATRICES";
        block.start = palette.absStart;
        block.end = palette.absEnd;
        out.layout.push_back(block);

        out.palettes.push_back(palette);
    }
}

void ParseNodes(const Cursor& cursor, const JsonRef& meta5, ModelFile& out,
                std::vector<std::string>& warnings) {
    int badQuaternions = 0;

    for (size_t i = 0; i < meta5.Size(); ++i) {
        const JsonRef nd = meta5.At(i);
        ModelNode node;
        node.index = static_cast<int>(i);
        node.name = nd.At(0).IsString() ? nd.At(0).Str()
                                        : ("node_" + std::to_string(i));

        int64_t offset = 0;
        if (nd.At(1).TryInt(offset)) {
            const size_t at = kBlockBase + static_cast<size_t>(offset);
            node.translation.x = cursor.F32At(at);
            node.translation.y = cursor.F32At(at + 4);
            node.translation.z = cursor.F32At(at + 8);
            node.hasTranslation = true;
        }
        if (nd.At(2).TryInt(offset)) {
            const size_t at = kBlockBase + static_cast<size_t>(offset);
            node.rotation.x = cursor.F32At(at);
            node.rotation.y = cursor.F32At(at + 4);
            node.rotation.z = cursor.F32At(at + 8);
            node.rotation.w = cursor.F32At(at + 12);
            const double length = std::sqrt(
                static_cast<double>(node.rotation.x) * node.rotation.x +
                static_cast<double>(node.rotation.y) * node.rotation.y +
                static_cast<double>(node.rotation.z) * node.rotation.z +
                static_cast<double>(node.rotation.w) * node.rotation.w);
            node.quatLength = static_cast<float>(length);
            if (std::fabs(length - 1.0) > 0.02) ++badQuaternions;
            node.hasRotation = true;
        }
        if (nd.At(3).TryInt(offset)) {
            const size_t at = kBlockBase + static_cast<size_t>(offset);
            Vec3 s;
            s.x = cursor.F32At(at);
            s.y = cursor.F32At(at + 4);
            s.z = cursor.F32At(at + 8);
            // Eine entartete Skalierung macht die Matrix
            // uninvertierbar. Filtern statt uebernehmen, wie es
            // die Referenz auch tut.
            const bool plausible =
                std::fabs(s.x) > 0.01f && std::fabs(s.x) < 100.0f &&
                std::fabs(s.y) > 0.01f && std::fabs(s.y) < 100.0f &&
                std::fabs(s.z) > 0.01f && std::fabs(s.z) < 100.0f;
            if (plausible) {
                node.scale = s;
                node.hasScale = true;
            } else {
                node.implausibleScale = true;
            }
        }

        const JsonRef children = nd.At(5);
        for (size_t c = 0; c < children.Size(); ++c)
            node.children.push_back(static_cast<int>(children.At(c).Int(-1)));

        int64_t group = 0;
        if (nd.At(6).TryInt(group)) node.meshGroup = static_cast<int>(group);
        int64_t palette = 0;
        if (nd.At(7).TryInt(palette)) node.palette = static_cast<int>(palette);

        out.nodes.push_back(node);
    }

    for (size_t i = 0; i < out.nodes.size(); ++i)
        for (size_t c = 0; c < out.nodes[i].children.size(); ++c) {
            const int child = out.nodes[i].children[c];
            if (child >= 0 && static_cast<size_t>(child) < out.nodes.size())
                out.nodes[static_cast<size_t>(child)].parent = static_cast<int>(i);
        }

    // ------------------------------------------------------------
    //  Die Palette steht am KNOTEN, nicht an der Gruppe.
    //
    //  Bei Dateien mit gleich vielen Gruppen und Paletten sind
    //  beide Zahlen zufaellig identisch. 039_skin8.model hat 27
    //  Gruppen und 7 Paletten - wer dort ueber die Gruppennummer
    //  indiziert, laesst 20 Submeshes ohne Skinning.
    //
    //  Und eine Gruppe kann an MEHREREN Knoten haengen: dieselbe
    //  Waffe zweimal, an Bone127 und Bone129.
    // ------------------------------------------------------------
    for (size_t i = 0; i < out.nodes.size(); ++i) {
        const ModelNode& node = out.nodes[i];
        if (node.meshGroup < 0) continue;
        if (static_cast<size_t>(node.meshGroup) >= out.groups.size()) continue;
        Instance instance;
        instance.group = node.meshGroup;
        instance.node = static_cast<int>(i);
        instance.palette = node.palette;
        out.instances.push_back(instance);
    }

    std::vector<char> placed(out.groups.size(), 0);
    for (size_t i = 0; i < out.instances.size(); ++i)
        placed[static_cast<size_t>(out.instances[i].group)] = 1;
    for (size_t g = 0; g < placed.size(); ++g)
        if (!placed[g]) out.unplacedGroups.push_back(static_cast<int>(g));

    if (badQuaternions > 0)
        warnings.push_back(std::to_string(badQuaternions) +
                           " Knoten mit |q| != 1 - deutet auf eine falsche "
                           "Pufferbasis hin");
}

void ParseClips(const Cursor& cursor, const JsonRef& meta7, ModelFile& out,
                std::vector<std::string>& warnings) {
    for (size_t ci = 0; ci < meta7.Size(); ++ci) {
        const JsonRef cd = meta7.At(ci);
        Clip clip;
        clip.name = cd.At(0).IsString() ? cd.At(0).Str()
                                        : ("clip_" + std::to_string(ci));
        clip.flag = cd.At(2).Bool(false);

        const JsonRef channels = cd.At(1);
        for (size_t chi = 0; chi < channels.Size(); ++chi) {
            const JsonRef chd = channels.At(chi);
            if (!chd.IsArray() || chd.Size() < 7) continue;

            Channel channel;
            channel.node = static_cast<int>(chd.At(0).Int(-1));

            // ------------------------------------------------------
            //  Feld 1 ist NICHT immer eine Zahl.
            //
            //  Neben 0/1/2 (Position, Rotation, Skalierung) stehen
            //  dort benannte Kanaele. Im Bestand sind das 1198 von
            //  124.000 Kanaelen, in zwei Gruppen:
            //
            //    valueType 0, comps 4 - material._Color (69),
            //      material._TintColor (29), material._TilingOffset
            //      (1). Dieselbe float32-Kodierung wie die Kanaele
            //      0/1/2. Werte im Bereich 0..1, typischerweise
            //      RGBA-Fades.
            //
            //    valueType 6, comps 1 - active (1093),
            //      rendererenable (4), enable (2). Ein Byte je Key,
            //      Wert 0 oder 1. Sichtbarkeitsschalter.
            //
            //  Beide Gruppen werden dekodiert. Was der Kanal
            //  BEDEUTET, entscheidet erst die Max-Schicht: kindName
            //  bleibt erhalten, damit dort ein Sichtbarkeits-
            //  Controller oder ein Materialparameter daraus wird.
            // ------------------------------------------------------
            const JsonRef kind = chd.At(1);
            if (kind.IsNumber()) {
                channel.kind = static_cast<int>(kind.Int(kChannelOther));
                if (channel.kind < 0 || channel.kind > 2) {
                    channel.kindName = std::to_string(channel.kind);
                    channel.kind = kChannelOther;
                }
            } else {
                channel.kind = kChannelOther;
                channel.kindName = kind.Str("?");
            }

            channel.field2     = static_cast<int>(chd.At(2).Int(0));
            channel.keyCount   = static_cast<int>(chd.At(3).Int(0));
            channel.components = static_cast<int>(chd.At(4).Int(0));

            const JsonRef times  = chd.At(5);
            const JsonRef values = chd.At(6);
            if (times.Size() >= 2) {
                channel.timeOffset = times.At(0).Int(0);
                channel.timeType = static_cast<int>(times.At(1).Int(0));
            }
            if (values.Size() >= 2) {
                channel.valueOffset = values.At(0).Int(0);
                channel.valueType = static_cast<int>(values.At(1).Int(0));
            }

            if (channel.keyCount < 0 || channel.components <= 0) continue;

            const size_t timeAt = kBlockBase + static_cast<size_t>(channel.timeOffset);
            const size_t valueAt = kBlockBase + static_cast<size_t>(channel.valueOffset);

            // Die Breite eines Wertes haengt am valueType, nicht am
            // kind: Typ 0 sind vier Byte, Typ 6 ist eines.
            const size_t valueWidth = (channel.valueType == kValueTypeBool) ? 1u : 4u;
            const size_t valueBytes = static_cast<size_t>(channel.keyCount) *
                                      static_cast<size_t>(channel.components) * valueWidth;

            if (timeAt + static_cast<size_t>(channel.keyCount) * 2 > cursor.Size() ||
                valueAt + valueBytes > cursor.Size()) {
                warnings.push_back("Clip '" + clip.name + "' Knoten " +
                                   std::to_string(channel.node) +
                                   ": Kurve liegt ausserhalb der Datei, uebersprungen");
                continue;
            }

            // Zeiten sind uint16-DELTAS in Millisekunden, erster
            // Eintrag 0. Typische Werte 33/66/100 - Vielfache von
            // 1/30 s.
            channel.timesMs.resize(static_cast<size_t>(channel.keyCount));
            double running = 0.0;
            for (int k = 0; k < channel.keyCount; ++k) {
                running += cursor.U16At(timeAt + static_cast<size_t>(k) * 2);
                channel.timesMs[static_cast<size_t>(k)] = running;
            }

            const size_t valueCount = static_cast<size_t>(channel.keyCount) *
                                      static_cast<size_t>(channel.components);
            if (channel.valueType == kValueTypeBool) {
                channel.isBool = true;
                channel.boolValues.resize(valueCount);
                for (size_t v = 0; v < valueCount; ++v) {
                    const uint8_t raw = cursor.U8At(valueAt + v);
                    // Nie etwas anderes als 0 oder 1 gesehen. Falls
                    // doch, soll es auffallen statt still zu 1 zu
                    // werden.
                    if (raw > 1) {
                        warnings.push_back("Clip '" + clip.name + "' Knoten " +
                                           std::to_string(channel.node) + " Kanal '" +
                                           channel.kindName + "': Schaltwert " +
                                           std::to_string(static_cast<int>(raw)) +
                                           " statt 0 oder 1");
                    }
                    channel.boolValues[v] = raw ? 1u : 0u;
                }
                channel.decoded = true;
            } else if (channel.valueType == kValueTypeFloat) {
                channel.values.resize(valueCount);
                for (size_t v = 0; v < valueCount; ++v)
                    channel.values[v] = cursor.F32At(valueAt + v * 4);
                channel.decoded = true;
            } else {
                warnings.push_back("Clip '" + clip.name + "' Knoten " +
                                   std::to_string(channel.node) + " Kanal '" +
                                   (channel.kindName.empty()
                                        ? std::to_string(channel.kind)
                                        : channel.kindName) +
                                   "': unbekannter Werttyp " +
                                   std::to_string(channel.valueType) +
                                   ", nicht dekodiert");
            }

            if (!channel.timesMs.empty())
                clip.durationMs = std::max(clip.durationMs, channel.timesMs.back());
            clip.channels.push_back(channel);
        }
        out.clips.push_back(clip);
    }
}

void ParseMaterialsAndTextures(const JsonRef& meta, ModelFile& out) {
    const JsonRef header = meta.At(0);
    if (header.IsObject()) {
        out.generator = header.Get("generator").Str();
        out.generatorVersion = header.Get("version").Str();
        out.leftHandCoord = header.Get("leftHandCoord").Bool(false);
    }

    const JsonRef materials = meta.At(2);
    for (size_t i = 0; i < materials.Size(); ++i) {
        const JsonRef md = materials.At(i);
        Material material;
        material.name = md.At(0).Str("material");
        material.type = md.At(1).Str();

        const JsonRef props = md.At(2);
        if (props.IsObject()) {
            material.blendMode  = static_cast<int>(props.Get("blendMode").Int(0));
            material.doubleSide = props.Get("doubleSide").Bool(false);
            material.depthWrite = props.Get("depthWrite").Bool(true);
            material.depthTest  = props.Get("depthTest").Bool(true);

            const JsonRef textures = props.Get("textures");
            for (size_t t = 0; t < textures.Size(); ++t) {
                const std::string slot = textures.KeyAt(t);
                material.textures[slot] = static_cast<int>(textures.At(t).Int(-1));
            }
            const JsonRef vectors = props.Get("vectors");
            for (size_t v = 0; v < vectors.Size(); ++v) {
                const JsonRef value = vectors.At(v);
                Vec4 vec;
                if (value.Size() >= 4) {
                    vec.x = static_cast<float>(value.At(0).Num());
                    vec.y = static_cast<float>(value.At(1).Num());
                    vec.z = static_cast<float>(value.At(2).Num());
                    vec.w = static_cast<float>(value.At(3).Num());
                }
                material.vectors[vectors.KeyAt(v)] = vec;
            }
        }
        out.materials.push_back(material);
    }

    const JsonRef textures = meta.At(6);
    for (size_t i = 0; i < textures.Size(); ++i) {
        Texture texture;
        const JsonRef td = textures.At(i);
        texture.uri = td.IsObject() ? td.Get("uri").Str() : td.Str();
        out.textures.push_back(texture);
    }
}

} // namespace

// ============================================================
//  Einstieg
// ============================================================

ReadResult ReadModelBuffer(const uint8_t* data, size_t size,
                           const std::string& source, ModelFile& out) {
    ReadResult result;
    out = ModelFile();
    out.path = source;
    out.name = BaseName(source);

    try {
        if (!LooksLikeEg3d(data, size))
            throw ParseError("keine EG3D-Datei (Signatur passt nicht)");

        Cursor cursor(data, size, "EG3D");
        out.fileSize = size;
        out.version = cursor.U32At(4);
        out.mainSize = cursor.U32At(8);
        out.jsonStart = kBlockBase + out.mainSize;

        if (out.jsonStart >= size)
            throw ParseError("mainSize " + std::to_string(out.mainSize) +
                             " legt den JSON-Teil hinter das Dateiende");

        JsonDoc doc;
        size_t consumed = 0;
        std::string jsonError;
        if (!doc.Parse(reinterpret_cast<const char*>(data + out.jsonStart),
                       size - out.jsonStart, consumed, jsonError))
            throw ParseError("JSON-Metadaten: " + jsonError);

        out.jsonChars = consumed;
        out.jsonTrailing = (size - out.jsonStart) - consumed;

        const JsonRef meta = doc.Root();
        if (!meta.IsArray())
            throw ParseError("Metadaten sind kein Array");
        if (meta.Size() < 8)
            result.warnings.push_back("Metadaten haben nur " +
                                      std::to_string(meta.Size()) +
                                      " Sektionen statt 8");

        ParseMaterialsAndTextures(meta, out);
        ParseGeometry(cursor, meta.At(3), out, result.warnings);
        ParsePalettes(cursor, meta.At(4), out, result.warnings);
        ParseNodes(cursor, meta.At(5), out, result.warnings);
        ParseClips(cursor, meta.At(7), out, result.warnings);

        std::stable_sort(out.layout.begin(), out.layout.end(),
                         [](const LayoutBlock& a, const LayoutBlock& b) {
                             return a.start < b.start;
                         });
    } catch (const ParseError& error) {
        out = ModelFile();
        result.error = source.empty() ? error.what() : (source + ": " + error.what());
        return result;
    } catch (const std::bad_alloc&) {
        out = ModelFile();
        result.error = source + ": Speicher reicht nicht - Datei vermutlich beschaedigt";
        return result;
    }

    result.ok = true;
    return result;
}

ReadResult ReadModelFile(const std::string& path, ModelFile& out) {
    ReadResult result;
    std::vector<uint8_t> buffer;
    if (!ReadWholeFile(path, buffer, result.error)) return result;
    return ReadModelBuffer(buffer.data(), buffer.size(), path, out);
}

LayoutReport VerifyLayout(const ModelFile& file) {
    LayoutReport report;
    report.blocks = file.layout.size();

    bool first = true;
    long long previousEnd = 0;
    long long common = 0;
    bool sameSoFar = true;

    for (size_t i = 0; i < file.layout.size(); ++i) {
        const LayoutBlock& block = file.layout[i];
        if (!first) {
            const long long gap =
                static_cast<long long>(block.start) - previousEnd;
            ++report.gaps;
            if (gap > report.maxGap) report.maxGap = gap;
            if (gap != 0) {
                ++report.nonZeroGaps;
                if (report.nonZeroGaps == 1) common = gap;
                else if (gap != common) sameSoFar = false;
            }
        }
        first = false;
        previousEnd = static_cast<long long>(block.end);
    }

    report.allNonZeroSame = sameSoFar && report.nonZeroGaps > 3;
    report.commonGap = common;
    return report;
}

} // namespace eg3d
