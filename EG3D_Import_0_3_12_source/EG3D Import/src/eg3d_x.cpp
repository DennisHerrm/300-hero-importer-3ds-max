#include "eg3d_x.h"
#include "eg3d_inflate.h"
#include "eg3d_parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eg3d {

namespace {

// Nur diese vier Groessen kommen vor. Sie explizit aufzulisten
// haelt den Frameloeser eindeutig - eine offene Formel wuerde
// 063_skin7.x mehrdeutig machen.
const size_t kBytesPerFrame[4] = { 28, 32, 40, 44 };
const size_t kScaleMinBpf = 40;

bool IsKnownBpf(size_t value) {
    for (int i = 0; i < 4; ++i) if (kBytesPerFrame[i] == value) return true;
    return false;
}

std::string BaseName(const std::string& path) {
    size_t cut = path.find_last_of("/\\");
    std::string file = (cut == std::string::npos) ? path : path.substr(cut + 1);
    size_t dot = file.find_last_of('.');
    if (dot != std::string::npos && dot > 0) file.resize(dot);
    return file;
}

std::string ReadName(const Cursor& block, size_t offset) {
    return GbkToUtf8(block.CStrAt(offset, 256));
}

// Median wie numpy: bei gerader Anzahl das Mittel der beiden
// mittleren Werte. pydump.py rechnet dasselbe - der Frameloeser
// entscheidet nach dieser Zahl, sie muss also auf beiden Seiten
// gleich herauskommen.
double Median(std::vector<double>& values) {
    if (values.empty()) return 1.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

struct FrameCandidate {
    double unitShare = 0.0;
    double deviation = 1.0;
    int    frames = 0;
    size_t bytesPerFrame = 0;
};

// Anteil exakt-normierter Rotationsschluessel und Medianabweichung.
// Stichprobe wie in der Referenz: hoechstens 24 Bones, per
// Schrittweite ausgewaehlt.
void QuaternionScore(const Cursor& block2, const std::vector<size_t>& addrs,
                     int frames, double& unitShare, double& deviation) {
    std::vector<double> deviations;
    const size_t sample = 24;
    const size_t step = std::max<size_t>(1, addrs.size() / sample);
    size_t unitCount = 0, total = 0;

    for (size_t k = 0; k < addrs.size(); k += step) {
        const size_t at = addrs[k] + static_cast<size_t>(frames) * 12;
        if (at + static_cast<size_t>(frames) * 16 > block2.Size()) continue;
        for (int f = 0; f < frames; ++f) {
            const size_t q = at + static_cast<size_t>(f) * 16;
            const double x = block2.F32At(q);
            const double y = block2.F32At(q + 4);
            const double z = block2.F32At(q + 8);
            const double w = block2.F32At(q + 12);
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!std::isfinite(length)) continue;
            const double d = std::fabs(length - 1.0);
            deviations.push_back(d);
            if (d < 1e-3) ++unitCount;
            ++total;
        }
    }
    if (deviations.empty()) { unitShare = 0.0; deviation = 1.0; return; }
    unitShare = static_cast<double>(unitCount) / static_cast<double>(total);
    deviation = Median(deviations);
}

// ------------------------------------------------------------
//  Framezahl bestimmen
//
//  Ein Boneblock ist frames * bytesPerFrame, und mehrere Paare
//  koennen strukturell passen: 063_skin7.x hat Bloecke von 26720
//  Byte, was SOWOHL zu 835 Frames a 32 als AUCH zu 668 a 40
//  passt - und nur das zweite ist echt.
//
//  Die Struktur allein kann das nicht entscheiden, also wird
//  jeder Kandidat GEPRUEFT: die Rotationsspur muss
//  Einheitsquaternionen ergeben. Der falsche Kandidat kommt bei
//  063_skin7.x auf 16,5 Prozent, der richtige auf 100.
//
//  Sortiert wird nach der MEDIANABWEICHUNG, nicht nach dem
//  Anteil exakter Werte: ein richtiger Kandidat hat Median
//  0,000000 auch dann, wenn einzelne Bones verschmutzte
//  Rotationen tragen - der Anteil faellt dabei auf 88-98 Prozent
//  und hat frueher 21 Dateien falschen Alarm ausgeloest.
// ------------------------------------------------------------
// ------------------------------------------------------------
//  Axis-Angle-Rotation, in 8 Byte je Key gepackt
//
//    Bits  0..7   uint8   Drehwinkel IN GRAD
//    Bits 16..27  int12   Achse x, Zweierkomplement, negiert
//    Bits 32..43  int12   Achse y
//    Bits 48..59  int12   Achse z
//    (Bits 8..15, 28..31, 44..47, 60..63 sind feste Kennungen)
//
//    theta = A * pi / 360        (halber Winkel: A/2 Grad)
//    Achse = normiert(-x, -y, -z)
//    q     = (Achse * sin(theta), cos(theta))
//
//  Aus dem Blender-Addon uebernommen, dort gegen die inversen
//  Bindmatrizen von 569 Bones aus 13 Dateien geprueft: mittlerer
//  Winkelfehler 0.467 Grad, 99.1 % unter 1 Grad. Der Rest ist
//  genau die 1-Grad-Quantisierung des 8-Bit-Winkels.
//
//  KONJUGIERT gespeichert - wie alles andere in diesem Format
//  auch, passend zur Zeilenvektor-Konvention.
// ------------------------------------------------------------
Quat DecodePackedRotation(uint64_t word) {
    auto s12 = [](uint64_t v) -> double {
        const int64_t x = static_cast<int64_t>(v & 0xFFF);
        return static_cast<double>(x >= 2048 ? x - 4096 : x);
    };

    const double a = static_cast<double>(word & 0xFF);
    double ax = -s12(word >> 16);
    double ay = -s12(word >> 32);
    double az = -s12(word >> 48);

    const double len = std::sqrt(ax * ax + ay * ay + az * az);
    if (len > 1e-9) { ax /= len; ay /= len; az /= len; }

    const double th = a * (3.14159265358979323846 / 360.0);
    const double sn = std::sin(th);

    Quat q;
    q.x = static_cast<float>(-ax * sn);
    q.y = static_cast<float>(-ay * sn);
    q.z = static_cast<float>(-az * sn);
    q.w = static_cast<float>(std::cos(th));
    return q;
}

int SolveFrames(const Cursor& block2, const std::vector<size_t>& sizes,
                const std::vector<size_t>& addrs, const std::vector<XClip>& clips,
                XFile& out, std::vector<std::string>& warnings) {
    int floorFrames = 0;
    for (size_t i = 0; i < clips.size(); ++i)
        floorFrames = std::max(floorFrames, clips[i].end + 1);

    size_t smallest = sizes.empty() ? 0 : sizes[0];
    for (size_t i = 1; i < sizes.size(); ++i) smallest = std::min(smallest, sizes[i]);

    std::vector<FrameCandidate> candidates;
    for (int b = 0; b < 4; ++b) {
        const size_t bpf = kBytesPerFrame[b];
        if (smallest == 0 || smallest % bpf != 0) continue;
        const size_t frames = smallest / bpf;
        if (frames == 0 || static_cast<int>(frames) < floorFrames) continue;

        bool allFit = true;
        for (size_t i = 0; i < sizes.size(); ++i) {
            if (sizes[i] % frames != 0 || !IsKnownBpf(sizes[i] / frames)) {
                allFit = false;
                break;
            }
        }
        if (!allFit) continue;

        FrameCandidate candidate;
        candidate.frames = static_cast<int>(frames);
        candidate.bytesPerFrame = bpf;
        QuaternionScore(block2, addrs, candidate.frames,
                        candidate.unitShare, candidate.deviation);
        candidates.push_back(candidate);
    }

    if (candidates.empty())
        throw ParseError("Framezahl laesst sich nicht bestimmen "
                         "(Blockgroessen passen zu keiner bekannten Frameaufteilung)");

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FrameCandidate& a, const FrameCandidate& b) {
                         if (a.deviation != b.deviation) return a.deviation < b.deviation;
                         return a.unitShare > b.unitShare;
                     });

    const FrameCandidate& winner = candidates[0];
    out.frameSolveDeviation = winner.deviation;
    out.frameSolveUnitShare = winner.unitShare;
    for (size_t i = 1; i < candidates.size(); ++i)
        out.rejectedFrameCounts.push_back(candidates[i].frames);

    if (winner.deviation > 0.005)
        warnings.push_back("beste Framezahl " + std::to_string(winner.frames) +
                           " hat noch eine Medianabweichung von " +
                           FormatFloat(winner.deviation) +
                           " - die Boneblock-Aufteilung passt fuer diese Datei nicht");
    return winner.frames;
}

// ------------------------------------------------------------
//  NaN und nicht normierte Rotationen
//
//  Beides ist Quelldatenschrott, kein Dekodierfehler:
//  156_skin10.x hat einen von 128 Bones, dessen Position und
//  Rotation ueber 37 aufeinanderfolgende Frames NaN sind,
//  waehrend jedes andere Quaternion derselben Datei exakt
//  Einheitslaenge hat. Ein einziges NaN zieht ueber das Skinning
//  das ganze Mesh mit.
// ------------------------------------------------------------
void SanitiseTracks(XFile& out, std::vector<std::string>& warnings) {
    for (size_t k = 0; k < out.bones.size(); ++k) {
        XBone& bone = out.bones[k];
        const size_t frames = bone.pos.size();
        if (frames == 0) continue;

        std::vector<char> bad(frames, 0);
        for (size_t f = 0; f < frames; ++f) {
            bool broken = !std::isfinite(bone.pos[f].x) || !std::isfinite(bone.pos[f].y) ||
                          !std::isfinite(bone.pos[f].z) ||
                          !std::isfinite(bone.quat[f].x) || !std::isfinite(bone.quat[f].y) ||
                          !std::isfinite(bone.quat[f].z) || !std::isfinite(bone.quat[f].w);
            if (!broken && bone.HasScale())
                broken = !std::isfinite(bone.scale[f].x) ||
                         !std::isfinite(bone.scale[f].y) ||
                         !std::isfinite(bone.scale[f].z);
            bad[f] = broken ? 1 : 0;
        }

        std::vector<size_t> good;
        for (size_t f = 0; f < frames; ++f) if (!bad[f]) good.push_back(f);

        size_t badCount = frames - good.size();
        if (badCount == 0) continue;

        ++out.nanBones;
        out.nanFrames += static_cast<int>(badCount);

        if (good.empty()) {
            // Jeder Frame kaputt - 144_skin8_huicheng.model hat so
            // einen Bone. Es gibt keinen gueltigen Frame zum
            // Kopieren, also Identitaet statt NaN; sonst kollabiert
            // jeder daran gewichtete Vertex.
            for (size_t f = 0; f < frames; ++f) {
                bone.pos[f] = Vec3();
                bone.quat[f] = Quat();
                if (bone.HasScale()) { bone.scale[f].x = bone.scale[f].y = bone.scale[f].z = 1.0f; }
            }
            continue;
        }

        // Ersatzframe: der erste gueltige mit Index >= f, sonst
        // der letzte gueltige. Entspricht searchsorted + clip in
        // der Referenz.
        const std::vector<Vec3> posCopy = bone.pos;
        const std::vector<Quat> quatCopy = bone.quat;
        const std::vector<Vec3> scaleCopy = bone.scale;
        for (size_t f = 0; f < frames; ++f) {
            if (!bad[f]) continue;
            size_t slot = static_cast<size_t>(
                std::lower_bound(good.begin(), good.end(), f) - good.begin());
            if (slot >= good.size()) slot = good.size() - 1;
            const size_t src = good[slot];
            bone.pos[f] = posCopy[src];
            bone.quat[f] = quatCopy[src];
            if (bone.HasScale()) bone.scale[f] = scaleCopy[src];
        }
    }

    if (out.nanBones > 0)
        warnings.push_back(std::to_string(out.nanBones) +
                           " Bone(s) mit NaN in " + std::to_string(out.nanFrames) +
                           " Frames (beschaedigte Quelldaten) - durch den naechsten "
                           "gueltigen Frame ersetzt");

    // Nicht normierte Rotationen: 156_skin10.x hat 7 von 128
    // Bones mit Laengen bis herunter zu exakt 0,5. Der ganze Track
    // wird normiert, nicht nur die auffaelligen Frames - so macht
    // es die Referenz auch.
    for (size_t k = 0; k < out.bones.size(); ++k) {
        XBone& bone = out.bones[k];
        bool anyBad = false;
        for (size_t f = 0; f < bone.quat.size(); ++f) {
            const double x = bone.quat[f].x, y = bone.quat[f].y;
            const double z = bone.quat[f].z, w = bone.quat[f].w;
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (std::fabs(length - 1.0) > 1e-3) { anyBad = true; ++out.denormKeys; }
        }
        if (!anyBad) continue;
        ++out.denormBones;
        for (size_t f = 0; f < bone.quat.size(); ++f) {
            const double x = bone.quat[f].x, y = bone.quat[f].y;
            const double z = bone.quat[f].z, w = bone.quat[f].w;
            const double inv = 1.0 / std::max(std::sqrt(x * x + y * y + z * z + w * w), 1e-9);
            bone.quat[f].x = static_cast<float>(x * inv);
            bone.quat[f].y = static_cast<float>(y * inv);
            bone.quat[f].z = static_cast<float>(z * inv);
            bone.quat[f].w = static_cast<float>(w * inv);
        }
    }
    if (out.denormBones > 0)
        warnings.push_back(std::to_string(out.denormBones) +
                           " Bone(s) mit nicht normierten Rotationen in " +
                           std::to_string(out.denormKeys) + " Frames - normiert");
}

bool WouldCycle(const XFile& file, int parent, int child) {
    int p = parent;
    int guard = 0;
    while (p >= 0 && guard < 512) {
        if (p == child) return true;
        p = file.bones[static_cast<size_t>(p)].parent;
        ++guard;
    }
    return false;
}

// ------------------------------------------------------------
//  Die Namenstabelle IST die Hierarchie.
//
//  Jeder Eintrag ist [name NUL][u32 Kind-Index]*. Ein Kind-Index
//  ist daran erkennbar, dass die Bytes 2-4 des Wortes null sind -
//  das kann kein ASCII-Name erzeugen.
//
//  Vier Nullbytes sind allerdings nicht von "Kind 0" zu
//  unterscheiden. Sie als Fuellung zu behandeln ist gefahrlos,
//  weil Index 0 der alphabetisch erste Name und damit die Wurzel
//  ist - eine Wurzel als Kind einzuhaengen waere ohnehin falsch.
// ------------------------------------------------------------
void ParseHierarchy(const Cursor& block1, XFile& out) {
    const uint32_t abon = out.tags.count("abon") ? out.tags["abon"] : 0;
    const size_t count = out.bones.size();

    for (size_t k = 0; k < count; ++k) {
        const size_t base = abon + k * kXBoneStride;
        if (base + 12 > block1.Size()) continue;
        const size_t nameOffset = block1.U32At(base + 8);
        if (nameOffset >= block1.Size()) continue;

        // Hinter der NUL des Namens beginnen die Kindindices.
        size_t p = nameOffset;
        while (p < block1.Size() && block1.Raw()[p] != 0) ++p;
        if (p >= block1.Size()) continue;
        ++p;

        while (p + 4 <= block1.Size()) {
            const uint8_t b0 = block1.Raw()[p];
            const uint8_t b1 = block1.Raw()[p + 1];
            const uint8_t b2 = block1.Raw()[p + 2];
            const uint8_t b3 = block1.Raw()[p + 3];
            if (b1 || b2 || b3) break;          // sieht nach Text aus, nicht nach Index
            const int child = b0;
            if (static_cast<size_t>(child) >= count) break;
            if (child == 0) break;              // Fuellung, siehe oben
            if (WouldCycle(out, static_cast<int>(k), child)) break;

            out.bones[k].children.push_back(child);
            if (out.bones[static_cast<size_t>(child)].parent < 0) {
                out.bones[static_cast<size_t>(child)].parent = static_cast<int>(k);
                ++out.hierarchyLinks;
            }
            p += 4;
        }
    }
}

} // namespace

// ============================================================
//  Oeffentliche Hilfen
// ============================================================

int XFile::VertexCount() const {
    int total = 0;
    for (size_t i = 0; i < meshes.size(); ++i) total += meshes[i].vertexCount;
    return total;
}

int XFile::TriangleCount() const {
    int total = 0;
    for (size_t i = 0; i < meshes.size(); ++i)
        total += static_cast<int>(meshes[i].indices.size() / 3);
    return total;
}

bool LooksLikeJumpx(const uint8_t* data, size_t size) {
    return data && size >= 5 && std::memcmp(data, "JUMPX", 5) == 0;
}

Matrix44 XBoneWorld(const XBone& bone, int frame) {
    if (frame < 0) frame = 0;
    if (bone.pos.empty()) return Matrix44();
    if (static_cast<size_t>(frame) >= bone.pos.size())
        frame = static_cast<int>(bone.pos.size()) - 1;

    // HIER die Konjugation - an genau einer Stelle. Das Format
    // speichert die transponierte Rotation, passend zu seinen
    // Zeilenvektor-Matrizen.
    const Quat& stored = bone.quat[static_cast<size_t>(frame)];
    Quat q;
    q.x = -stored.x;
    q.y = -stored.y;
    q.z = -stored.z;
    q.w =  stored.w;

    Vec3 scale;
    scale.x = scale.y = scale.z = 1.0f;
    if (bone.HasScale()) scale = bone.scale[static_cast<size_t>(frame)];

    return MatFromQuatPosScale(q, bone.pos[static_cast<size_t>(frame)], scale);
}

// ============================================================
//  Einstieg
// ============================================================

ReadResult ReadXBuffer(const uint8_t* data, size_t size,
                       const std::string& source, XFile& out) {
    ReadResult result;
    out = XFile();
    out.path = source;
    out.name = BaseName(source);

    std::vector<uint8_t> block1Data, block2Data;

    try {
        if (!LooksLikeJumpx(data, size))
            throw ParseError("keine JUMPX-Datei (Signatur passt nicht)");

        Cursor head(data, size, "JUMPX");
        out.fileSize = size;
        // Ueber GbkToUtf8: die Werbetexte hinter "JUMPX V5.01" sind
        // chinesisch. Ohne Umwandlung landen rohe GBK-Bytes im
        // Dump, und der ist dann kein gueltiges UTF-8 mehr.
        // (Fund aus dem ersten Vergleich gegen die Python-Referenz.)
        out.signature = GbkToUtf8(head.CStrAt(0, 64));

        // ---- Inhaltsverzeichnis ----
        size_t off = 0x58;
        while (off + 12 <= size) {
            bool printable = true;
            for (int i = 0; i < 4; ++i) {
                const uint8_t c = data[off + static_cast<size_t>(i)];
                if (c < 32 || c >= 127) { printable = false; break; }
            }
            if (!printable) break;
            if (head.U32At(off + 4) != 4) break;
            out.tags[std::string(reinterpret_cast<const char*>(data + off), 4)] =
                head.U32At(off + 8);
            off += 12;
        }
        if (out.tags.empty())
            throw ParseError("kein lesbares Inhaltsverzeichnis bei 0x58");

        const uint32_t raw1 = head.U32At(off);
        const uint32_t raw2 = head.U32At(off + 4);
        off += 16;   // roh1, roh2, gepackt1, gepackt2

        InflateResult first = ZlibInflate(data + off, size - off, raw1, block1Data);
        if (!first.ok) throw ParseError("Block 1 laesst sich nicht entpacken: " + first.error);
        const size_t secondAt = off + first.consumed;
        if (secondAt >= size) throw ParseError("hinter Block 1 folgt kein zweiter Stream");
        InflateResult second = ZlibInflate(data + secondAt, size - secondAt, raw2, block2Data);
        if (!second.ok) throw ParseError("Block 2 laesst sich nicht entpacken: " + second.error);

        if (block1Data.size() != raw1 || block2Data.size() != raw2)
            result.warnings.push_back("entpackte Groessen weichen vom Kopf ab (" +
                                      std::to_string(block1Data.size()) + "/" +
                                      std::to_string(block2Data.size()) + " statt " +
                                      std::to_string(raw1) + "/" + std::to_string(raw2) + ")");

        out.block1Size = block1Data.size();
        out.block2Size = block2Data.size();

        Cursor block1(block1Data.data(), block1Data.size(), "JUMPX Block 1");
        Cursor block2(block2Data.data(), block2Data.size(), "JUMPX Block 2");

        const uint32_t nbon = out.tags.count("nbon") ? out.tags["nbon"] : 0;
        const uint32_t ngeo = out.tags.count("ngeo") ? out.tags["ngeo"] : 0;
        const uint32_t nact = out.tags.count("nact") ? out.tags["nact"] : 0;
        const uint32_t ntex = out.tags.count("ntex") ? out.tags["ntex"] : 0;
        const uint32_t nmtl = out.tags.count("nmtl") ? out.tags["nmtl"] : 0;
        const uint32_t abon = out.tags.count("abon") ? out.tags["abon"] : 0;
        const uint32_t aact = out.tags.count("aact") ? out.tags["aact"] : 0;
        const uint32_t atex = out.tags.count("atex") ? out.tags["atex"] : 0;
        const uint32_t amtl = out.tags.count("amtl") ? out.tags["amtl"] : 0;
        const uint32_t ageo = out.tags.count("ageo") ? out.tags["ageo"] : 0;

        // ---- Clips zuerst: sie begrenzen die Zeitleiste ----
        for (uint32_t k = 0; k < nact; ++k) {
            const size_t base = aact + static_cast<size_t>(k) * kXActStride;
            if (base + kXActStride > block1Data.size()) break;
            XClip clip;
            clip.name = ReadName(block1, base);
            if (clip.name.empty()) clip.name = "clip_" + std::to_string(k);
            clip.start = block1.U16At(base + 80);
            clip.end = block1.U16At(base + 82);
            out.clips.push_back(clip);
        }

        // ---- Boneblöcke ----
        //
        // Die Adresse des Animationsblocks steht NICHT immer bei
        // +144. Die Dateien aus dem Monster-Bestand haben sie bei
        // +148 - dort ist die ganze Tracktabelle um vier Bytes
        // verschoben.
        //
        // 21 von 51 .x-Dateien scheiterten deshalb mit
        // "unplausible Blockadresse". Statt den Offset fest
        // anzunehmen, wird der gesucht, der fuer JEDEN Bone eine
        // gueltige Adresse ergibt - so macht es auch das
        // Blender-Addon, das dieselben Dateien liest.
        //
        // Gesucht wird ab 120, weil davor die Matrixdaten des
        // Deskriptors stehen; der erste Treffer gewinnt.
        size_t addrOffset = 0;
        bool haveOffset = false;
        for (size_t cand = 120; cand + 4 <= kXBoneStride; ++cand) {
            bool good = true;
            for (uint32_t k = 0; k < nbon && good; ++k) {
                const size_t q =
                    abon + static_cast<size_t>(k) * kXBoneStride + cand;
                if (q + 4 > block1Data.size()) { good = false; break; }
                const int64_t a =
                    static_cast<int64_t>(block1.U32At(q)) - kXAddrBase;
                if (a < 0 || static_cast<size_t>(a) >= block2Data.size())
                    good = false;
            }
            if (good) { addrOffset = cand; haveOffset = true; break; }
        }

        if (!haveOffset)
            throw ParseError("kein Offset im Bone-Deskriptor liefert fuer alle "
                             + std::to_string(nbon) +
                             " Bones eine gueltige Blockadresse");

        std::vector<size_t> addrs;
        std::vector<int64_t> addrs2;   // -1 = keine zweite Adresse
        addrs.reserve(nbon);
        addrs2.reserve(nbon);
        for (uint32_t k = 0; k < nbon; ++k) {
            const size_t base = abon + static_cast<size_t>(k) * kXBoneStride;
            if (base + addrOffset + 4 > block1Data.size()) break;
            const int64_t address =
                static_cast<int64_t>(block1.U32At(base + addrOffset)) - kXAddrBase;
            if (address < 0 || static_cast<size_t>(address) >= block2Data.size())
                throw ParseError("Bone " + std::to_string(k) +
                                 " hat eine unplausible Blockadresse " +
                                 std::to_string(address));
            addrs.push_back(static_cast<size_t>(address));

            // ---- ZWEITE Adressspalte, 12 Bytes weiter ----
            //
            // Je Bone stehen ZWEI Trackadressen im Deskriptor:
            // erst die Position, zwoelf Bytes spaeter die
            // Rotation. Ihr Abstand ist die Groesse der
            // Positionsspur und gibt die Framezahl direkt:
            //
            //     A2 - A1 = frames * 12  ->  float32-Positionen
            //     A2 - A1 = frames *  4  ->  10:10:10 gepackt
            //
            // Das ersetzt das Raten aus den Blockgroessen, das
            // beim Monster-Bestand gar nicht funktionieren kann,
            // weil die Bloecke dort nicht lueckenlos liegen.
            int64_t rotAddress = -1;
            if (base + addrOffset + 16 <= block1Data.size()) {
                const int64_t a2 =
                    static_cast<int64_t>(block1.U32At(base + addrOffset + 12)) -
                    kXAddrBase;
                if (a2 >= 0 && static_cast<size_t>(a2) < block2Data.size())
                    rotAddress = a2;
            }
            addrs2.push_back(rotAddress);
        }
        if (addrs.size() != nbon)
            throw ParseError("nur " + std::to_string(addrs.size()) + " von " +
                             std::to_string(nbon) + " Bone-Deskriptoren lesbar");

        // Blockgroessen ergeben sich aus dem Abstand zum naechsten
        // Block in ADRESSREIHENFOLGE, nicht in Bonereihenfolge.
        std::vector<size_t> order(addrs.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return addrs[a] < addrs[b]; });

        std::vector<size_t> sizes(addrs.size(), 0);
        for (size_t j = 0; j < order.size(); ++j) {
            const size_t next = (j + 1 < order.size()) ? addrs[order[j + 1]]
                                                       : block2Data.size();
            if (next <= addrs[order[j]]) throw ParseError("Bonebloecke ueberlappen");
            sizes[order[j]] = next - addrs[order[j]];
        }

        // ------------------------------------------------------------
        //  Framezahl aus dem Abstand der beiden Trackadressen
        //
        //  Ist er fuer alle Bones gleich, ergibt sich die Framezahl
        //  direkt - und mit ihr, ob die Positionen als float32
        //  (12 Byte/Key) oder 10:10:10 gepackt (4 Byte/Key)
        //  vorliegen. Nur wenn das nicht traegt, wird wie bisher
        //  aus den Blockgroessen geraten.
        // ------------------------------------------------------------
        bool packedPos = false;
        bool packedRotation = false;
        bool packedScale = false;
        out.frames = 0;

        int64_t delta = -1;
        bool sameDelta = true;
        for (size_t k = 0; k < addrs.size() && sameDelta; ++k) {
            if (addrs2[k] < 0) { sameDelta = false; break; }
            const int64_t d = addrs2[k] - static_cast<int64_t>(addrs[k]);
            if (d <= 0) { sameDelta = false; break; }
            if (delta < 0) delta = d;
            else if (d != delta) sameDelta = false;
        }

        if (sameDelta && delta > 0) {
            if (delta % 12 == 0) {
                out.frames = static_cast<int>(delta / 12);
            } else if (delta % 4 == 0) {
                // Positionen gepackt. Die Rotation ist dann
                // entweder float32-Quaternion oder Axis-Angle -
                // das entscheidet die Groesse der Rotationsspur.
                const int64_t f = delta / 4;
                const int64_t smallest =
                    sizes.empty()
                        ? -1
                        : static_cast<int64_t>(
                              *std::min_element(sizes.begin(), sizes.end()));

                const int64_t rotBytes = smallest - delta;
                if (f > 0 && (rotBytes == f * 8 || rotBytes == f * 20)) {
                    out.frames = static_cast<int>(f);
                    packedPos = true;
                    packedRotation = true;
                    packedScale = (rotBytes == f * 20);
                    result.warnings.push_back(
                        "komprimierte Variante: Positionen 10:10:10, "
                        "Rotationen Axis-Angle" +
                        std::string(packedScale ? ", dazu eine Skalierungsspur"
                                                : ""));
                } else if (f > 0 && rotBytes == f * 16) {
                    out.frames = static_cast<int>(f);
                    packedPos = true;
                }
            }
        }

        // ------------------------------------------------------------
        //  Clips, die ueber die Zeitleiste hinausreichen, sind ein
        //  Fehler IN DER DATEI - kein Dekodierfehler.
        //
        //  Das Blender-Addon haelt das ausdruecklich fest:
        //  "Some files declare clips beyond their own timeline [...]
        //   That is an authoring error in the file, not a decoding
        //   problem, so clamp instead of refusing."
        //
        //  monster_youlong.x hat 53 Frames und einen 'dead'-Clip bis
        //  156. Ich hatte daraus geschlossen, die Framezahl sei
        //  falsch, und sie aus den Clips neu bestimmt - das war die
        //  falsche Richtung: die Framezahl stimmt, die Clipgrenzen
        //  nicht.
        //
        //  Geklemmt wird weiter unten, wo die Clips gelesen sind.
        // ------------------------------------------------------------

        if (out.frames <= 0)
            out.frames = SolveFrames(block2, sizes, addrs, out.clips, out,
                                     result.warnings);
        const size_t frames = static_cast<size_t>(out.frames);

        // ------------------------------------------------------------
        //  Die Rotationsvariante MESSEN, nicht aus Groessen schliessen
        //
        //  Ein Quaternion hat Laenge 1. Liest man Axis-Angle-Bytes als
        //  float32, kommen Werte nahe null heraus - bei 11 der 21
        //  Monster-Dateien war der Mittelwert 0.02 statt 1.00.
        //
        //  Die Blockgroessen taugen zur Unterscheidung nicht: sie sind
        //  bei diesen Dateien nicht lueckenlos, und rotBytes ergibt
        //  sich dann aus dem Abstand zum naechsten Block, nicht aus
        //  der tatsaechlichen Spur.
        //
        //  Also wird eine Stichprobe beider Deutungen gelesen und die
        //  genommen, deren Quaternionen Einheitslaenge haben. So macht
        //  es auch das Blender-Addon (_unit_quaternion_score).
        // ------------------------------------------------------------
        if (frames > 0 && !addrs.empty()) {
            auto meanNorm = [&](bool asPacked) -> double {
                double sum = 0.0;
                int n = 0;
                const size_t stride = std::max<size_t>(1, addrs.size() / 8);
                for (size_t k = 0; k < addrs.size(); k += stride) {
                    const size_t rotAt =
                        (addrs2[k] >= 0)
                            ? static_cast<size_t>(addrs2[k])
                            : addrs[k] + frames * (packedPos ? 4 : 12);
                    const size_t take = std::min<size_t>(frames, 8);
                    for (size_t f = 0; f < take; ++f) {
                        Quat q;
                        if (asPacked) {
                            const size_t r = rotAt + f * 8;
                            if (r + 8 > block2Data.size()) continue;
                            const uint64_t w =
                                static_cast<uint64_t>(block2.U32At(r)) |
                                (static_cast<uint64_t>(block2.U32At(r + 4)) << 32);
                            q = DecodePackedRotation(w);
                        } else {
                            const size_t r = rotAt + f * 16;
                            if (r + 16 > block2Data.size()) continue;
                            q.x = block2.F32At(r);
                            q.y = block2.F32At(r + 4);
                            q.z = block2.F32At(r + 8);
                            q.w = block2.F32At(r + 12);
                        }
                        const double len = std::sqrt(
                            static_cast<double>(q.x) * q.x +
                            static_cast<double>(q.y) * q.y +
                            static_cast<double>(q.z) * q.z +
                            static_cast<double>(q.w) * q.w);
                        if (std::isfinite(len)) { sum += len; ++n; }
                    }
                }
                return n > 0 ? sum / n : 0.0;
            };

            const double asFloat = meanNorm(false);
            const double asPacked = meanNorm(true);
            const bool floatOk = std::fabs(asFloat - 1.0) < 0.02;
            const bool packedOk = std::fabs(asPacked - 1.0) < 0.02;

            if (!floatOk && packedOk && !packedRotation) {
                packedRotation = true;

                // ------------------------------------------------
                //  Wenn die ROTATION gepackt ist, ist es die
                //  POSITION meist auch.
                //
                //  monster_youlong.x: die Messung erkannte
                //  Axis-Angle, packedPos blieb aber aus - die
                //  Positionen wurden weiter als float32 gelesen
                //  und kamen alle als exakt 0.0 heraus. Im
                //  Viewport sassen dann alle 58 Bones im Ursprung,
                //  in einem Wuerfel von 1.87 Einheiten, waehrend
                //  das Mesh 240 Einheiten hoch ist. Genau das
                //  sieht man als "winzige Bones".
                //
                //  Geprueft wird an der Spanne: stehen alle
                //  float32-Positionen auf demselben Punkt, koennen
                //  sie nicht stimmen.
                // ------------------------------------------------
                if (!packedPos) {
                    double lo = 0.0, hi = 0.0;
                    bool empty = true;
                    const size_t stride = std::max<size_t>(1, addrs.size() / 8);
                    for (size_t k = 0; k < addrs.size(); k += stride) {
                        const size_t take = std::min<size_t>(frames, 8);
                        for (size_t f = 0; f < take; ++f) {
                            const size_t p = addrs[k] + f * 12;
                            if (p + 12 > block2Data.size()) continue;
                            for (int c = 0; c < 3; ++c) {
                                const double v = block2.F32At(p + c * 4);
                                if (!std::isfinite(v)) continue;
                                if (empty) { lo = hi = v; empty = false; }
                                lo = std::min(lo, v);
                                hi = std::max(hi, v);
                            }
                        }
                    }
                    // Nicht auf "exakt gleich" pruefen, sondern auf
                    // "viel zu klein". Bei jiutoulong.x spannen die
                    // float32-Positionen 0.10 Einheiten, das Mesh
                    // 661 - sie sind nicht identisch, aber genauso
                    // unbrauchbar.
                    //
                    // Die Meshausdehnung ist an dieser Stelle noch
                    // nicht bekannt, deshalb der Vergleich mit der
                    // Bindmatrix-Translation: die steht im
                    // Deskriptor und ist immer im Modellmassstab.
                    double refSpan = 0.0;
                    for (uint32_t k = 0; k < nbon; ++k) {
                        const size_t bb = abon + static_cast<size_t>(k) * kXBoneStride;
                        if (bb + 36 > block1Data.size()) break;
                        // Die inverse Bindmatrix beginnt bei +24. Die
                        // TRANSLATION steht in ihrer vierten Zeile,
                        // also bei +24+48 - nicht am Matrixanfang.
                        // Dort las ich vorher die erste Basiszeile,
                        // deren Betraege bei etwa 1 liegen, und die
                        // Pruefung ging bei monster_xiaopaobing_01.x
                        // ins Leere.
                        for (int c = 0; c < 3; ++c) {
                            const double v = std::fabs(block1.F32At(bb + 72 + c * 4));
                            if (std::isfinite(v)) refSpan = std::max(refSpan, v);
                        }
                    }

                    const double span = empty ? 0.0 : (hi - lo);
                    const bool tooSmall =
                        (refSpan > 1e-3) ? (span < refSpan * 0.05) : (span < 1e-6);

                    if (tooSmall) {
                        packedPos = true;
                        result.warnings.push_back(
                            "Positionen ebenfalls gepackt gelesen: als float32 "
                            "spannen sie nur " + std::to_string(span) +
                            " gegen " + std::to_string(refSpan) +
                            " im Deskriptor");
                    }
                }

                result.warnings.push_back(
                    "Rotationen als Axis-Angle erkannt (float32 ergab "
                    "Quaternionen der Laenge " +
                    std::to_string(asFloat) + " statt 1.0)");
            } else if (floatOk && !packedOk && packedRotation) {
                packedRotation = false;
                packedScale = false;
            }
        }

        out.bones.resize(nbon);
        // Bonespuren sind gross (bis 8,6 MB Block 2) und voellig
        // unabhaengig voneinander - der klassische Fall fuer
        // ParallelFor. Kein SDK-Zugriff, kein gemeinsamer Zustand.
        ParallelFor(out.bones.size(), [&](size_t k) {
            XBone& bone = out.bones[k];
            bone.index = static_cast<int>(k);
            const size_t base = abon + k * kXBoneStride;
            std::string name = ReadName(block1, block1.U32At(base + 8));
            // Ein Name muss mit einem Buchstaben anfangen; sonst
            // hat der Offset ins Leere gezeigt.
            const bool usable = !name.empty() &&
                                ((name[0] >= 'A' && name[0] <= 'Z') ||
                                 (name[0] >= 'a' && name[0] <= 'z') ||
                                 static_cast<unsigned char>(name[0]) >= 0x80);
            char fallback[16];
            std::snprintf(fallback, sizeof fallback, "Bone_%02d", static_cast<int>(k));
            bone.name = usable ? name : std::string(fallback);

            const size_t at = addrs[k];
            // Bei gepackten Bloecken sagt die Bytezahl je Frame
            // nichts ueber den Inhalt - die Spuren liegen dort
            // nicht hintereinander im selben Block.
            bone.bytesPerFrame = packedPos ? 0 : sizes[k] / frames;

            bone.pos.resize(frames);
            bone.quat.resize(frames);

            // ---- Positionen ----
            //
            // Gepackt: drei 10-Bit-Werte in einem uint32, als
            // Anteil in einer Box je Bone. Die beiden Eckpunkte
            // stehen 60 und 48 Byte VOR der Adressspalte - nicht
            // 24 und 12, wie man vermuten wuerde.
            if (packedPos) {
                const size_t bk = abon + static_cast<size_t>(k) * kXBoneStride +
                                  addrOffset;
                Vec3 hi{}, lo{};
                if (bk >= 60) {
                    hi.x = block1.F32At(bk - 60);
                    hi.y = block1.F32At(bk - 56);
                    hi.z = block1.F32At(bk - 52);
                    lo.x = block1.F32At(bk - 48);
                    lo.y = block1.F32At(bk - 44);
                    lo.z = block1.F32At(bk - 40);
                }
                for (size_t f = 0; f < frames; ++f) {
                    const uint32_t packed = block2.U32At(at + f * 4);
                    const double qx = static_cast<double>(packed & 1023) / 1023.0;
                    const double qy =
                        static_cast<double>((packed >> 10) & 1023) / 1023.0;
                    const double qz =
                        static_cast<double>((packed >> 20) & 1023) / 1023.0;
                    bone.pos[f].x = static_cast<float>(lo.x + qx * (hi.x - lo.x));
                    bone.pos[f].y = static_cast<float>(lo.y + qy * (hi.y - lo.y));
                    bone.pos[f].z = static_cast<float>(lo.z + qz * (hi.z - lo.z));
                }
            } else {
                for (size_t f = 0; f < frames; ++f) {
                    const size_t p = at + f * 12;
                    bone.pos[f].x = block2.F32At(p);
                    bone.pos[f].y = block2.F32At(p + 4);
                    bone.pos[f].z = block2.F32At(p + 8);
                }
            }

            // ---- Rotationen ----
            const size_t rotAt =
                (addrs2[k] >= 0) ? static_cast<size_t>(addrs2[k])
                                 : at + frames * (packedPos ? 4 : 12);

            if (packedRotation) {
                for (size_t f = 0; f < frames; ++f) {
                    const size_t r = rotAt + f * 8;
                    const uint64_t word =
                        static_cast<uint64_t>(block2.U32At(r)) |
                        (static_cast<uint64_t>(block2.U32At(r + 4)) << 32);
                    bone.quat[f] = DecodePackedRotation(word);
                }
            } else {
                for (size_t f = 0; f < frames; ++f) {
                    const size_t q = rotAt + f * 16;
                    bone.quat[f].x = block2.F32At(q);
                    bone.quat[f].y = block2.F32At(q + 4);
                    bone.quat[f].z = block2.F32At(q + 8);
                    bone.quat[f].w = block2.F32At(q + 12);
                }
            }

            // Skalierungsspur der komprimierten Variante.
            if (packedScale) {
                bone.scale.resize(frames);
                for (size_t f = 0; f < frames; ++f) {
                    const size_t sc = rotAt + frames * 8 + f * 12;
                    bone.scale[f].x = block2.F32At(sc);
                    bone.scale[f].y = block2.F32At(sc + 4);
                    bone.scale[f].z = block2.F32At(sc + 8);
                }
            }
            if (!packedScale && bone.bytesPerFrame >= kScaleMinBpf) {
                bone.scale.resize(frames);
                for (size_t f = 0; f < frames; ++f) {
                    const size_t s = at + frames * 28 + f * 12;
                    bone.scale[f].x = block2.F32At(s);
                    bone.scale[f].y = block2.F32At(s + 4);
                    bone.scale[f].z = block2.F32At(s + 8);
                }
            }
            if (bone.bytesPerFrame == 32 || bone.bytesPerFrame == 44) {
                // Die vier Byte je Frame sind ein uint32, der nur 0
                // oder 1 annimmt: ein SICHTBARKEITSSCHALTER fuer den
                // Bone und alles, was daran haengt. Belegt ueber die
                // Clip-Korrelation - in 045.x ist Bone17 exakt
                // waehrend single_skill_04_a/_b an. Die betroffenen
                // Bones sind durchweg Effekt- und Requisiten-
                // aufhaengungen (_tx, _fx, _zhibao, Dummy01).
                const size_t flagBase =
                    at + frames * (bone.bytesPerFrame == 32 ? 28 : 40);
                bone.visible.resize(frames);
                for (size_t f = 0; f < frames; ++f)
                    bone.visible[f] = block2.U32At(flagBase + f * 4) ? 1 : 0;
            }
        });

        SanitiseTracks(out, result.warnings);
        ParseHierarchy(block1, out);

        // ---- Texturen ----
        // Das ist eine TABELLE aus [u32 flags][u32 nameOffset], die
        // Namen liegen dahinter. Den Namen direkt hinter dem
        // Eintrag zu lesen geht nur zufaellig, solange es genau
        // eine Textur gibt.
        for (uint32_t i = 0; i < ntex; ++i) {
            const size_t base = atex + static_cast<size_t>(i) * kXTexStride;
            if (base + 8 > block1Data.size()) break;
            const std::string name = ReadName(block1, block1.U32At(base + 4));
            if (!name.empty()) out.textures.push_back(name);
            else result.warnings.push_back("Textur " + std::to_string(i) +
                                           ": Namensoffset nicht lesbar");
        }

        // ---- Materialien ----
        for (uint32_t i = 0; i < nmtl; ++i) {
            const size_t base = amtl + static_cast<size_t>(i) * kXMtlStride;
            if (base + kXMtlStride > block1Data.size()) break;
            XMaterial material;
            const uint32_t tex = block1.U32At(base + 12);
            material.texture = (tex < out.textures.size()) ? static_cast<int>(tex) : -1;
            out.materials.push_back(material);
        }

        // ---- Meshes ----
        for (uint32_t g = 0; g < ngeo; ++g) {
            const size_t base = ageo + static_cast<size_t>(g) * kXGeoStride;
            if (base + kXGeoStride > block1Data.size()) break;

            const size_t nameOffset = block1.U32At(base + 8);
            const uint32_t material = block1.U32At(base + 16);
            const uint32_t vc = block1.U32At(base + 28);
            const uint32_t tc = block1.U32At(base + 32);

            // Jeder Puffer hat seine EIGENE Adresse. Siehe Falle 1
            // im Header.
            auto address = [&](size_t field) -> int64_t {
                const int64_t value =
                    static_cast<int64_t>(block1.U32At(base + field)) - kXAddrBase;
                return (value >= 0 && static_cast<size_t>(value) < block2Data.size())
                           ? value : -1;
            };

            int64_t aPos = address(36), aNrm = address(44), aUv = address(52);
            int64_t aByte = address(68), aIdx = address(76), aSkin = address(92);

            // ------------------------------------------------------
            //  AELTERE ANORDNUNG (23 TOC-Eintraege statt 25)
            //
            //  Dort sind die Positionen auf EINEN uint32 je Vertex
            //  quantisiert - 10:10:10 als Anteil in einer Box, die
            //  im Deskriptor bei +96 (hi) und +108 (lo) steht.
            //  Die Normalen liegen als int8x3 in drei Bytes statt
            //  vier.
            //
            //  Erkannt daran, dass bei +36 KEINE Positionsadresse
            //  steht, dafuer aber eine bei +40. 068.x und 083_b.x
            //  sind so gebaut; ohne diesen Zweig meldet der Parser
            //  "keine brauchbare Positionsadresse" und die Datei
            //  kommt ohne Mesh an.
            // ------------------------------------------------------
            bool quantised = false;
            int64_t aQPos = -1, aNrm3 = -1;
            Vec3 qLo{}, qHi{};

            if (aPos < 0 && address(40) >= 0) {
                aQPos = address(40);
                aNrm3 = address(48);
                qHi.x = block1.F32At(base + 96);
                qHi.y = block1.F32At(base + 100);
                qHi.z = block1.F32At(base + 104);
                qLo.x = block1.F32At(base + 108);
                qLo.y = block1.F32At(base + 112);
                qLo.z = block1.F32At(base + 116);

                const bool finite =
                    std::isfinite(qHi.x) && std::isfinite(qHi.y) &&
                    std::isfinite(qHi.z) && std::isfinite(qLo.x) &&
                    std::isfinite(qLo.y) && std::isfinite(qLo.z);
                const bool spans =
                    (qHi.x > qLo.x) || (qHi.y > qLo.y) || (qHi.z > qLo.z);
                quantised = finite && spans && aQPos >= 0;

                if (quantised)
                    result.warnings.push_back(
                        "Mesh " + std::to_string(g) +
                        ": aeltere Anordnung, Positionen 10:10:10 in einer Box");
            }

            if (aPos < 0 && !quantised) {
                if (vc == 0 || tc == 0)
                    result.warnings.push_back("Mesh " + std::to_string(g) +
                                              " ist leer, uebersprungen");
                else
                    result.warnings.push_back("Mesh " + std::to_string(g) +
                                              " hat keine brauchbare Positionsadresse, "
                                              "uebersprungen");
                continue;
            }
            // Notweg: dicht gepackte Anordnung. Greift nur, wenn
            // ein Feld fehlt - und nicht bei der quantisierten
            // Form, wo die Positionen nur 4 Byte je Vertex belegen.
            if (!quantised && aNrm < 0)
                aNrm = aPos + static_cast<int64_t>(vc) * 12;
            if (aUv   < 0) aUv   = aNrm + static_cast<int64_t>(vc) * 12;
            if (aByte < 0) aByte = aUv  + static_cast<int64_t>(vc) * 8;
            if (aIdx  < 0) aIdx  = aByte + static_cast<int64_t>(vc);
            if (aSkin < 0) aSkin = aIdx + static_cast<int64_t>(tc) * 6;

            XMesh mesh;
            mesh.name = ReadName(block1, nameOffset);
            if (mesh.name.empty()) mesh.name = "mesh_" + std::to_string(g);
            mesh.material = static_cast<int>(material);
            mesh.vertexCount = static_cast<int>(vc);
            mesh.triangleCount = static_cast<int>(tc);

            mesh.positions.resize(vc);
            mesh.normals.resize(vc);
            mesh.uvs.resize(vc);
            mesh.paletteIndex.resize(vc);
            mesh.boneIndices.assign(static_cast<size_t>(vc) * 4, 0);
            mesh.weights.assign(static_cast<size_t>(vc) * 4, 0.0f);

            for (uint32_t v = 0; v < vc; ++v) {
                if (quantised) {
                    // Ein uint32 je Vertex: drei 10-Bit-Anteile in
                    // der Box aus dem Deskriptor.
                    const uint32_t packed = block2.U32At(
                        static_cast<size_t>(aQPos) + static_cast<size_t>(v) * 4);
                    const float fx = static_cast<float>(packed & 1023) / 1023.0f;
                    const float fy =
                        static_cast<float>((packed >> 10) & 1023) / 1023.0f;
                    const float fz =
                        static_cast<float>((packed >> 20) & 1023) / 1023.0f;
                    mesh.positions[v].x = qLo.x + fx * (qHi.x - qLo.x);
                    mesh.positions[v].y = qLo.y + fy * (qHi.y - qLo.y);
                    mesh.positions[v].z = qLo.z + fz * (qHi.z - qLo.z);

                    // Normalen als int8x3, drei Byte statt zwoelf.
                    if (aNrm3 >= 0) {
                        const size_t n3 = static_cast<size_t>(aNrm3) +
                                          static_cast<size_t>(v) * 3;
                        auto s8 = [&](size_t at) {
                            const int raw = static_cast<int>(block2.U8At(at));
                            return static_cast<float>(raw >= 128 ? raw - 256 : raw) /
                                   127.0f;
                        };
                        mesh.normals[v].x = s8(n3);
                        mesh.normals[v].y = s8(n3 + 1);
                        mesh.normals[v].z = s8(n3 + 2);
                    }
                } else {
                    const size_t p = static_cast<size_t>(aPos) + static_cast<size_t>(v) * 12;
                    mesh.positions[v].x = block2.F32At(p);
                    mesh.positions[v].y = block2.F32At(p + 4);
                    mesh.positions[v].z = block2.F32At(p + 8);
                    const size_t n = static_cast<size_t>(aNrm) + static_cast<size_t>(v) * 12;
                    mesh.normals[v].x = block2.F32At(n);
                    mesh.normals[v].y = block2.F32At(n + 4);
                    mesh.normals[v].z = block2.F32At(n + 8);
                }
                const size_t t = static_cast<size_t>(aUv) + static_cast<size_t>(v) * 8;
                mesh.uvs[v].x = block2.F32At(t);
                mesh.uvs[v].y = block2.F32At(t + 4);
                mesh.paletteIndex[v] =
                    block2.U8At(static_cast<size_t>(aByte) + static_cast<size_t>(v));

                const size_t s = static_cast<size_t>(aSkin) + static_cast<size_t>(v) * 24;
                for (int slot = 0; slot < 4; ++slot) {
                    const uint8_t boneIndex = block2.U8At(s + 1 + static_cast<size_t>(slot));
                    float weight = block2.F32At(s + 8 + static_cast<size_t>(slot) * 4);
                    if (boneIndex >= nbon) {
                        // 255 ist der Fuellwert "kein Bone". Das
                        // Gewicht nullen statt an einen Bone zu
                        // binden, den es nicht gibt.
                        ++mesh.clearedBoneSlots;
                        mesh.boneIndices[static_cast<size_t>(v) * 4 +
                                         static_cast<size_t>(slot)] = 0;
                        weight = 0.0f;
                    } else {
                        mesh.boneIndices[static_cast<size_t>(v) * 4 +
                                         static_cast<size_t>(slot)] = boneIndex;
                    }
                    mesh.weights[static_cast<size_t>(v) * 4 +
                                 static_cast<size_t>(slot)] = weight;
                }
            }

            mesh.indices.reserve(static_cast<size_t>(tc) * 3);
            for (uint32_t t = 0; t < tc; ++t) {
                const size_t at = static_cast<size_t>(aIdx) + static_cast<size_t>(t) * 6;
                const uint32_t a = block2.U16At(at);
                const uint32_t b = block2.U16At(at + 2);
                const uint32_t c = block2.U16At(at + 4);
                // Manche Meshes sind mit 0xFFFF aufgefuellt. Solche
                // Dreiecke lassen sich nicht bauen; verwerfen statt
                // alles Nachfolgende zu vergiften.
                if (a >= vc || b >= vc || c >= vc) { ++mesh.droppedTriangles; continue; }
                mesh.indices.push_back(a);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
            }

            if (mesh.droppedTriangles > 0)
                result.warnings.push_back("Mesh " + std::to_string(g) + " '" + mesh.name +
                                          "': " + std::to_string(mesh.droppedTriangles) +
                                          " von " + std::to_string(tc) +
                                          " Dreiecken verweisen auf nicht vorhandene "
                                          "Vertices (0xFFFF-Fuellung), verworfen");
            if (mesh.clearedBoneSlots > 0)
                result.warnings.push_back("Mesh " + std::to_string(g) + " '" + mesh.name +
                                          "': " + std::to_string(mesh.clearedBoneSlots) +
                                          " Skin-Plaetze zeigen hinter die Bone-Liste, "
                                          "Gewichte genullt");
            if (mesh.indices.empty()) {
                result.warnings.push_back("Mesh " + std::to_string(g) + " '" + mesh.name +
                                          "' hat keine brauchbaren Dreiecke, uebersprungen");
                continue;
            }
            out.meshes.push_back(mesh);
        }

        // ------------------------------------------------------------
        //  Animation ohne deklarierte Clips
        //
        //  nact kann 0 sein, obwohl die Bonespuren voll bespielt
        //  sind: 045_skin5_huicheng.x deklariert keinen einzigen
        //  Clip, aber 110 von 120 Bones bewegen sich ueber 211
        //  Frames. Die Clipliste durchzugehen wirft das komplett
        //  weg.
        // ------------------------------------------------------------
        if (out.clips.empty() && out.frames > 1) {
            int moved = 0;
            for (size_t k = 0; k < out.bones.size(); ++k) {
                const XBone& bone = out.bones[k];
                bool changes = false;
                for (size_t f = 1; f < bone.pos.size() && !changes; ++f) {
                    if (std::fabs(bone.pos[f].x - bone.pos[0].x) > 1e-5f ||
                        std::fabs(bone.pos[f].y - bone.pos[0].y) > 1e-5f ||
                        std::fabs(bone.pos[f].z - bone.pos[0].z) > 1e-5f ||
                        std::fabs(bone.quat[f].x - bone.quat[0].x) > 1e-5f ||
                        std::fabs(bone.quat[f].y - bone.quat[0].y) > 1e-5f ||
                        std::fabs(bone.quat[f].z - bone.quat[0].z) > 1e-5f ||
                        std::fabs(bone.quat[f].w - bone.quat[0].w) > 1e-5f)
                        changes = true;
                }
                if (changes) ++moved;
            }
            if (moved > 0) {
                XClip clip;
                clip.name = "full_timeline";
                clip.start = 0;
                clip.end = out.frames - 1;
                clip.synthetic = true;
                out.clips.push_back(clip);
                result.warnings.push_back(
                    "die Datei deklariert keine Clips, aber " + std::to_string(moved) +
                    " von " + std::to_string(out.bones.size()) +
                    " Bones bewegen sich - ein synthetischer Clip 'full_timeline' "
                    "deckt die ganze Zeitleiste ab");
            }
        }

        // ---- Clips auf die Zeitleiste KLEMMEN ----
        //
        // Bisher wurde nur gewarnt. Der Importer legte dann Keys
        // fuer Frames an, zu denen es keine Daten gibt - bei
        // monster_youlong.x reicht der 'dead'-Clip bis 156, die
        // Datei hat 53 Frames. Das kam als zerrissene Animation
        // an, und die Warnung ging in der Liste unter.
        //
        // Klemmen statt ablehnen, wie das Blender-Addon: der
        // Fehler steckt in der Datei, die restlichen Clips sind
        // brauchbar.
        int clamped = 0;
        for (XClip& clip : out.clips) {
            if (clip.end >= out.frames) {
                clip.end = out.frames - 1;
                ++clamped;
            }
            if (clip.start > clip.end) clip.start = clip.end;
            if (clip.start < 0) clip.start = 0;
        }
        if (clamped > 0)
            result.warnings.push_back(
                std::to_string(clamped) +
                " Clip(s) reichten ueber die Zeitleiste hinaus und wurden auf "
                + std::to_string(out.frames - 1) + " begrenzt - ein Fehler in "
                "der Datei, kein Dekodierproblem");

    } catch (const ParseError& error) {
        out = XFile();
        result.error = source.empty() ? error.what() : (source + ": " + error.what());
        return result;
    } catch (const std::bad_alloc&) {
        out = XFile();
        result.error = source + ": Speicher reicht nicht - Datei vermutlich beschaedigt";
        return result;
    }

    result.ok = true;
    return result;
}

ReadResult ReadXFile(const std::string& path, XFile& out) {
    ReadResult result;
    std::vector<uint8_t> buffer;
    if (!ReadWholeFile(path, buffer, result.error)) return result;
    return ReadXBuffer(buffer.data(), buffer.size(), path, out);
}

} // namespace eg3d
