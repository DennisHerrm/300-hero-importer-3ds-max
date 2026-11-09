// ============================================================
//  EG3D Import - Szenenaufbau
//
//  Grundregeln dieser Datei:
//
//  1. KEIN Parsen. Alles, was Bytes liest, steht in eg3d_*.cpp
//     und laesst sich ohne Max pruefen.
//  2. KEINE Threads. Das Max-SDK ist nicht threadsicher - kein
//     INode, kein Mesh, kein Interface-Aufruf ausserhalb des
//     Hauptthreads. Der Parser darf parallel arbeiten, diese
//     Datei nie.
//  3. Jeder Zeiger aus dem SDK wird geprueft, bevor er benutzt
//     wird. CreateInstance liefert nullptr, wenn eine Klasse
//     fehlt - und das passiert im Feld, nicht nur in der Theorie.
// ============================================================

#include "eg3dimport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <iterator>

namespace eg3d {

namespace {

// ------------------------------------------------------------
//  Zeichenumwandlung
//
//  Die Dateien tragen GBK-Namen (300 Heroes ist ein chinesisches
//  Spiel). eg3d_common.cpp wandelt GBK nach UTF-8; hier geht es
//  von UTF-8 nach UTF-16, weil Max mit wchar_t arbeitet.
// ------------------------------------------------------------
std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();

#ifdef _WIN32
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();

    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &result[0], needed);
    return result;
#else
    // Nur fuer die Stub-Uebersetzung: eine Ersatzabbildung, die
    // ASCII durchreicht. Im echten Build laeuft immer der
    // Windows-Zweig.
    return std::wstring(text.begin(), text.end());
#endif
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();

#ifdef _WIN32
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();

    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        &result[0], needed, nullptr, nullptr);
    return result;
#else
    return std::string(text.begin(), text.end());
#endif
}

// Millisekunden aus der Datei in Max' Ticks. GetTicksPerFrame()
// und GetFrameRate() kommen aus den Szeneneinstellungen - wer mit
// 24 fps arbeitet, bekommt seine Keys an den richtigen Stellen.
// ------------------------------------------------------------
//  Millisekunden -> Ticks, ueber GANZE FRAMES
//
//  Die Datei speichert die Keyzeiten als GERUNDETE
//  Millisekunden: 33, 66, 100 - nicht 33.333, 66.667, 100.
//  Direkt in Ticks umgerechnet ergibt 33 ms bei 30 fps
//  0.99 Frames statt 1.0, und der Fehler summiert sich: nach
//  hundert Keys ist ein ganzer Frame Versatz aufgelaufen.
//
//  Sichtbar wird das als Keys, die zwischen den Frames liegen -
//  "halbe Frames" in der Zeitleiste.
//
//  Deshalb wird auf ganze Frames GERUNDET. Die Zeiten in der
//  Datei sind ohnehin auf Frames gemeint; die Millisekunden
//  sind nur die Schreibweise.
// ------------------------------------------------------------
TimeValue MsToTicks(double ms, bool snapToFrames, double msPerFrame = 0.0) {
    // ------------------------------------------------------------
    //  Im KEYRASTER DER DATEI rechnen, nicht in Millisekunden
    //
    //  Die Keys stehen 33 ms auseinander. Bei 30 fps sind das
    //  0.99 Frames - und Runden verschluckt jeden hundertsten Key:
    //
    //      Key 50 -> 49.50 -> Frame 50
    //      Key 51 -> 50.49 -> Frame 50   (derselbe!)
    //      Key 52 -> 51.48 -> Frame 51
    //
    //  Zwei Keys auf einem Frame heisst: einer geht verloren, die
    //  Bewegung ruckt. Sichtbar wurde es als "alle Animationen
    //  stottern ausser Run" - bat_run ist mit 795 ms nur 24 Keys
    //  lang und bleibt unter der ersten Kollision bei Key 51.
    //
    //  Mit msPerFrame wird stattdessen im Raster der Datei
    //  gerechnet: ein Keyabstand ist genau ein Frame. Damit kann
    //  keine Kollision mehr entstehen, egal wie lang der Clip ist.
    // ------------------------------------------------------------
    if (snapToFrames && msPerFrame > 1e-6) {
        const double frame = ms / msPerFrame;
        return static_cast<TimeValue>(std::lround(frame)) * GetTicksPerFrame();
    }
    if (snapToFrames) {
        const double frame = ms * static_cast<double>(GetFrameRate()) / 1000.0;
        return static_cast<TimeValue>(std::lround(frame)) * GetTicksPerFrame();
    }
    const double ticksPerMs = static_cast<double>(GetTicksPerFrame()) *
                              static_cast<double>(GetFrameRate()) / 1000.0;
    return static_cast<TimeValue>(std::lround(ms * ticksPerMs));
}

TimeValue FrameToTicks(int frame) {
    return static_cast<TimeValue>(frame) * GetTicksPerFrame();
}

// ------------------------------------------------------------
//  Matrix44 (eigene, Zeilenvektor) -> Matrix3 (Max)
//
//  Beide Typen ordnen zeilenweise an: Zeilen 0..2 sind die Basis,
//  Zeile 3 die Translation. Deshalb wird hier NICHT transponiert
//  und nicht umsortiert - nur kopiert.
//
//  Wer hier ein Transponieren einbaut, weil das Blender-Addon
//  eines hat, dreht jedes Modell falsch herum. Das Addon
//  transponiert, WEIL Blender Spaltenvektoren fuehrt.
// ------------------------------------------------------------
Matrix3 ToMatrix3(const Matrix44& source, float scale) {
    Matrix3 result(1);
    for (int row = 0; row < 3; ++row) {
        result.SetRow(row, Point3(source.m[row][0], source.m[row][1],
                                  source.m[row][2]));
    }
    result.SetRow(3, Point3(source.m[3][0] * scale, source.m[3][1] * scale,
                            source.m[3][2] * scale));
    return result;
}

// ------------------------------------------------------------
//  Y-up nach Z-up: Drehung um +90 Grad um X.
//
//      (x, y, z)  ->  (x, -z, y)
//
//  Zeilenvektor-Konvention, passend zu Matrix44 und Matrix3.
// ------------------------------------------------------------
Matrix44 UpAxisMatrix() {
    Matrix44 m;
    m.m[0][0] = 1.0f; m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = 0.0f;
    m.m[1][0] = 0.0f; m.m[1][1] = 0.0f; m.m[1][2] = 1.0f; m.m[1][3] = 0.0f;
    m.m[2][0] = 0.0f; m.m[2][1] = -1.0f; m.m[2][2] = 0.0f; m.m[2][3] = 0.0f;
    m.m[3][0] = 0.0f; m.m[3][1] = 0.0f; m.m[3][2] = 0.0f; m.m[3][3] = 1.0f;
    return m;
}

// ------------------------------------------------------------
//  Engine-Raum (Y-up) -> Max (Z-up), siehe
//  ImportOptions::engineSpace. JEDE Weltmatrix der .model-Szene
//  geht genau einmal hier durch - Bind-Pose, Skin-Bindlage,
//  Mesh-Knoten und Animation. Nur dann bleibt die Deformation
//  v * IBM * Welt unveraendert und Bind-Pose und Animation
//  liegen im selben Raum.
// ------------------------------------------------------------
//
//  Die Matrix ist Y-up -> Z-up UND 180 Grad um Z:
//
//      (x, y, z)  ->  (-x, z, y)
//
//  Ohne die halbe Drehung schaute eine .model-Figur nach +Y,
//  eine .x-Figur nach -Y (am Render nachgesehen, 013.x gegen
//  336_skin1.model). -Y ist die Max-Konvention: die
//  Front-Ansicht zeigt das Gesicht. Reine Drehung (det = +1),
//  keine Spiegelung - die Haendigkeit bleibt.
Matrix44 EngineToMaxMatrix() {
    Matrix44 m;
    m.m[0][0] = -1.0f; m.m[0][1] = 0.0f; m.m[0][2] = 0.0f; m.m[0][3] = 0.0f;
    m.m[1][0] = 0.0f;  m.m[1][1] = 0.0f; m.m[1][2] = 1.0f; m.m[1][3] = 0.0f;
    m.m[2][0] = 0.0f;  m.m[2][1] = 1.0f; m.m[2][2] = 0.0f; m.m[2][3] = 0.0f;
    m.m[3][0] = 0.0f;  m.m[3][1] = 0.0f; m.m[3][2] = 0.0f; m.m[3][3] = 1.0f;
    return m;
}

Matrix44 MaxSpace(const Matrix44& world, bool on) {
    return on ? MatMul(world, EngineToMaxMatrix()) : world;
}

// ------------------------------------------------------------
//  Bind-Pose als Max-Skin-Pose ablegen (ImportOptions::
//  storeSkinPose). Aufzurufen, solange Frame 0 noch die Bind-Pose
//  zeigt - also VOR dem Aufbau der Animation.
//
//  Alle drei Anteile einschalten: in den Bind-Posen steckt
//  Massstab (Mesh-Knoten mit 0.59, 1.76, 21.35 ...), und Max
//  laesst Skalierung in der Skin Pose standardmaessig weg.
// ------------------------------------------------------------
int StoreSkinPose(const std::vector<INode*>& nodes) {
    int stored = 0;
    for (INode* node : nodes) {
        if (node == nullptr) continue;
        ISkinPose* pose = ISkinPose::GetISkinPose(*node);
        if (pose == nullptr) continue;
        pose->EnableSkinPos(true);
        pose->EnableSkinRot(true);
        pose->EnableSkinScale(true);
        pose->SetSkinPose(0);
        ++stored;
    }
    return stored;
}

Point3 ToPoint3(const Vec3& v, float scale = 1.0f) {
    return Point3(v.x * scale, v.y * scale, v.z * scale);
}

// Bewusst KEINE eigene Quat-nach-Max-Umrechnung.
//
// Rotationen gehen ausschliesslich ueber
// MatFromQuatPosScale() -> ToMatrix3(). Eine zweite Umrechnung
// waere eine zweite Wahrheit ueber die Drehrichtung, und die
// beiden auseinanderlaufen zu sehen kostet einen Abend: in der
// Bind-Pose stimmen sie ueberein, erst in der Animation nicht.

// Ein Vertexindex, der auf nichts zeigt, ist kein Grund
// abzubrechen - aber einer, der still durchgeht, baut ein Mesh
// mit falschen Dreiecken.
bool IndicesInRange(const std::vector<uint32_t>& indices, size_t vertexCount) {
    return std::all_of(indices.begin(), indices.end(),
                       [vertexCount](uint32_t i) { return i < vertexCount; });
}

}  // namespace

// ============================================================
//  Der Interface-Deskriptor
//
//  DIE DRITTE EINTRAGUNGSSTELLE. Eine Funktion muss stehen:
//    1. im enum Eg3dImportFnID   (eg3dimport.h)
//    2. in der BEGIN_FUNCTION_MAP (eg3dimport.h)
//    3. hier
//
//  Fehlt die dritte, ist die Funktion bei MAXScript gar nicht
//  angemeldet - und kein Neubau behebt das, weil der Quelltext
//  vollstaendig aussieht: enum und FUNCTION_MAP haben ihren
//  Eintrag. Genau dieser Fehler hat bei XFBIN Import eine
//  Version gekostet.
//
//  PRUEFE_API.py gleicht die drei Listen ab, auch die
//  Argumentzahl: FN_2 in der Map und eine 1 im Deskriptor ergibt
//  keinen Uebersetzungsfehler, sondern falsch belegte Argumente
//  zur Laufzeit.
// ============================================================
static Eg3dImportInterface theEg3dImportInterface(
    EG3DIMPORT_INTERFACE_ID,
    _T("Eg3dCpp"), 0, nullptr, FP_CORE,

    // --- Datei oeffnen und schliessen ---
    Eg3dImportInterface::fn_open, _T("open"), 0, TYPE_INT, 0, 1,
        _T("path"), 0, TYPE_STRING,
    Eg3dImportInterface::fn_close, _T("close"), 0, TYPE_INT, 0, 0,
    Eg3dImportInterface::fn_isOpen, _T("isOpen"), 0, TYPE_INT, 0, 0,
    Eg3dImportInterface::fn_kind, _T("kind"), 0, TYPE_STRING, 0, 0,

    // --- Auskunft ---
    Eg3dImportInterface::fn_summary, _T("summary"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_clipNames, _T("clipNames"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_dump, _T("dump"), 0, TYPE_INT, 0, 1,
        _T("outPath"), 0, TYPE_STRING,

    // --- Optionen ---
    Eg3dImportInterface::fn_setOption, _T("setOption"), 0, TYPE_INT, 0, 2,
        _T("name"), 0, TYPE_STRING,
        _T("value"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setScale, _T("setScale"), 0, TYPE_INT, 0, 1,
        _T("value"), 0, TYPE_FLOAT,

    // --- Aufbau ---
    Eg3dImportInterface::fn_import, _T("import"), 0, TYPE_INT, 0, 0,

    Eg3dImportInterface::fn_pendingFile, _T("pendingFile"), 0, TYPE_STRING, 0, 0,

    // --- Protokoll ---
    Eg3dImportInterface::fn_log, _T("log"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_saveLog, _T("saveLog"), 0, TYPE_INT, 0, 1,
        _T("outPath"), 0, TYPE_STRING,
    Eg3dImportInterface::fn_setDebug, _T("setDebug"), 0, TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setRestFrames, _T("setRestFrames"), 0, TYPE_INT, 0, 1,
        _T("frames"), 0, TYPE_INT,
    Eg3dImportInterface::fn_clipRanges, _T("clipRanges"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_setReduceKeys, _T("setReduceKeys"), 0, TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setBindFromInvBind, _T("setBindFromInvBind"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setMatchBindScale, _T("setMatchBindScale"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setUpAxisFix, _T("setUpAxisFix"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setBindScaleOnly, _T("setBindScaleOnly"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setRetarget, _T("setRetargetAnimation"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setSnapKeys, _T("setSnapKeys"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setBindMesh, _T("setBindIncludesMesh"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setMeshAxis, _T("setMeshAxisFix"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setFlipUv, _T("setFlipUvV"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setLinearKeys, _T("setLinearKeys"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setIgnoreZeroScale, _T("setIgnoreZeroScale"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setMeshScale, _T("setMeshBindScale"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setAnimRoot, _T("setAnimRootFix"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,
    Eg3dImportInterface::fn_setAnimScale, _T("setAnimBindScale"), 0,
        TYPE_INT, 0, 1,
        _T("on"), 0, TYPE_INT,

    // --- Zustand ---
    Eg3dImportInterface::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_lastError, _T("lastError"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_warnings, _T("warnings"), 0, TYPE_STRING, 0, 0,
    Eg3dImportInterface::fn_stats, _T("stats"), 0, TYPE_STRING, 0, 0,

    // p_end, nicht end: "end" ist kein SDK-Bezeichner. Der Stub
    // hatte es falsch nachgebildet, der echte Build meldet
    // C2065.
    p_end
);

Eg3dImportInterface* GetEg3dInterface() { return &theEg3dImportInterface; }

// ============================================================
//  Gemeinsame Helfer
// ============================================================

void SceneBuilder::Warn(const std::wstring& text) {
    // Deckel: eine kaputte Datei kann sonst Zehntausende Warnungen
    // erzeugen, und die Oberflaeche haengt daran, nicht am Import.
    const size_t kMaxWarnings = 200;
    if (warnings_.size() < kMaxWarnings) {
        warnings_.push_back(text);
    } else if (warnings_.size() == kMaxWarnings) {
        warnings_.push_back(L"... weitere Warnungen unterdrueckt.");
    }
}

void SceneBuilder::Note(const std::wstring& text) {
    Msg line;
    line << L"[";
    line.Fixed(clock_.ms(), 1);
    line << L" ms] " << text;
    log_.push_back(line.str());
}

INode* SceneBuilder::MakeNode(const std::wstring& name, bool asBone) {
    if (ip_ == nullptr) return nullptr;

    Object* object = nullptr;

    if (asBone) {
        // BONE_OBJ_CLASSID ist ein GeomObject, kein Helper. Seine
        // Abmessungen stehen im Parameterblock boneobj_params
        // (boneobj_width, _height, _taper, _length aus plugapi.h)
        // und sind nach dem Anlegen 0 - ein Bone ohne Laenge ist
        // im Viewport nicht zu sehen.
        //
        // Gesetzt werden sie NICHT hier, sondern in ShapeBones():
        // die Laenge eines Bones ist der Abstand zu seinem Kind,
        // und die Hierarchie steht an dieser Stelle noch nicht.
        object = static_cast<Object*>(
            ip_->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
    }

    // Rueckfallebene: Point-Helper. Der braucht keine
    // Parameterblock-Kenntnis und wird vom Skin-Modifier genauso
    // als Knochen angenommen.
    if (object == nullptr) {
        object = static_cast<Object*>(
            ip_->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
    }

    if (object == nullptr) return nullptr;

    INode* node = ip_->CreateObjectNode(object);
    if (node == nullptr) return nullptr;

    node->SetName(const_cast<MCHAR*>(name.c_str()));
    ++stats_.nodes;
    if (options_.verbose) Note(Msg() << L"  Knoten: " << name);
    return node;
}

// ============================================================
//  ParentsFirst - topologische Reihenfolge
//
//  DER FEHLER, DEN DAS HIER BEHEBT
//
//  INode::SetNodeTM setzt die WELTmatrix. Wird danach der
//  Elternteil bewegt, wandert das Kind mit - relativ zu seiner
//  neuen Elternlage. Wer also ein Kind setzt, bevor sein
//  Elternteil an seinem Platz steht, verschiebt es zweimal.
//
//  In den .x-Dateien stehen die Bones ALPHABETISCH, nicht nach
//  Hierarchie: "Bip001 Head" ist Index 1, sein Elternteil
//  "Bip001 Neck" ist Index 26. Der Kopf wurde also gesetzt,
//  lange bevor der Hals stand - und danach vom Hals, von der
//  Wirbelsaeule und vom Becken je noch einmal mitgenommen.
//
//  Das Ergebnis sah im Viewport so aus: Arme abgespreizt,
//  Einzelteile quer im Raum, Knoten mit Koordinaten wie
//  X = -1312046. Kein Fehler in der Bind-Pose, keiner im
//  Quaternion - nur die falsche Reihenfolge.
//
//  Der Zyklusschutz ist kein Zierrat: ein kaputter parent-Index,
//  der auf sich selbst oder rueckwaerts zeigt, wuerde die
//  Schleife sonst nie verlassen.
//
//  ACHTUNG: tools/pruefe_hierarchie.cpp enthaelt eine Kopie
//  dieser Funktion, weil diese Datei Max-Header einbindet und
//  sich ohne SDK nicht uebersetzen laesst. Wer hier etwas
//  aendert, muss die Kopie mitziehen.
// ============================================================
std::vector<int> SceneBuilder::ParentsFirst(const std::vector<int>& parents) {
    const size_t count = parents.size();
    std::vector<int> order;
    order.reserve(count);

    std::vector<char> done(count, 0);
    std::vector<int> stack;

    for (size_t start = 0; start < count; ++start) {
        if (done[start]) continue;

        // Kette nach oben sammeln, bis zu einer Wurzel oder einem
        // schon erledigten Knoten.
        stack.clear();
        int at = static_cast<int>(start);
        while (at >= 0 && static_cast<size_t>(at) < count &&
               !done[static_cast<size_t>(at)]) {
            // Zyklus: dieser Knoten liegt schon auf dem Stapel.
            if (std::find(stack.begin(), stack.end(), at) != stack.end()) break;
            stack.push_back(at);
            at = parents[static_cast<size_t>(at)];
        }

        // Von der Wurzel her ausgeben.
        for (size_t i = stack.size(); i > 0; --i) {
            const int index = stack[i - 1];
            if (done[static_cast<size_t>(index)]) continue;
            done[static_cast<size_t>(index)] = 1;
            order.push_back(index);
        }
    }

    return order;
}

// ============================================================
//  MeasureBindFix - die Korrektur AUSMESSEN statt raten
//
//  Zwischen den inversen Bindmatrizen (meta[4]) und den
//  Knotentransformationen (meta[5]) liegt eine feste
//  Transformation. Sie wirkt von RECHTS:
//
//      bindWorld = nodeWorld * C
//      also  C = inv(nodeWorld) * bindWorld
//
//  DIE SEITE IST DER GANZE PUNKT. Von links gerechnet
//  (C = bindWorld * inv(nodeWorld)) ist die Differenz je Bone
//  eine andere - Becken und Wirbelsaeule bekommen das Gegenteil
//  von Ober- und Unterschenkel. Das sah nach zwei verschiedenen
//  Koerperhaltungen aus, und daran ist der erste Anlauf
//  gescheitert.
//
//  Von rechts gerechnet ist sie ueber alle 176 Bindmatrizen von
//  039_skin8.model dieselbe:
//
//      [ 1.68812   0         0      ]
//      [ 0         0         1.68812]
//      [ 0        -1.68812   0      ]
//      T = 0
//
//  Das ist Skalierung 1.68812 mal einer Drehung um -90 Grad um
//  X - eine Y-up-nach-Z-up-Umrechnung. Die Skalierung allein
//  war also nur ein Drittel der Antwort.
//
//  Gemessen wird komponentenweise per MEDIAN: einzelne Bones
//  folgen der Regel nicht (Effektknoten, der zweite Charakter
//  im selben Modell), und ein Mittelwert waere davon zu
//  verziehen. Wie viele folgen, steht im Protokoll - eine
//  Korrektur, der nur die Haelfte folgt, waere keine.
// ============================================================
void SceneBuilder::MeasureBindFix(const ModelFile& file) {
    paletteFix_.assign(file.palettes.size(), Matrix44());
    paletteFixValid_.assign(file.palettes.size(), 0);
    nodePalette_.assign(file.nodes.size(), -1);

    // Weltmatrizen aus den Knotendaten, Eltern vor Kindern.
    std::vector<int> parents(file.nodes.size());
    std::transform(file.nodes.begin(), file.nodes.end(), parents.begin(),
                   [](const ModelNode& node) { return node.parent; });

    std::vector<Matrix44> nodeWorld(file.nodes.size());
    for (int index : ParentsFirst(parents)) {
        const size_t i = static_cast<size_t>(index);
        const ModelNode& source = file.nodes[i];
        const Matrix44 local = MatFromQuatPosScale(
            source.rotation, source.translation, source.scale);
        nodeWorld[i] = (source.parent >= 0 &&
                        static_cast<size_t>(source.parent) < nodeWorld.size())
                           ? MatMul(local, nodeWorld[static_cast<size_t>(source.parent)])
                           : local;
    }

    int totalAgree = 0, totalBones = 0;

    for (size_t p = 0; p < file.palettes.size(); ++p) {
        const Palette& palette = file.palettes[p];

        std::vector<std::vector<float>> samples(12);
        std::vector<std::pair<size_t, Matrix44>> perBone;

        for (size_t b = 0; b < palette.bones.size(); ++b) {
            const int global = palette.bones[b];
            if (global < 0 || static_cast<size_t>(global) >= nodeWorld.size()) continue;
            if (b >= palette.invBind.size()) continue;

            Matrix44 bindW, nodeInv;
            if (!MatInvert(palette.invBind[b], bindW)) continue;
            if (!MatInvert(nodeWorld[static_cast<size_t>(global)], nodeInv)) continue;

            // C = inv(nodeWorld) * bindWorld - von RECHTS.
            const Matrix44 c = MatMul(nodeInv, bindW);
            perBone.emplace_back(static_cast<size_t>(global), c);

            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 3; ++col) {
                    samples[static_cast<size_t>(row * 3 + col)].push_back(c.m[row][col]);
                }
            }
        }

        // Eine Palette mit einem einzigen Bone (die Waffen und
        // Effekte in dieser Datei) taugt nicht zum Messen: dort
        // ist der Median der eine Wert, und ob er eine Konvention
        // beschreibt oder eine Einzellage, laesst sich nicht
        // unterscheiden.
        if (samples[0].size() < 4) continue;

        Matrix44 fix;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 3; ++col) {
                std::vector<float>& v = samples[static_cast<size_t>(row * 3 + col)];
                const size_t middle = v.size() / 2;
                std::nth_element(v.begin(), v.begin() + middle, v.end());
                fix.m[row][col] = v[middle];
            }
            fix.m[row][3] = (row == 3) ? 1.0f : 0.0f;
        }

        int agree = 0;
        for (const std::pair<size_t, Matrix44>& entry : perBone) {
            float worst = 0.0f;
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    worst = std::max(worst,
                                     std::fabs(entry.second.m[row][col] - fix.m[row][col]));
                }
            }
            if (worst < 0.01f) ++agree;
        }

        totalAgree += agree;
        totalBones += static_cast<int>(perBone.size());

        // Nur uebernehmen, wenn die Palette der Korrektur auch
        // wirklich folgt. Sonst richtet sie in dieser Palette
        // mehr an, als sie behebt.
        const bool trustworthy =
            (perBone.size() > 0) &&
            (agree * 10 >= static_cast<int>(perBone.size()) * 9);

        if (trustworthy) {
            paletteFix_[p] = fix;
            paletteFixValid_[p] = 1;
            for (const std::pair<size_t, Matrix44>& entry : perBone) {
                if (nodePalette_[entry.first] < 0) {
                    nodePalette_[entry.first] = static_cast<int>(p);
                }
            }
        }

        Note(Msg() << L"  Palette " << p << L": " << agree << L" von "
                   << perBone.size() << L" Bones folgen "
                   << (trustworthy ? L"(uebernommen)" : L"(verworfen)"));
    }

    Note(Msg() << L"Korrektur je Palette gemessen: " << totalAgree
               << L" von " << totalBones << L" Bones insgesamt");
}

void SceneBuilder::ReportExtents() {
    if (ip_ == nullptr) return;

    auto box = [](const std::vector<INode*>& list, const wchar_t* label,
                  SceneBuilder* self) {
        bool any = false;
        Point3 lo(0.0f, 0.0f, 0.0f), hi(0.0f, 0.0f, 0.0f);
        for (INode* node : list) {
            if (node == nullptr) continue;
            const Point3 p = node->GetNodeTM(0).GetTrans();
            if (!any) { lo = p; hi = p; any = true; continue; }
            lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
            lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
            lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
        }
        if (!any) return;
        Msg out;
        out << L"  " << label << L": X ";
        out.Fixed(lo.x, 2) << L" .. "; out.Fixed(hi.x, 2);
        out << L"   Y "; out.Fixed(lo.y, 2) << L" .. "; out.Fixed(hi.y, 2);
        out << L"   Z "; out.Fixed(lo.z, 2) << L" .. "; out.Fixed(hi.z, 2);
        self->Note(out.str());
    };

    Note(L"Ausdehnung bei Frame 0 (Knotenlagen):");
    box(nodes_, L"Bones ", this);

    // Die Bone-Hoehe bei Frame 0 getrennt merken - fuer den
    // Vergleich unten. Vorher stand dort versehentlich die
    // Vertex-Box, und das Verhaeltnis war damit unbrauchbar.
    float boneHeight0 = 0.0f;
    {
        bool got = false;
        float lo0 = 0.0f, hi0 = 0.0f;
        for (INode* node : nodes_) {
            if (node == nullptr) continue;
            const float z = node->GetNodeTM(0).GetTrans().z;
            if (!got) { lo0 = z; hi0 = z; got = true; continue; }
            lo0 = std::min(lo0, z); hi0 = std::max(hi0, z);
        }
        if (got) boneHeight0 = hi0 - lo0;
    }

    std::vector<INode*> meshes(builtMeshes_.size());
    std::transform(builtMeshes_.begin(), builtMeshes_.end(), meshes.begin(),
                   [](const BuiltMesh& built) { return built.node; });
    box(meshes, L"Meshes", this);

    // Und die tatsaechliche Groesse der Mesh-Geometrie - die
    // Knotenlage allein sagt nichts, wenn die Vertices absolut
    // stehen.
    bool any = false;
    Point3 lo(0.0f, 0.0f, 0.0f), hi(0.0f, 0.0f, 0.0f);
    for (const BuiltMesh& built : builtMeshes_) {
        if (built.node == nullptr) continue;
        Object* object = built.node->GetObjectRef();
        if (object == nullptr) continue;
        TriObject* tri = dynamic_cast<TriObject*>(object);
        if (tri == nullptr) continue;

        Mesh& mesh = tri->GetMesh();
        const Matrix3 tm = built.node->GetNodeTM(0);
        for (int v = 0; v < mesh.numVerts; ++v) {
            const Point3 p = tm * mesh.getVert(v);
            if (!any) { lo = p; hi = p; any = true; continue; }
            lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
            lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
            lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
        }
    }
    if (any) {
        Msg out;
        out << L"  Vertices: X "; out.Fixed(lo.x, 2) << L" .. "; out.Fixed(hi.x, 2);
        out << L"   Y "; out.Fixed(lo.y, 2) << L" .. "; out.Fixed(hi.y, 2);
        out << L"   Z "; out.Fixed(lo.z, 2) << L" .. "; out.Fixed(hi.z, 2);
        Note(out.str());
    }

    // ------------------------------------------------------------
    //  DER SPRUNG: dieselbe Messung bei einem Animationsframe.
    //
    //  Genau danach wird seit zwanzig Versuchen gesucht - "ab der
    //  ersten Animation dreht und skaliert es". Stehen hier zwei
    //  Zeilen mit deutlich verschiedenen Zahlen, ist der Faktor
    //  ablesbar statt zu erraten.
    //
    //  Frame 30 ist willkuerlich, aber weit genug hinter den
    //  Ruheframes, um in einem Clip zu liegen.
    // ------------------------------------------------------------
    const TimeValue probe = FrameToTicks(30);
    bool any2 = false;
    Point3 lo2(0.0f, 0.0f, 0.0f), hi2(0.0f, 0.0f, 0.0f);
    for (INode* node : nodes_) {
        if (node == nullptr) continue;
        const Point3 p = node->GetNodeTM(probe).GetTrans();
        if (!any2) { lo2 = p; hi2 = p; any2 = true; continue; }
        lo2.x = std::min(lo2.x, p.x); hi2.x = std::max(hi2.x, p.x);
        lo2.y = std::min(lo2.y, p.y); hi2.y = std::max(hi2.y, p.y);
        lo2.z = std::min(lo2.z, p.z); hi2.z = std::max(hi2.z, p.z);
    }
    if (any2) {
        Msg out;
        out << L"  Bones @Frame 30: X "; out.Fixed(lo2.x, 2) << L" .. ";
        out.Fixed(hi2.x, 2);
        out << L"   Y "; out.Fixed(lo2.y, 2) << L" .. "; out.Fixed(hi2.y, 2);
        out << L"   Z "; out.Fixed(lo2.z, 2) << L" .. "; out.Fixed(hi2.z, 2);
        Note(out.str());

        // Das Verhaeltnis der Hoehen sagt den Massstabssprung
        // direkt an - 1.0 heisst kein Sprung. BEIDE Werte muessen
        // von den BONES kommen.
        const float h0 = boneHeight0;
        const float h1 = hi2.z - lo2.z;
        if (h0 > 1e-4f && h1 > 1e-4f) {
            Msg ratio;
            ratio << L"  Hoehenverhaeltnis Frame 30 / Frame 0: ";
            ratio.Fixed(static_cast<double>(h1 / h0), 4);
            ratio << L"  (1.0 = kein Massstabssprung)";
            Note(ratio.str());
        }
    }
}

// ------------------------------------------------------------
//  Texturdatei suchen - der Name in der Datei stimmt nicht immer
//
//  068.x nennt "068.tga", ausgeliefert wird "068.dds". Die Engine
//  loest das ueber ihren Ressourcenpfad auf; beim Import neben der
//  Modelldatei muss die Endung durchprobiert werden.
//
//  Ohne das setzt der Importer stillschweigend einen Pfad, der ins
//  Leere zeigt: das Material bekommt eine Bitmap, die Bitmap hat
//  eine Datei, die es nicht gibt - und im Viewport bleibt alles
//  grau, ohne dass irgendwo etwas gemeldet wird.
//
//  Reihenfolge: erst der Name wie angegeben, dann die ueblichen
//  Endungen. Gross- und Kleinschreibung spielt unter Windows keine
//  Rolle.
// ------------------------------------------------------------
std::wstring SceneBuilder::FindTexture(const std::wstring& uri) const {
    if (uri.empty()) return std::wstring();

    // Ueber ifstream statt GetFileAttributesW: das kommt ohne
    // Windows-Header aus und laesst sich damit auch gegen den
    // SDK-Stub uebersetzen.
    auto exists = [](const std::wstring& path) {
        if (path.empty()) return false;
        std::ifstream probe(WideToUtf8(path).c_str(), std::ios::binary);
        return probe.good();
    };

    const std::wstring dir = baseDir_.empty() ? std::wstring() : baseDir_ + L"\\";

    // 1. So, wie es in der Datei steht.
    if (exists(dir + uri)) return dir + uri;

    // 2. Andere Endung, gleicher Stamm.
    const size_t dot = uri.find_last_of(L'.');
    const std::wstring stem = (dot == std::wstring::npos) ? uri : uri.substr(0, dot);

    static const wchar_t* kEndings[] = {L".dds", L".tga", L".png", L".jpg",
                                        L".jpeg", L".bmp", L".tif"};
    for (const wchar_t* ending : kEndings) {
        const std::wstring candidate = dir + stem + ending;
        if (exists(candidate)) return candidate;
    }

    return std::wstring();
}

// ------------------------------------------------------------
//  Lineare Controller statt TCB
//
//  Max legt beim Setzen von Keys standardmaessig TCB-Controller
//  an. Die Autodesk-Doku dazu:
//
//    "High Continuity values create curved OVERSHOOT on both
//     sides of the key. Low Continuity values create a linear
//     animation curve."
//
//  Continuity steht standardmaessig auf 25 - also Ueberschwingen
//  zwischen jedem Paar von Keys. Bei GEBACKENEN Spieldaten, wo
//  ohnehin jeder Frame einen Key hat, ist das falsch: die Kurve
//  schwingt zwischen Werten, die schon stimmen. Sichtbar als
//  Zittern und Nachschwingen.
//
//  Das Blender-Addon fuer dieselben Daten setzt aus demselben
//  Grund LINEAR als Standard: "No overshooting between the
//  animations, recommended for baked game data."
//
//  Deshalb bekommen die Knoten vor dem ersten Key lineare
//  Controller fuer Position, Rotation und Skalierung.
// ------------------------------------------------------------
void SceneBuilder::UseLinearControllers(INode* node) {
    if (node == nullptr || !options_.linearKeys) return;

    Control* tm = node->GetTMController();
    if (tm == nullptr) return;

    Control* pos = static_cast<Control*>(
        CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
    if (pos != nullptr) tm->SetPositionController(pos);

    Control* rot = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    if (rot != nullptr) tm->SetRotationController(rot);

    Control* scl = static_cast<Control*>(
        CreateInstance(CTRL_SCALE_CLASS_ID, Class_ID(LININTERP_SCALE_CLASS_ID, 0)));
    if (scl != nullptr) tm->SetScaleController(scl);
}

float SceneBuilder::SceneExtent() const {
    if (nodeWorldPos_.empty()) return 1.0f;

    Vec3 lo = nodeWorldPos_[0];
    Vec3 hi = nodeWorldPos_[0];
    for (const Vec3& p : nodeWorldPos_) {
        lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
        lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
        lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
    }
    const float extent = std::max(std::max(hi.x - lo.x, hi.y - lo.y),
                                  hi.z - lo.z);
    return (extent > 1e-4f) ? extent : 1.0f;
}

// ------------------------------------------------------------
//  ShapeBones - Bones sichtbar machen
//
//  Ein frisch angelegtes Bone-Objekt hat Laenge, Breite und Hoehe
//  0 und ist damit unsichtbar. Genau deshalb sah der erste
//  Importlauf nach Point-Helpern aus, auch wenn "Real bones"
//  eingeschaltet war.
//
//  Die Doku ist beim Laengenbegriff eindeutig: die Bone-Geometrie
//  wird "lengthwise from the pivot point to the bone's child
//  object" gezeichnet. Die Laenge ist also der Abstand zum Kind,
//  nicht eine feste Zahl - und ein Bone ohne Kind (Fingerspitze,
//  Haarende) bekommt einen kurzen Stummel, so wie Max ihn beim
//  Erzeugen einer Kette selbst anlegt.
//
//  Breite und Hoehe werden aus der Laenge abgeleitet. Das ist
//  Geschmackssache und keine Formatfrage; wichtig ist nur, dass
//  sie nicht 0 sind.
// ------------------------------------------------------------
void SceneBuilder::ShapeBones() {
    if (!options_.boneObjects) return;

    // Zuerst alle Kindabstaende sammeln, dann den Median als
    // Rueckfallwert nehmen. SceneExtent() waere die naheliegende
    // Wahl, taugt aber nicht: ein einziger Knoten weit ausserhalb
    // (Partikel-Emitter, Effekt-Dummy) blaeht sie auf, und dann
    // sind ALLE Bones zu dick. Der Median interessiert sich nicht
    // fuer Ausreisser.
    std::vector<float> distances;
    distances.reserve(nodes_.size());

    for (INode* node : nodes_) {
        if (node == nullptr) continue;
        const Point3 origin = node->GetNodeTM(0).GetTrans();
        for (int c = 0; c < node->NumberOfChildren(); ++c) {
            INode* child = node->GetChildNode(c);
            if (child == nullptr) continue;
            const float distance = Length(child->GetNodeTM(0).GetTrans() - origin);
            if (distance > 1e-4f) distances.push_back(distance);
        }
    }

    float median = 1.0f;
    if (!distances.empty()) {
        const size_t middle = distances.size() / 2;
        std::nth_element(distances.begin(), distances.begin() + middle,
                         distances.end());
        median = distances[middle];
    } else {
        median = SceneExtent() * 0.02f;
    }

    const float fallback = median;

    for (INode* node : nodes_) {
        if (node == nullptr) continue;

        Object* object = node->GetObjectRef();
        if (object == nullptr) continue;
        if (object->ClassID() != BONE_OBJ_CLASSID) continue;

        IParamBlock2* block = object->GetParamBlockByID(boneobj_params);
        if (block == nullptr) continue;

        // Abstand zum naechsten Kind. Mehrere Kinder: das naechste
        // gewinnt - alles andere liesse Bones quer durch das
        // Modell zeigen, wenn ein Becken auf beide Beine zeigt.
        float length = 0.0f;
        const Matrix3 own = node->GetNodeTM(0);
        const Point3 origin = own.GetTrans();

        for (int c = 0; c < node->NumberOfChildren(); ++c) {
            INode* child = node->GetChildNode(c);
            if (child == nullptr) continue;
            const Point3 delta = child->GetNodeTM(0).GetTrans() - origin;
            const float distance = Length(delta);
            if (distance > 1e-4f && (length <= 0.0f || distance < length)) {
                length = distance;
            }
        }

        if (length <= 0.0f) length = fallback;   // Bone ohne Kind

        // Dicke aus dem MEDIAN, nicht aus der eigenen Laenge: ein
        // langer Bone (Becken zu Fuss ueber mehrere Glieder) waere
        // sonst ein Balken quer durch das Modell.
        const float thickness = std::max(median * 0.12f, 1e-3f);

        block->SetValue(boneobj_length, 0, length);
        block->SetValue(boneobj_width, 0, thickness);
        block->SetValue(boneobj_height, 0, thickness);
        block->SetValue(boneobj_taper, 0, 90.0f);

        node->InvalidateTreeTM();
    }
}

void SceneBuilder::SetNodeWorld(INode* node, const Matrix44& world) {
    if (node == nullptr) return;
    Matrix3 tm = ToMatrix3(world, options_.scale);
    node->SetNodeTM(0, tm);
}


// ------------------------------------------------------------
//  Material
//
//  Bewusst ein Standardmaterial und keine Nachbildung der
//  Engine-Shader. Was das Spiel aus ezParticle macht, laesst sich
//  in Max nicht sinnvoll nachbauen; was ein Modelleur braucht,
//  ist die richtige Textur am richtigen Mesh.
// ------------------------------------------------------------
Mtl* SceneBuilder::MakeMaterial(const Material& material,
                                const std::vector<Texture>& textures,
                                const std::wstring& baseDir) {
    if (ip_ == nullptr) return nullptr;

    // StdMat2, nicht Mtl: SetTwoSided und SetOpacity sitzen an
    // StdMat2, nicht an der Basisklasse Mtl.
    StdMat2* mtl = static_cast<StdMat2*>(
        ip_->CreateInstance(MATERIAL_CLASS_ID, Class_ID(DMTL_CLASS_ID, 0)));
    if (mtl == nullptr) {
        Warn(Msg() << L"Material '" << Utf8ToWide(material.name)
                   << L"': StdMat liess sich nicht anlegen.");
        return nullptr;
    }

    mtl->SetName(Utf8ToWide(material.name).c_str());

    // doubleSide steht als Materialeigenschaft in der Datei und
    // hat in Max eine direkte Entsprechung.
    if (material.doubleSide) mtl->SetTwoSided(TRUE);

    // _TintColor faerbt in der Engine das ganze Material. Als
    // Diffusfarbe ist das die naechstliegende Entsprechung.
    const auto tint = material.vectors.find("_TintColor");
    if (tint != material.vectors.end()) {
        mtl->SetDiffuse(Color(tint->second.x, tint->second.y, tint->second.z), 0);
        if (tint->second.w > 0.0f && tint->second.w < 1.0f) {
            mtl->SetOpacity(tint->second.w, 0);
        }
    }

    // _MainTex ist der Diffuskanal. Die uebrigen Slots (_MaskTex,
    // _DissolveTex, _SpecTex) steuern Engine-Effekte, fuer die es
    // in einem StdMat keine ehrliche Entsprechung gibt - sie
    // landen deshalb als Benutzereigenschaft am Knoten statt als
    // falsch zugeordnete Map.
    const auto mainTex = material.textures.find("_MainTex");
    if (mainTex != material.textures.end() && mainTex->second >= 0 &&
        static_cast<size_t>(mainTex->second) < textures.size()) {

        BitmapTex* bitmap = static_cast<BitmapTex*>(
            ip_->CreateInstance(TEXMAP_CLASS_ID, Class_ID(BMTEX_CLASS_ID, 0)));
        if (bitmap != nullptr) {
            const std::wstring uri =
                Utf8ToWide(textures[static_cast<size_t>(mainTex->second)].uri);
            const std::wstring full = baseDir.empty() ? uri : (baseDir + L"\\" + uri);
            bitmap->SetMapName(full.c_str());
            bitmap->SetName(uri.c_str());
            mtl->SetSubTexmap(ID_DI, bitmap);
        } else {
            Warn(Msg() << L"Material '" << Utf8ToWide(material.name)
                       << L"': BitmapTex liess sich nicht anlegen.");
        }
    }

    ++stats_.materials;
    return mtl;
}

// ============================================================
//  .model - Knoten
// ============================================================

void SceneBuilder::BuildModelNodes(const ModelFile& file) {
    nodes_.assign(file.nodes.size(), nullptr);

    // Erst alle Knoten anlegen, dann verketten. Andersherum
    // muesste die Reihenfolge stimmen, und sie tut es nicht
    // zwingend: ein Kind kann in der Datei vor seinem Elternteil
    // stehen.
    for (size_t i = 0; i < file.nodes.size(); ++i) {
        const ModelNode& source = file.nodes[i];
        const std::wstring name = source.name.empty()
                                      ? (L"node_" + std::to_wstring(i))
                                      : Utf8ToWide(source.name);

        INode* node = MakeNode(name, options_.boneObjects);
        if (node == nullptr) {
            Warn(Msg() << L"Knoten " << i << L" (" << name
                       << L") liess sich nicht anlegen.");
            continue;
        }
        nodes_[i] = node;
    }

    // Verketten. Ein Knoten ohne Elternteil bleibt an der Wurzel -
    // Max haengt ihn dort von selbst hin.
    for (size_t i = 0; i < file.nodes.size(); ++i) {
        const ModelNode& source = file.nodes[i];
        if (nodes_[i] == nullptr) continue;
        if (source.parent < 0) continue;
        if (static_cast<size_t>(source.parent) >= nodes_.size()) continue;

        INode* parent = nodes_[static_cast<size_t>(source.parent)];
        if (parent != nullptr) parent->AttachChild(nodes_[i], 0);
    }

    // ------------------------------------------------------------
    //  Die BIND-POSE kommt aus den inversen Bindmatrizen,
    //  NICHT aus den Knotendaten.
    //
    //  meta[5] traegt Frame 0 des autoPlay-Clips, nicht die
    //  Ruhestellung - das steht seit der Formatanalyse in
    //  DEVNOTES, und genau daran hat sich diese Funktion bis
    //  0.1.7 nicht gehalten. Sichtbar wurde es so: die Meshes
    //  standen sauber, das Skelett lag flach am Boden.
    //
    //  Nachgemessen an 039_skin8.model: die inversen
    //  Bindmatrizen tragen eine Skalierung von 0.592375, die
    //  Knotendaten eine von 1.0. Das Skelett kam also um Faktor
    //  1/0.592 = 1.69 zu klein heraus, zusaetzlich zur falschen
    //  Lage.
    //
    //  inv(IBM) ist die WELTmatrix des Bones zum Bindzeitpunkt -
    //  direkt, ohne Aufsummieren ueber die Elternkette. Damit
    //  haengt sie weder an der Reihenfolge noch an der Frage,
    //  wie die lokalen Werte verkettet gehoeren.
    //
    //  Nur Knoten OHNE Paletteneintrag (Effekte, Partikel,
    //  Hilfsknoten) bekommen weiterhin meta[5]. Fuer die gibt es
    //  keine Bindmatrix, und ihre Lage ist ohnehin die des
    //  autoPlay-Frames.
    // ------------------------------------------------------------
    // Ruhelagen im meta[5]-Raum - werden fuer die
    // Mesh-Transformation der Bindmatrizen gebraucht (siehe
    // unten) und spaeter fuer das Retargeting.
    std::vector<int> parents0(file.nodes.size());
    std::transform(file.nodes.begin(), file.nodes.end(), parents0.begin(),
                   [](const ModelNode& node) { return node.parent; });
    restWorld_.assign(file.nodes.size(), Matrix44());
    for (int index : ParentsFirst(parents0)) {
        const size_t n = static_cast<size_t>(index);
        const ModelNode& src = file.nodes[n];
        const Matrix44 local = MatFromQuatPosScale(
            src.rotation, src.translation, src.scale);
        restWorld_[n] = (src.parent >= 0 &&
                         static_cast<size_t>(src.parent) < restWorld_.size())
                            ? MatMul(local, restWorld_[static_cast<size_t>(src.parent)])
                            : local;
    }

    std::vector<Matrix44> bindWorld(file.nodes.size());
    std::vector<char> hasBind(file.nodes.size(), 0);
    int singular = 0;

    // Immer MESSEN (die Zahl im Protokoll ist die Diagnose),
    // aber nur anwenden, wenn die Option es sagt.
    // Massstab nur MESSEN und melden. 0.1.8 hat ihn NICHT
    // angewandt - weder auf die Ruhelage noch auf die Animation -
    // und war damit der beste Stand. Die Zahl ist eine Diagnose,
    // keine Korrektur.
    bindScale_ = 1.0f;
    paletteBindScale_.assign(file.palettes.size(), 1.0f);
    for (size_t p = 0; p < file.palettes.size(); ++p) {
        std::vector<float> own;
        for (const Matrix44& m : file.palettes[p].invBind) {
            const float len = std::sqrt(m.m[0][0] * m.m[0][0] +
                                        m.m[0][1] * m.m[0][1] +
                                        m.m[0][2] * m.m[0][2]);
            if (len > 1e-6f) own.push_back(len);
        }
        if (own.empty()) continue;
        const size_t middle = own.size() / 2;
        std::nth_element(own.begin(), own.begin() + middle, own.end());
        if (own[middle] > 1e-6f) paletteBindScale_[p] = own[middle];
        Note(Msg() << L"  Palette " << p << L": Bindfaktor "
                   << paletteBindScale_[p] << L" (" << own.size() << L" Bones)");
    }

    // Je Knoten den Faktor seiner Palette merken. Knoten ohne
    // eigenen Eintrag (Wurzeln, Effekte) bekommen ihn spaeter von
    // ihren Nachkommen.
    nodeBindScale_.assign(file.nodes.size(), 0.0f);
    for (size_t p = 0; p < file.palettes.size(); ++p) {
        for (int global : file.palettes[p].bones) {
            if (global < 0 || static_cast<size_t>(global) >= nodeBindScale_.size()) {
                continue;
            }
            if (nodeBindScale_[static_cast<size_t>(global)] == 0.0f) {
                nodeBindScale_[static_cast<size_t>(global)] = paletteBindScale_[p];
            }
        }
    }
    // Von den Kindern nach oben durchreichen: eine Wurzel erbt den
    // Faktor ihres Teilbaums.
    for (int index : ParentsFirst(parents0)) {
        const size_t n = static_cast<size_t>(index);
        if (nodeBindScale_[n] == 0.0f) continue;
        int at = file.nodes[n].parent;
        while (at >= 0 && static_cast<size_t>(at) < nodeBindScale_.size() &&
               nodeBindScale_[static_cast<size_t>(at)] == 0.0f) {
            nodeBindScale_[static_cast<size_t>(at)] = nodeBindScale_[n];
            at = file.nodes[static_cast<size_t>(at)].parent;
        }
    }

    // Diagnose: welche Knoten sind Wurzeln, und welchen
    // Bindfaktor bekommt ihr Teilbaum? Das ist die Zahl, mit der
    // animRootFix rechnet.
    for (size_t n = 0; n < file.nodes.size(); ++n) {
        if (file.nodes[n].parent >= 0) continue;
        Note(Msg() << L"  Wurzel '" << Utf8ToWide(file.nodes[n].name)
                   << L"': Bindfaktor "
                   << (nodeBindScale_[n] > 1e-6f ? nodeBindScale_[n] : 1.0f)
                   << (nodeBindScale_[n] > 1e-6f ? L"" : L" (kein Eintrag im Teilbaum)"));
    }

    if (options_.bindScaleOnly) {
        std::vector<float> lengths;
        for (const Palette& palette : file.palettes) {
            for (const Matrix44& m : palette.invBind) {
                const float len = std::sqrt(m.m[0][0] * m.m[0][0] +
                                            m.m[0][1] * m.m[0][1] +
                                            m.m[0][2] * m.m[0][2]);
                if (len > 1e-6f) lengths.push_back(len);
            }
        }
        if (!lengths.empty()) {
            const size_t middle = lengths.size() / 2;
            std::nth_element(lengths.begin(), lengths.begin() + middle, lengths.end());
            if (lengths[middle] > 1e-6f) bindScale_ = 1.0f / lengths[middle];
            Note(Msg() << L"Massstab Bindraum/Knotenraum: " << bindScale_
                       << L" (aus " << lengths.size() << L" Bindmatrizen)");
        }
    }

    if (options_.matchBindScale) {
        MeasureBindFix(file);
    } else {
        paletteFix_.clear();
        paletteFixValid_.clear();
        nodePalette_.assign(file.nodes.size(), -1);
    }

    // ------------------------------------------------------------
    //  Die Bindmatrix enthaelt die MESH-Transformation mit.
    //
    //  Unity rechnet die Bindpose so (Doku zu Mesh.bindposes):
    //
    //      bindPose = bone.worldToLocalMatrix
    //               * renderer.transform.localToWorldMatrix
    //
    //  Sie ist also NICHT inv(boneWorld), sondern enthaelt
    //  zusaetzlich die Weltmatrix des Objekts, an dem der Skin
    //  haengt. In Zeilenvektor-Schreibweise:
    //
    //      IBM      = meshWorld * inv(boneWorld)
    //      boneWorld = inv(IBM) * meshWorld
    //
    //  Genau das erklaert alle Messungen: der Knoten
    //  "039_skin8", an dem die Instanz haengt, traegt im
    //  Szenendump Skalierung 0.592375 - den Kehrwert von
    //  1.688118 - und dazu eine Drehung um -90 Grad um X. Das
    //  ist die gesuchte Korrektur, und sie steckt schon in den
    //  Daten; sie musste nur aus der richtigen Quelle kommen.
    //
    //  Deshalb ist sie je Palette konstant (eine Palette gehoert
    //  zu einem Mesh) und zwischen Paletten verschieden (andere
    //  Mesh-Knoten) - was die Palettenmessung genau so gezeigt
    //  hatte.
    // ------------------------------------------------------------
    paletteMeshWorld_.assign(file.palettes.size(), Matrix44());
    paletteHasMesh_.assign(file.palettes.size(), 0);
    std::vector<Matrix44>& paletteMeshWorld = paletteMeshWorld_;
    std::vector<char>& paletteHasMesh = paletteHasMesh_;

    for (const Instance& instance : file.instances) {
        if (instance.palette < 0 ||
            static_cast<size_t>(instance.palette) >= file.palettes.size()) {
            continue;
        }
        if (instance.node < 0 ||
            static_cast<size_t>(instance.node) >= restWorld_.size()) {
            continue;
        }
        const size_t p = static_cast<size_t>(instance.palette);
        if (paletteHasMesh[p]) continue;

        paletteMeshWorld[p] = restWorld_[static_cast<size_t>(instance.node)];
        paletteHasMesh[p] = 1;
    }

    if (options_.bindPoseFromInvBind)
    for (size_t pi = 0; pi < file.palettes.size(); ++pi) {
        const Palette& palette = file.palettes[pi];
        for (size_t b = 0; b < palette.bones.size(); ++b) {
            const int global = palette.bones[b];
            if (global < 0 || static_cast<size_t>(global) >= file.nodes.size()) {
                continue;
            }
            if (b >= palette.invBind.size()) continue;
            if (hasBind[static_cast<size_t>(global)]) continue;

            Matrix44 world;
            if (!MatInvert(palette.invBind[b], world)) {
                ++singular;
                continue;
            }

            // ... und die Mesh-Transformation wieder hinein.
            // Mit engineSpace immer: die IBM sind relativ zum
            // Mesh-Knoten gespeichert (siehe ImportOptions).
            if ((options_.bindIncludesMesh || options_.engineSpace) &&
                paletteHasMesh[pi]) {
                world = MatMul(world, paletteMeshWorld[pi]);
            }

            bindWorld[static_cast<size_t>(global)] = world;
            hasBind[static_cast<size_t>(global)] = 1;
        }
    }

    if (singular > 0) {
        Warn(Msg() << singular << L" nicht invertierbare Bind-Matrix/Matrizen - "
                   << L"diese Knoten stehen auf ihrer Ruhelage aus meta[5].");
    }

    const int fromBind = static_cast<int>(
        std::count(hasBind.begin(), hasBind.end(), 1));
    Note(Msg() << L"Bind-Pose: " << fromBind << L" von " << file.nodes.size()
               << L" Knoten aus den inversen Bindmatrizen, "
               << (static_cast<int>(file.nodes.size()) - fromBind)
               << L" aus meta[5]");

    // ------------------------------------------------------------
    //  ZWEI RAEUME IN EINER SZENE
    //
    //  Das ist der Kern des verbleibenden Problems: die Knoten MIT
    //  Bindmatrix stehen im Bindraum, die OHNE im Knotenraum - und
    //  die Animation setzt spaeter ALLE aus den meta[7]-Kanaelen,
    //  also im Knotenraum. Ab dem ersten Animationsframe springen
    //  die ersten in den zweiten Raum.
    //
    //  Sichtbar wird das an der Ausdehnung: bei Frame 0 reicht Y
    //  weit (die Knoten aus meta[5], Y-up), Z dagegen nur so weit
    //  wie der Charakter hoch ist (die Knoten aus den
    //  Bindmatrizen, Z-up).
    // ------------------------------------------------------------
    if (fromBind > 0 && fromBind < static_cast<int>(file.nodes.size())) {
        Warn(Msg() << L"Die Szene enthaelt ZWEI Raeume: " << fromBind
                   << L" Knoten aus den Bindmatrizen und "
                   << (static_cast<int>(file.nodes.size()) - fromBind)
                   << L" aus meta[5]. Die Animation kommt fuer ALLE aus "
                   << L"meta[7] - deshalb springt es beim ersten "
                   << L"Animationsframe.");
    }

    // ------------------------------------------------------------
    //  RETARGETING der Animation
    //
    //  Die Ruhelage kommt aus den Bindmatrizen, die Animation aus
    //  den meta[7]-Kanaelen - zwei verschiedene Raeume. Genau
    //  deshalb steht Frame 0 richtig und kippt die Animation um
    //  90 Grad weg.
    //
    //  Die Loesung ist NICHT eine gemeinsame Korrekturmatrix -
    //  die gibt es nicht, das haben die Messungen gezeigt.
    //  Sondern JE BONE die Abbildung von seiner meta[5]-Ruhelage
    //  auf seine Bind-Lage:
    //
    //      R[i] = bindWorld[i] * inv(restWorld[i])
    //
    //  In der Animation wird dann gerechnet:
    //
    //      animWorldNeu[i] = R[i] * animWorld[i]
    //
    //  Steht der Bone auf seiner Ruhelage (anim == rest), kommt
    //  genau bindWorld heraus - Frame 0 bleibt also so richtig,
    //  wie er jetzt ist. Bewegt er sich, wird dieselbe Bewegung
    //  auf die Bind-Lage uebertragen.
    //
    //  Dass R je Bone anders ist, ist kein Widerspruch: es IST
    //  die Matrix, die die erste Messung "von links" ergeben hat.
    //  Sie war nicht falsch gemessen - sie ist als globale
    //  Korrektur nur sinnlos und als Retargeting genau richtig.
    // ------------------------------------------------------------
    retarget_.assign(file.nodes.size(), Matrix44());
    hasRetarget_.assign(file.nodes.size(), 0);

    // ------------------------------------------------------------
    //  bindIncludesMesh und retargetAnimation SCHLIESSEN SICH AUS.
    //
    //  bindIncludesMesh rechnet die Mesh-Transformation in die
    //  Bindmatrix zurueck - danach FALLEN Bind-Pose und Ruhelage
    //  zusammen (nachgemessen: Abweichung 0.00006 ueber 91 Bones).
    //  Das Retargeting bildet dann eine Differenz ab, die es nicht
    //  mehr gibt: rechnerisch die Einheitsmatrix, praktisch das
    //  Rundungsrauschen von 176 Matrixinversionen, das sich ueber
    //  die Hierarchie aufschaukelt.
    //
    //  Die Pruefung steht hier und nicht nur in der Oberflaeche:
    //  die API ist auch aus Skripten aufrufbar, und eine
    //  Kombination, die das Ergebnis zerlegt, darf gar nicht erst
    //  zustande kommen.
    // ------------------------------------------------------------
    if (options_.bindIncludesMesh && options_.retargetAnimation) {
        Warn(L"'Retarget animation' wird ignoriert: mit "
             L"'Bind matrix includes mesh transform' fallen Bind-Pose und "
             L"Ruhelage zusammen, es gibt nichts zu uebertragen.");
        options_.retargetAnimation = false;
    }

    std::vector<int> parents(file.nodes.size());
    std::transform(file.nodes.begin(), file.nodes.end(), parents.begin(),
                   [](const ModelNode& node) { return node.parent; });

    if (options_.retargetAnimation) {
        int built = 0;
        for (size_t n = 0; n < file.nodes.size(); ++n) {
            if (!hasBind[n]) continue;
            Matrix44 restInv;
            if (!MatInvert(restWorld_[n], restInv)) continue;
            // VON RECHTS: bindWorld = restWorld * C, also
            //     C = inv(restWorld) * bindWorld
            //
            // In 0.1.17 stand hier MatMul(bindWorld, restInv) -
            // die andere Seite. Das war derselbe Fehler wie ganz
            // am Anfang, nur spiegelbildlich: die Messung hatte
            // gezeigt, dass die Beziehung von RECHTS konstant ist
            // (fuer Palette 0 alle 91 Bones identisch), von links
            // dagegen je Bone anders.
            //
            // Bei anim == rest kommt rest * inv(rest) * bind =
            // bind heraus - Frame 0 bleibt also unveraendert.
            retarget_[n] = MatMul(restInv, bindWorld[n]);
            hasRetarget_[n] = 1;
            ++built;
        }

        Note(Msg() << L"Retargeting: " << built << L" Bones bekommen die "
                   << L"Animation auf ihre Bind-Lage uebertragen");

        // Die Matrix eines Beispielbones ins Protokoll. Ist sie
        // annaehernd die Einheitsmatrix, liegen beide Raeume schon
        // aufeinander und das Retargeting kann nichts bewirken.
        for (size_t n = 0; n < hasRetarget_.size(); ++n) {
            if (!hasRetarget_[n]) continue;
            const Matrix44& c = retarget_[n];
            Note(Msg() << L"  Beispiel '" << Utf8ToWide(file.nodes[n].name)
                       << L"': [" << c.m[0][0] << L" " << c.m[0][1] << L" "
                       << c.m[0][2] << L"] [" << c.m[1][0] << L" " << c.m[1][1]
                       << L" " << c.m[1][2] << L"] [" << c.m[2][0] << L" "
                       << c.m[2][1] << L" " << c.m[2][2] << L"]");
            break;
        }
    }

    // Lokale Transformation setzen, NACHDEM die Hierarchie steht -
    // und in der Reihenfolge ELTERN VOR KINDERN.
    //
    // SetNodeTM arbeitet in Weltkoordinaten. Ein Kind, das gesetzt
    // wird, bevor sein Elternteil steht, wird danach vom
    // Elternteil noch einmal mitgenommen. Siehe ParentsFirst().
    for (int index : ParentsFirst(parents)) {
        const size_t i = static_cast<size_t>(index);
        const ModelNode& source = file.nodes[i];
        if (nodes_[i] == nullptr) continue;

        Matrix3 tm;

        if (hasBind[i]) {
            // Weltmatrix direkt aus inv(IBM) - nichts zu
            // akkumulieren.
            tm = ToMatrix3(MaxSpace(bindWorld[i], options_.engineSpace),
                           options_.scale);
        } else {
            // Kein Bindeintrag: lokale Werte aus meta[5], ueber
            // die Elternkette in den Weltraum gerechnet.
            Vec3 ruheScale = source.scale;
            if (options_.ignoreZeroScale) {
                const float grenze = options_.zeroScaleThreshold;
                if (std::fabs(ruheScale.x) <= grenze &&
                    std::fabs(ruheScale.y) <= grenze &&
                    std::fabs(ruheScale.z) <= grenze) {
                    ruheScale.x = ruheScale.y = ruheScale.z = 1.0f;
                    ++zeroScaleFixed_;
                }
            }

            const Matrix44 local = MatFromQuatPosScale(
                source.rotation, source.translation, ruheScale);

// WIE IN 0.1.8: die Elternkette kommt aus der SZENE,
            // nicht aus eigenen Matrizen.
            //
            // Das ist kein Schoenheitsfehler, sondern DER
            // Unterschied: der Elternteil steht in der Szene
            // schon auf seiner BIND-Lage, wenn er eine
            // Bindmatrix hatte. Ein Kind ohne eigene Bindmatrix
            // erbt sie dadurch. Baut man die Kette stattdessen
            // rein aus meta[5] auf, geht genau diese Vererbung
            // verloren - und die Knoten ohne Bindeintrag
            // (Effekte, Waffen) landen woanders.
            tm = ToMatrix3(local, options_.scale);
            INode* parent = nodes_[i]->GetParentNode();
            if (parent != nullptr) {
                Matrix3 parentTm = parent->GetNodeTM(0);
                tm = tm * parentTm;
            } else if (options_.engineSpace) {
                // Wurzel: Engine-Raum -> Max, wie alle anderen
                // Weltmatrizen. Die Kinder erben es ueber parentTm.
                tm = tm * ToMatrix3(EngineToMaxMatrix(), 1.0f);
            }

            // KEINE Achsendrehung in der Ruhelage.
            //
            // Das Blender-Addon dreht ausdruecklich nur den
            // CLIP-Raum ("rotating the clip space +90 deg about X
            // so the animated character stands upright like the
            // rest pose"). Die Ruhelage steht schon richtig - sie
            // kommt aus den Bindmatrizen.
            //
            // Sie hier ebenfalls zu drehen war der Fehler in
            // 0.1.14: dann drehen sich beide, und der Unterschied
            // zwischen ihnen bleibt genau derselbe.
        }

        nodes_[i]->SetNodeTM(0, tm);
        const Point3 at = tm.GetTrans();
        nodeWorldPos_.push_back(Vec3{at.x, at.y, at.z});

        if (source.implausibleScale) {
            Warn(Msg() << L"Knoten '" << Utf8ToWide(source.name)
                       << L"': unplausible Skalierung in der Datei, "
                       << L"unveraendert uebernommen.");
        }
    }
}

// ============================================================
//  .model - Meshes
// ============================================================

// ------------------------------------------------------------
//  Massstab fuer die Mesh-Vertices
//
//  options_.scale ist die Benutzereinstellung. Dazu kommt der
//  Kehrwert des gemessenen Bindfaktors: in den Bindmatrizen
//  steckt 0.592376, in den Knotendaten 1.0 - die Vertices leben
//  im Bindraum und muessen auf die Groesse der Knoten gebracht
//  werden.
//
//  Bei einer Datei ohne Massstabsunterschied ist bindScale_ 1.0
//  und diese Funktion gibt einfach options_.scale zurueck.
// ------------------------------------------------------------
float SceneBuilder::MeshScale(int palette) const {
    float factor = options_.scale;
    if (!options_.meshBindScale) return factor;

    // Den Faktor DIESER Palette nehmen, nicht den globalen
    // Median: die beiden Charaktere in 039_skin8.model haben
    // verschiedene (0.592376 und 1.000000).
    if (palette >= 0 && static_cast<size_t>(palette) < paletteBindScale_.size()) {
        const float own = paletteBindScale_[static_cast<size_t>(palette)];
        if (own > 1e-6f) return factor / own;
    }

    // Starres Mesh oder Palette ohne Bindmatrizen: unskaliert.
    // Es haengt an seinem Knoten und braucht keine Umrechnung.
    return factor;
}

void SceneBuilder::BuildModelMeshes(const ModelFile& file) {
    builtMeshes_.clear();

    for (size_t instanceIndex = 0; instanceIndex < file.instances.size();
         ++instanceIndex) {
        const Instance& instance = file.instances[instanceIndex];
        if (instance.group < 0 ||
            static_cast<size_t>(instance.group) >= file.groups.size()) {
            continue;
        }
        if (instance.node < 0 ||
            static_cast<size_t>(instance.node) >= nodes_.size()) {
            continue;
        }

        const std::vector<Submesh>& group =
            file.groups[static_cast<size_t>(instance.group)];

        for (size_t s = 0; s < group.size(); ++s) {
            const Submesh& submesh = group[s];

            if (submesh.positions.empty() || submesh.indices.empty()) continue;

            if (!IndicesInRange(submesh.indices, submesh.positions.size())) {
                Warn(Msg() << L"Submesh " << instance.group << L"/" << s
                           << L": Indices ausserhalb des Vertexbereichs, "
                           << L"uebersprungen.");
                continue;
            }

            TriObject* tri = CreateNewTriObject();
            if (tri == nullptr) {
                Warn(Msg() << L"Submesh " << instance.group << L"/" << s
                           << L": TriObject liess sich nicht anlegen.");
                continue;
            }

            Mesh& mesh = tri->GetMesh();
            const int vertexCount = static_cast<int>(submesh.positions.size());
            const int faceCount = static_cast<int>(submesh.indices.size() / 3);

            mesh.setNumVerts(vertexCount);
            mesh.setNumFaces(faceCount);

            for (int v = 0; v < vertexCount; ++v) {
                const Vec3& p = submesh.positions[static_cast<size_t>(v)];
                const float ms = MeshScale(instance.palette);
                // -90 Grad um X: (x, y, z) -> (x, z, -y)
                //
                // Am Modell bestimmt, nicht hergeleitet: mit
                // "Use Time 0 as Reference Pose" stehen Skelett
                // und Animation richtig, das Mesh liegt flach,
                // und -90 um X stellt es auf.
                mesh.setVert(v, (options_.meshAxisFix && !options_.engineSpace)
                                    ? Point3(p.x * ms, p.z * ms, -p.y * ms)
                                    : ToPoint3(p, ms));
            }

            for (int f = 0; f < faceCount; ++f) {
                const size_t base = static_cast<size_t>(f) * 3;
                mesh.faces[f].setVerts(
                    static_cast<int>(submesh.indices[base]),
                    static_cast<int>(submesh.indices[base + 1]),
                    static_cast<int>(submesh.indices[base + 2]));
                mesh.faces[f].setEdgeVisFlags(1, 1, 1);
                mesh.faces[f].smGroup = 1;
            }

            // UV-Kanaele. Kanal 1 ist UV0, Kanal 2 ist UV1 - Max
            // zaehlt Mapkanaele ab 1, Kanal 0 waere die
            // Vertexfarbe.
            const bool hasUv0 = submesh.uv0.size() == submesh.positions.size();
            const bool hasUv1 = submesh.uv1.size() == submesh.positions.size();
            const bool hasColor = submesh.colors.size() == submesh.positions.size();

            mesh.setNumMaps(hasUv1 ? 3 : 2, TRUE);

            if (hasUv0) {
                mesh.setMapSupport(1, TRUE);
                mesh.setNumMapVerts(1, vertexCount);
                mesh.setNumMapFaces(1, faceCount);
                for (int v = 0; v < vertexCount; ++v) {
                    const Vec2& uv = submesh.uv0[static_cast<size_t>(v)];
                    // V umdrehen: DirectX zaehlt V von oben, Max von
                    // unten. Siehe ImportOptions::flipUvV.
                    mesh.setMapVert(1, v, Point3(uv.x,
                                                 options_.flipUvV ? 1.0f - uv.y
                                                                  : uv.y,
                                                 0.0f));
                }
                for (int f = 0; f < faceCount; ++f) {
                    const size_t base = static_cast<size_t>(f) * 3;
                    mesh.mapFaces(1)[f].setTVerts(
                        static_cast<int>(submesh.indices[base]),
                        static_cast<int>(submesh.indices[base + 1]),
                        static_cast<int>(submesh.indices[base + 2]));
                }
            }

            if (hasUv1) {
                mesh.setMapSupport(2, TRUE);
                mesh.setNumMapVerts(2, vertexCount);
                mesh.setNumMapFaces(2, faceCount);
                for (int v = 0; v < vertexCount; ++v) {
                    const Vec2& uv = submesh.uv1[static_cast<size_t>(v)];
                    mesh.setMapVert(2, v, Point3(uv.x, uv.y, 0.0f));
                }
                for (int f = 0; f < faceCount; ++f) {
                    const size_t base = static_cast<size_t>(f) * 3;
                    mesh.mapFaces(2)[f].setTVerts(
                        static_cast<int>(submesh.indices[base]),
                        static_cast<int>(submesh.indices[base + 1]),
                        static_cast<int>(submesh.indices[base + 2]));
                }
            }

            if (hasColor) {
                mesh.setMapSupport(0, TRUE);
                mesh.setNumMapVerts(0, vertexCount);
                mesh.setNumMapFaces(0, faceCount);
                for (int v = 0; v < vertexCount; ++v) {
                    const Vec4& c = submesh.colors[static_cast<size_t>(v)];
                    mesh.setMapVert(0, v, Point3(c.x, c.y, c.z));
                }
                for (int f = 0; f < faceCount; ++f) {
                    const size_t base = static_cast<size_t>(f) * 3;
                    mesh.mapFaces(0)[f].setTVerts(
                        static_cast<int>(submesh.indices[base]),
                        static_cast<int>(submesh.indices[base + 1]),
                        static_cast<int>(submesh.indices[base + 2]));
                }
            }

            mesh.InvalidateTopologyCache();
            mesh.buildNormals();

            INode* node = ip_->CreateObjectNode(tri);
            if (node == nullptr) {
                Warn(Msg() << L"Submesh " << instance.group << L"/" << s
                           << L": Knoten liess sich nicht anlegen.");
                continue;
            }

            const std::wstring name =
                Utf8ToWide(submesh.Name()).empty()
                    ? (L"mesh_" + std::to_wstring(instance.group) + L"_" +
                       std::to_wstring(s))
                    : Utf8ToWide(submesh.Name());
            node->SetName(const_cast<MCHAR*>(name.c_str()));

            // Ein geskinntes Mesh bleibt an der Wurzel - und muss
            // deshalb die Achsenkorrektur SELBST tragen. Sonst
            // laegen seine Vertices im Y-up-Raum, waehrend die
            // Bones schon gedreht sind.
            if (options_.engineSpace) {
                // Engine-Raum: die Vertices sind lokal zum
                // Mesh-Knoten (dorthin zeigen auch die IBM). Das
                // Objekt steht also auf der Weltlage des
                // Mesh-Knotens - gehaeutet wie starr.
                //
                // Gehaeutet: aus meta[5], dieselbe Matrix, die in
                // die Bind-Pose der Bones eingeht (paletteMeshWorld).
                // Starr: auf der Lage des Traegers in der Szene, der
                // es weiter unten als Kind bekommt und es dann durch
                // die Animation mitnimmt (Welt(t) * v wie in der
                // Engine).
                Matrix3 meshTm;
                if (instance.palette >= 0 &&
                    static_cast<size_t>(instance.node) < restWorld_.size()) {
                    meshTm = ToMatrix3(
                        MaxSpace(restWorld_[static_cast<size_t>(instance.node)], true),
                        options_.scale);
                } else if (nodes_[static_cast<size_t>(instance.node)] != nullptr) {
                    meshTm = nodes_[static_cast<size_t>(instance.node)]->GetNodeTM(0);
                } else {
                    meshTm = ToMatrix3(EngineToMaxMatrix(), 1.0f);
                }
                node->SetNodeTM(0, meshTm);
            } else if (instance.palette >= 0 && options_.upAxisFix) {
                Matrix3 meshTm = ToMatrix3(UpAxisMatrix(), 1.0f);
                node->SetNodeTM(0, meshTm);
            }

            // ------------------------------------------------
            //  Ein starres Mesh wird nur VERKETTET, nicht
            //  transformiert.
            //
            //  Die Vertices stehen bereits im Modellraum, also
            //  absolut. Dem Knoten zusaetzlich die Matrix seines
            //  Traegers zu geben, wendet dieselbe Transformation
            //  ein zweites Mal an - das Mesh wandert dann um
            //  genau diesen Betrag davon.
            //
            //  Das Blender-Addon macht es genauso: es legt die
            //  Vertices unveraendert ab und haengt das Objekt nur
            //  in die Hierarchie.
            //
            //  AttachChild mit keepTM = 1 haelt die Weltlage des
            //  Kindes fest, waehrend es den Elternteil bekommt -
            //  das ist hier die richtige Wahl, nicht 0.
            //
            //  NACHGEMESSEN: das Hauptmesh g0_s0 hat Vertices von
            //  Z = 0.00 bis 3.46, die Bone-Positionen aus
            //  inv(IBM) reichen bis Z = 3.05. Beide liegen also
            //  im SELBEN Raum - die Bind-Pose war nie falsch.
            //
            //  Was auseinanderlief, waren die starren Meshes: ein
            //  Traeger mit Skalierung 0.592 hat sein Mesh um
            //  genau diesen Faktor verschoben und verkleinert,
            //  obwohl dessen Vertices schon absolut standen. Im
            //  Viewport sah das aus, als seien die Bones zu
            //  klein - tatsaechlich waren einzelne Meshes zu
            //  gross und an der falschen Stelle.
            // ------------------------------------------------
            if (instance.palette < 0) {
                INode* carrier = nodes_[static_cast<size_t>(instance.node)];
                if (carrier != nullptr) carrier->AttachChild(node, 1);
            }

            if (submesh.material >= 0 &&
                static_cast<size_t>(submesh.material) < materials_.size()) {
                Mtl* mtl = materials_[static_cast<size_t>(submesh.material)];
                if (mtl != nullptr) node->SetMtl(mtl);
            }

            if (options_.verbose) {
                Note(Msg() << L"  Mesh '" << name << L"': Instanz "
                           << instanceIndex << L", Palette " << instance.palette
                           << L", Massstab " << MeshScale(instance.palette)
                           << L", " << vertexCount << L" Vertices");
            }

            BuiltMesh built;
            built.node = node;
            built.instance = static_cast<int>(instanceIndex);
            built.submesh = static_cast<int>(s);
            builtMeshes_.push_back(built);

            ++stats_.meshes;
            stats_.vertices += vertexCount;
            stats_.faces += faceCount;
        }
    }
}

// ============================================================
//  .model - Figuren fuer die Layer
//
//  Die Oberflaeche sortiert in "<Figur> Bones" / "<Figur> Mesh".
//  Bis 0.3.12 war die Figur der oberste Vorfahr eines Objekts -
//  das gab zwei Fehler, die man im Layer-Explorer sah:
//
//    - gehaeutete Meshes haengen an der Wurzel, jedes bekam einen
//      eigenen Layer ("g0_s0 Mesh", "g1_s0 Mesh" ...);
//    - 039_skin8.model hat ZWEI Figuren unter einem Dateiknoten,
//      beide landeten in "039_skin8 Bones".
//
//  Hier wird die Figur aus den Daten bestimmt: jeder Knoten zaehlt
//  zu dem Teilbaum direkt unter der Wurzel, in dem er steht. Eine
//  Palette verbindet die Teilbaeume ihrer Bones mit dem Teilbaum
//  ihres Mesh-Knotens (Union-Find). Was dabei zusammenhaengt, ist
//  eine Figur; benannt wird sie nach dem Mesh-Knoten mit den
//  meisten Vertices (039_skin8.model: "039_skin8" und
//  "144_skin10"). Teilbaeume ohne Mesh (Effektknoten) kommen zur
//  groessten Figur.
//
//  Ergebnis: Benutzereigenschaft "eg3d_model" an jedem Knoten und
//  Mesh. Das MacroScript liest sie, fehlt sie (.x), gilt weiter der
//  oberste Vorfahr.
// ============================================================
void SceneBuilder::TagModelGroups(const ModelFile& file) {
    const size_t count = file.nodes.size();
    if (count == 0) return;

    // Teilbaum direkt unter der Wurzel (die Wurzel selbst: sie selbst).
    std::vector<int> branch(count, -1);
    for (size_t n = 0; n < count; ++n) {
        int at = static_cast<int>(n);
        int guard = 0;
        while (file.nodes[static_cast<size_t>(at)].parent >= 0 &&
               file.nodes[static_cast<size_t>(file.nodes[static_cast<size_t>(at)].parent)]
                       .parent >= 0 &&
               ++guard < 4096) {
            at = file.nodes[static_cast<size_t>(at)].parent;
        }
        branch[n] = at;
    }

    std::vector<int> link(count);
    for (size_t n = 0; n < count; ++n) link[n] = static_cast<int>(n);
    auto find = [&](int x) {
        while (link[static_cast<size_t>(x)] != x) {
            link[static_cast<size_t>(x)] = link[static_cast<size_t>(link[static_cast<size_t>(x)])];
            x = link[static_cast<size_t>(x)];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = find(a); b = find(b);
        if (a != b) link[static_cast<size_t>(b)] = a;
    };

    for (const Instance& instance : file.instances) {
        if (instance.node < 0 || static_cast<size_t>(instance.node) >= count) continue;
        if (instance.palette < 0 ||
            static_cast<size_t>(instance.palette) >= file.palettes.size()) continue;
        const int own = branch[static_cast<size_t>(instance.node)];
        for (int bone : file.palettes[static_cast<size_t>(instance.palette)].bones) {
            if (bone >= 0 && static_cast<size_t>(bone) < count)
                unite(own, branch[static_cast<size_t>(bone)]);
        }
    }

    // Je Gruppe: Vertices und der groesste Mesh-Knoten als Name.
    std::map<int, int> verts;
    std::map<int, std::pair<int, int>> best;   // Gruppe -> (Vertices, Knoten)
    for (const BuiltMesh& built : builtMeshes_) {
        if (built.instance < 0 ||
            static_cast<size_t>(built.instance) >= file.instances.size()) continue;
        const Instance& instance = file.instances[static_cast<size_t>(built.instance)];
        if (instance.node < 0 || static_cast<size_t>(instance.node) >= count) continue;
        const int group = find(branch[static_cast<size_t>(instance.node)]);
        int v = 0;
        if (instance.group >= 0 && static_cast<size_t>(instance.group) < file.groups.size() &&
            built.submesh >= 0 &&
            static_cast<size_t>(built.submesh) <
                file.groups[static_cast<size_t>(instance.group)].size()) {
            v = static_cast<int>(file.groups[static_cast<size_t>(instance.group)]
                                     [static_cast<size_t>(built.submesh)].positions.size());
        }
        verts[group] += v;
        std::pair<int, int>& slot = best[group];
        if (v > slot.first) slot = std::make_pair(v, instance.node);
    }

    int largest = -1;
    int largestVerts = -1;
    for (const auto& entry : verts) {
        if (entry.second > largestVerts) { largestVerts = entry.second; largest = entry.first; }
    }

    auto nameOf = [&](int group) -> std::wstring {
        auto it = best.find(group);
        if (it == best.end() || it->second.first <= 0) {
            if (largest >= 0) {
                auto big = best.find(largest);
                if (big != best.end())
                    return Utf8ToWide(file.nodes[static_cast<size_t>(big->second.second)].name);
            }
            // Gar kein Mesh: der Name der Wurzel.
            int root = group;
            while (file.nodes[static_cast<size_t>(root)].parent >= 0)
                root = file.nodes[static_cast<size_t>(root)].parent;
            return Utf8ToWide(file.nodes[static_cast<size_t>(root)].name);
        }
        return Utf8ToWide(file.nodes[static_cast<size_t>(it->second.second)].name);
    };

    int tagged = 0;
    std::map<std::wstring, int> perModel;
    for (size_t n = 0; n < count && n < nodes_.size(); ++n) {
        if (nodes_[n] == nullptr) continue;
        const std::wstring model = nameOf(find(branch[n]));
        nodes_[n]->SetUserPropString(_T("eg3d_model"), model.c_str());
        ++perModel[model];
        ++tagged;
    }
    for (const BuiltMesh& built : builtMeshes_) {
        if (built.node == nullptr || built.instance < 0 ||
            static_cast<size_t>(built.instance) >= file.instances.size()) continue;
        const Instance& instance = file.instances[static_cast<size_t>(built.instance)];
        if (instance.node < 0 || static_cast<size_t>(instance.node) >= count) continue;
        const std::wstring model = nameOf(find(branch[static_cast<size_t>(instance.node)]));
        built.node->SetUserPropString(_T("eg3d_model"), model.c_str());
        ++tagged;
    }

    Msg line;
    line << L"Figuren fuer die Layer: " << perModel.size();
    for (const auto& entry : perModel) line << L" | " << entry.first << L" (" << entry.second << L" Knoten)";
    Note(line);
}

// ============================================================
//  .model - Skin
//
//  Ueber ISkinImportData, NICHT ueber skinOps aus MAXScript.
//  skinOps arbeitet ueber das Modifikatorpanel: es braucht die
//  Auswahl im Panel, es zeichnet bei jedem Gewicht neu, und es
//  ist bei zwanzigtausend Vertices unbrauchbar langsam.
//  ISkinImportData braucht kein Panel.
// ============================================================

bool SceneBuilder::BuildModelSkin(const ModelFile& file) {
    if (ip_ == nullptr) return false;

    // Ueber die tatsaechlich gebauten Meshes laufen, nicht ueber
    // die Instanzliste. Jedes trägt seine Herkunft mit sich -
    // siehe BuiltMesh. Ein uebersprungenes Submesh kann die
    // Zuordnung damit nicht mehr verschieben.
    for (const BuiltMesh& built : builtMeshes_) {
        INode* node = built.node;
        if (node == nullptr) continue;
        if (built.instance < 0 ||
            static_cast<size_t>(built.instance) >= file.instances.size()) {
            continue;
        }

        const Instance& instance = file.instances[static_cast<size_t>(built.instance)];

        // Starres Mesh - haengt schon am Knoten.
        if (instance.palette < 0 ||
            static_cast<size_t>(instance.palette) >= file.palettes.size()) {
            continue;
        }
        if (instance.group < 0 ||
            static_cast<size_t>(instance.group) >= file.groups.size()) {
            continue;
        }

        const std::vector<Submesh>& group =
            file.groups[static_cast<size_t>(instance.group)];
        if (built.submesh < 0 ||
            static_cast<size_t>(built.submesh) >= group.size()) {
            continue;
        }

        const Submesh& submesh = group[static_cast<size_t>(built.submesh)];
        const Palette& palette = file.palettes[static_cast<size_t>(instance.palette)];

        if (submesh.boneIndices.empty() || submesh.weights.empty()) continue;

        Modifier* skinMod = static_cast<Modifier*>(
            ip_->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
        if (skinMod == nullptr) {
            Warn(L"Skin-Modifier liess sich nicht anlegen - "
                 L"Meshes bleiben ungeskinnt.");
            return false;
        }

        Object* baseObject = node->GetObjectRef();
        if (baseObject == nullptr) {
            Warn(Msg() << L"Mesh '" << node->GetName()
                       << L"': Knoten hat kein Objekt, kein Skin.");
            continue;
        }

        IDerivedObject* derived = nullptr;
        if (baseObject->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
            derived = static_cast<IDerivedObject*>(baseObject);
        } else {
            derived = CreateDerivedObject(baseObject);
            if (derived == nullptr) {
                Warn(L"Derived Object liess sich nicht anlegen - kein Skin.");
                continue;
            }
            node->SetObjectRef(derived);
        }
        derived->AddModifier(skinMod, nullptr, 0);

        ISkinImportData* skinImport = static_cast<ISkinImportData*>(
            skinMod->GetInterface(I_SKINIMPORTDATA));
        if (skinImport == nullptr) {
            Warn(L"ISkinImportData nicht verfuegbar - "
                 L"das Max-SDK meldet die Schnittstelle nicht.");
            continue;
        }

        // ---- Bones anmelden ----
        //
        // Nur die, die dieses Submesh wirklich benutzt. Eine
        // Palette kann 60 Bones haben, von denen ein Umhang drei
        // braucht.
        std::vector<bool> used(palette.bones.size(), false);
        for (size_t i = 0; i < submesh.boneIndices.size(); ++i) {
            if (i >= submesh.weights.size()) break;
            if (submesh.weights[i] <= 0.0f) continue;
            const size_t local = submesh.boneIndices[i];
            if (local < used.size()) used[local] = true;
        }

        std::vector<size_t> localBones;
        std::vector<INode*> boneNodes;
        for (size_t b = 0; b < used.size(); ++b) {
            if (!used[b]) continue;
            const int global = palette.bones[b];
            if (global < 0 || static_cast<size_t>(global) >= nodes_.size()) continue;
            if (nodes_[static_cast<size_t>(global)] == nullptr) continue;
            localBones.push_back(b);
            boneNodes.push_back(nodes_[static_cast<size_t>(global)]);
        }

        if (boneNodes.empty()) {
            Warn(Msg() << L"Mesh '" << node->GetName()
                       << L"': keine benutzten Bones, kein Skin.");
            continue;
        }

        // update = TRUE nur beim LETZTEN, sonst rechnet Max die
        // Gewichtstabelle bei jedem Bone neu durch.
        for (size_t i = 0; i < boneNodes.size(); ++i) {
            const bool last = (i + 1 == boneNodes.size());
            skinImport->AddBoneEx(boneNodes[i], last ? TRUE : FALSE);
        }

        // ---- Bind-Pose ----
        //
        // Die inverse Bind-Matrix steht in der Palette. Max will
        // die NICHT invertierte Matrix des Bones zum
        // Bindzeitpunkt, also wird zurueckinvertiert.
        const Matrix3 meshTm = node->GetNodeTM(0);
        for (size_t i = 0; i < boneNodes.size(); ++i) {
            const size_t b = localBones[i];
            if (b >= palette.invBind.size()) continue;

            // DIESELBE Rechnung wie bei den Knoten - sonst
            // bekommt der Skin-Modifier eine Bind-Lage aus einem
            // anderen Raum als die Bones, die er bewegt.
            //
            // Genau das war der Rest in 0.1.22: die Knoten
            // trugen inv(IBM) * meshWorld, SetSkinTm bekam nur
            // inv(IBM). Ein Unterschied von Massstab 1.688 und
            // 90 Grad - dieselbe Differenz, die vorher zwischen
            // Ruhelage und Animation stand, jetzt zwischen Bones
            // und Skin.
            Matrix44 bind;
            if (!MatInvert(palette.invBind[b], bind)) {
                // 263_skin5.model hat zwei davon. Max bekommt
                // dann die aktuelle Knotenmatrix - besser als NaN
                // durch die ganze Kette zu reichen.
                Warn(Msg() << L"Palette " << instance.palette << L" Bone " << b
                           << L": Bind-Matrix nicht invertierbar, "
                           << L"aktuelle Knotenlage benutzt.");
                continue;
            }

            // Dieselbe Matrix, die der Knoten schon traegt -
            // BuildModelNodes hat ihn genau dorthin gesetzt.
            // Trotzdem ausdruecklich uebergeben: SetSkinTm ist
            // die Stelle, an der Max die Bind-Pose FESTHAELT,
            // und sie aus der Datei zu nehmen statt aus der
            // Szene ist die verlaesslichere Quelle.
            //
            // Mit upAxisFix waere das inkonsistent - die Knoten
            // truegen dann eine Drehung, die Bindmatrix nicht.
            if (options_.engineSpace &&
                static_cast<size_t>(instance.node) < restWorld_.size()) {
                // Der EIGENE Mesh-Knoten dieser Instanz - dieselbe
                // Matrix, auf der das Objekt steht (BuildModelMeshes).
                // Dann kuerzt sich MeshWelt in der Deformation
                // heraus, auch wenn eine Gruppe mehrfach instanziert
                // ist (112_160.model) und die Bones ihre Bind-Pose
                // von der ersten Instanz haben.
                bind = MatMul(bind, restWorld_[static_cast<size_t>(instance.node)]);
            } else if (options_.bindIncludesMesh &&
                static_cast<size_t>(instance.palette) < paletteHasMesh_.size() &&
                paletteHasMesh_[static_cast<size_t>(instance.palette)]) {
                bind = MatMul(
                    bind, paletteMeshWorld_[static_cast<size_t>(instance.palette)]);
            }

            Matrix3 boneTm = (options_.upAxisFix && !options_.engineSpace)
                                 ? boneNodes[i]->GetNodeTM(0)
                                 : ToMatrix3(MaxSpace(bind, options_.engineSpace),
                                             options_.scale);
            skinImport->SetSkinTm(boneNodes[i], meshTm, boneTm);
        }

        // ---- Gewichte ----
        //
        // Die Bone-Indices im Vertex zeigen in die PALETTE, nicht
        // in die globale Knotenliste - und boneNodes enthaelt nur
        // die benutzten. Deshalb die Umsetzung ueber localBones.
        std::vector<int> paletteToSlot(palette.bones.size(), -1);
        for (size_t i = 0; i < localBones.size(); ++i) {
            paletteToSlot[localBones[i]] = static_cast<int>(i);
        }

        const size_t vertexCount = submesh.positions.size();
        int rejected = 0;

        for (size_t v = 0; v < vertexCount; ++v) {
            Tab<INode*> tabBones;
            Tab<float>  tabWeights;

            int used_slots = 0;
            for (int k = 0; k < 4; ++k) {
                const size_t at = v * 4 + static_cast<size_t>(k);
                if (at >= submesh.weights.size()) break;
                if (submesh.weights[at] <= 0.0f) continue;
                const size_t local = submesh.boneIndices[at];
                if (local >= paletteToSlot.size()) continue;
                if (paletteToSlot[local] < 0) continue;
                ++used_slots;
            }

            if (used_slots == 0) {
                ++rejected;
                continue;
            }

            tabBones.SetCount(used_slots);
            tabWeights.SetCount(used_slots);

            int slot = 0;
            for (int k = 0; k < 4 && slot < used_slots; ++k) {
                const size_t at = v * 4 + static_cast<size_t>(k);
                if (at >= submesh.weights.size()) break;
                if (submesh.weights[at] <= 0.0f) continue;
                const size_t local = submesh.boneIndices[at];
                if (local >= paletteToSlot.size()) continue;
                const int target = paletteToSlot[local];
                if (target < 0) continue;

                tabBones[slot] = boneNodes[static_cast<size_t>(target)];
                tabWeights[slot] = submesh.weights[at];
                ++slot;
            }

            if (!skinImport->AddWeights(node, static_cast<int>(v),
                                        tabBones, tabWeights)) {
                ++rejected;
            }
        }

        if (rejected > 0) {
            Warn(Msg() << L"Mesh '" << node->GetName() << L"': " << rejected
                       << L" von " << vertexCount
                       << L" Vertices ohne Gewichte.");
        }

        if (rejected < static_cast<int>(vertexCount)) ++stats_.skinned;
        if (options_.verbose) {
            Note(Msg() << L"  Skin: " << node->GetName() << L", "
                       << boneNodes.size() << L" Bones, Bind-Lage aus "
                       << (options_.bindPoseFromInvBind && !options_.upAxisFix
                               ? L"der Datei"
                               : L"der Szene"));
        }
    }

    return true;
}

// ============================================================
//  .model - Animation
// ============================================================

// ------------------------------------------------------------
//  Kanalwert zu einem Zeitpunkt
//
//  Die Kanaele haben je eigene Keyzeiten - Position kann 200
//  Keys haben und Rotation 3. Ausgewertet wird "letzter Key bei
//  oder vor t", ohne Interpolation: die Zwischenwerte legt Max
//  selbst ueber seine Controller an, und zwei Interpolationen
//  uebereinander waeren schlechter als eine.
// ------------------------------------------------------------
namespace {

// ------------------------------------------------------------
//  Kanalwert zu einem Zeitpunkt - LINEAR zwischen den Keys
//
//  Vorher lieferte diese Funktion den letzten Key <= t, also
//  konstante Interpolation. Solange nur an den EIGENEN Keyzeiten
//  eines Kanals gesampelt wurde, fiel das nicht auf: dort trifft
//  man immer genau einen Key.
//
//  Seit der Animationspfad an den VEREINIGTEN Keyzeiten des
//  ganzen Clips sampelt, ist es der Hauptfehler: ein Kanal ohne
//  Key an diesem Zeitpunkt bekommt den alten Wert eingefroren und
//  springt erst beim naechsten eigenen Key weiter. Das sind
//  Treppenstufen - und genau das sieht man als Stottern.
//
//  Die Kanaele in .model haben NICHT alle dieselben Keyzeiten:
//  6630 Kanaele in 157_skin11.model, mit unterschiedlichen
//  Rastern. Bei .x tritt das Problem nicht auf, weil dort jeder
//  Bone in jedem Frame einen Wert hat.
//
//  Rotationen werden ueber den kuerzeren Weg gemischt (Vorzeichen
//  angleichen) und wieder normiert; alles andere linear.
// ------------------------------------------------------------
const float* SampleChannel(const Channel& channel, double timeMs,
                           float* scratch) {
    if (!channel.decoded || channel.isBool || channel.keyCount <= 0) {
        return nullptr;
    }

    const int comps = channel.components;
    const size_t n = static_cast<size_t>(channel.keyCount);

    size_t at = 0;
    for (size_t k = 0; k < n; ++k) {
        if (channel.timesMs[k] > timeMs + 1e-6) break;
        at = k;
    }

    const size_t offset = at * static_cast<size_t>(comps);
    if (offset + static_cast<size_t>(comps) > channel.values.size()) {
        return nullptr;
    }

    // Letzter Key, oder genau auf einem Key: nichts zu mischen.
    if (at + 1 >= n || scratch == nullptr) return &channel.values[offset];

    const double t0 = channel.timesMs[at];
    const double t1 = channel.timesMs[at + 1];
    if (t1 - t0 < 1e-9) return &channel.values[offset];

    double f = (timeMs - t0) / (t1 - t0);
    if (f <= 1e-9) return &channel.values[offset];
    if (f > 1.0) f = 1.0;

    const size_t next = (at + 1) * static_cast<size_t>(comps);
    if (next + static_cast<size_t>(comps) > channel.values.size()) {
        return &channel.values[offset];
    }

    const float* a = &channel.values[offset];
    const float* b = &channel.values[next];

    if (comps == 4) {
        // Quaternion: ueber den kuerzeren Weg, dann normieren.
        double dot = 0.0;
        for (int c = 0; c < 4; ++c) dot += static_cast<double>(a[c]) * b[c];
        const double sign = (dot < 0.0) ? -1.0 : 1.0;

        double len = 0.0;
        for (int c = 0; c < 4; ++c) {
            const double v = a[c] * (1.0 - f) + sign * b[c] * f;
            scratch[c] = static_cast<float>(v);
            len += v * v;
        }
        len = std::sqrt(len);
        if (len > 1e-9)
            for (int c = 0; c < 4; ++c)
                scratch[c] = static_cast<float>(scratch[c] / len);
    } else {
        for (int c = 0; c < comps; ++c)
            scratch[c] = static_cast<float>(a[c] * (1.0 - f) + b[c] * f);
    }

    return scratch;
}

}  // namespace

// ------------------------------------------------------------
//  Controller tauschen, Bind-Pose behalten
//
//  UseLinearControllers setzt FRISCHE Controller ein - deren Wert
//  ist die Einheitsmatrix, die Bind-Pose aus BuildModelNodes /
//  BuildXBones ist danach weg. Bis 0.3.12 fiel das nicht auf, weil
//  die Animation sofort jeden Knoten ueberschrieb. Mit Ruheframes
//  sollte Frame 0 aber die Bind-Pose zeigen - tatsaechlich zeigte
//  Max dort den ersten Animationsframe (vor dem ersten Key gilt
//  dessen Wert), in 96 von 97 gehaeuteten Testdateien. Genau der
//  "Sprung zwischen Frame 0 und 5", nach dem lange gesucht wurde,
//  war damit gar nicht zu sehen - nur seine Folgen.
//
//  Deshalb: Weltmatrizen vorher merken, Controller tauschen,
//  Eltern vor Kindern zuruecksetzen, Skin Pose ablegen und bei
//  Ruheframes einen echten Key bei Frame 0 setzen.
// ------------------------------------------------------------
void SceneBuilder::SwapControllersKeepBind(const std::vector<int>& parents) {
    std::vector<Matrix3> bind(nodes_.size());
    std::vector<char> have(nodes_.size(), 0);
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i] == nullptr) continue;
        bind[i] = nodes_[i]->GetNodeTM(0);
        have[i] = 1;
    }

    for (INode* node : nodes_) UseLinearControllers(node);

    const std::vector<int> order = ParentsFirst(parents);
    for (int index : order) {
        const size_t i = static_cast<size_t>(index);
        if (i < have.size() && have[i]) nodes_[i]->SetNodeTM(0, bind[i]);
    }

    if (options_.storeSkinPose) StoreSkinPose(nodes_);

    if (options_.restFrames > 0) {
        AnimateGuard animate;
        int keyed = 0;
        for (int index : order) {
            const size_t i = static_cast<size_t>(index);
            if (i < have.size() && have[i]) {
                nodes_[i]->SetNodeTM(0, bind[i]);
                ++keyed;
            }
        }
        Note(Msg() << L"Bind-Pose als Key bei Frame 0: " << keyed << L" Knoten ("
                   << options_.restFrames << L" Ruheframes)");
    }
}

void SceneBuilder::BuildModelAnimation(const ModelFile& file) {
    if (file.clips.empty()) return;

    // ------------------------------------------------------------
    //  Keyraster der Datei bestimmen
    //
    //  Der HAEUFIGSTE Abstand zwischen zwei Keys ist ein Frame.
    //  Nicht der kleinste: einzelne Kanaele haben Zwischenkeys, und
    //  ein Ausreisser wuerde die ganze Zeitleiste stauchen.
    //
    //  Gezaehlt wird ueber die ersten Kanaele; das reicht, weil das
    //  Raster im ganzen Clip dasselbe ist.
    // ------------------------------------------------------------
    // Lineare Controller BEVOR der erste Key gesetzt wird - ein
    // Controllerwechsel danach wirft die Keys weg. Die Bind-Pose
    // bleibt dabei erhalten (SwapControllersKeepBind).
    {
        std::vector<int> parentsOfNodes(file.nodes.size());
        std::transform(file.nodes.begin(), file.nodes.end(), parentsOfNodes.begin(),
                       [](const ModelNode& node) { return node.parent; });
        SwapControllersKeepBind(parentsOfNodes);
    }

    keyStepMs_ = 0.0;
    if (options_.snapKeysToFrames) {
        std::map<int, int> haeufigkeit;
        int gesehen = 0;
        for (const Clip& clip : file.clips) {
            for (const Channel& channel : clip.channels) {
                if (channel.timesMs.size() < 2) continue;
                for (size_t k = 1; k < channel.timesMs.size(); ++k) {
                    const double d = channel.timesMs[k] - channel.timesMs[k - 1];
                    if (d > 0.5 && d < 10000.0)
                        ++haeufigkeit[static_cast<int>(std::lround(d))];
                }
                if (++gesehen >= 40) break;
            }
            if (gesehen >= 40) break;
        }

        int bester = 0, wieOft = 0;
        for (const std::pair<const int, int>& eintrag : haeufigkeit) {
            if (eintrag.second > wieOft) { wieOft = eintrag.second; bester = eintrag.first; }
        }

        if (bester > 0) {
            keyStepMs_ = static_cast<double>(bester);
            Note(Msg() << L"Keyraster: " << bester << L" ms je Frame ("
                       << wieOft << L" von " << haeufigkeit.size()
                       << L" Abstaenden) - ein Keyabstand ist ein Frame");
        }
    }

    // AnimateGuard ist Pflicht. Ohne AnimateOn() setzt SetValue
    // stillschweigend nur den Wert und legt KEINEN Key an - der
    // Import meldet dann Erfolg und die Zeitleiste bleibt leer.
    AnimateGuard animate;

    std::vector<int> parents(file.nodes.size());
    std::transform(file.nodes.begin(), file.nodes.end(), parents.begin(),
                   [](const ModelNode& node) { return node.parent; });
    const std::vector<int> order = ParentsFirst(parents);

    // ------------------------------------------------------------
    //  Ruheframes und der Uebergang aus der Bind-Pose
    //
    //  Mit restFrames > 0 steht bei Frame 0 die Bind-Pose (ohne
    //  Key, aus BuildModelNodes) und der erste Animationskey liegt
    //  weiter hinten. Max interpoliert dazwischen - und genau in
    //  diesen Frames dreht und skaliert sich alles, was zwischen
    //  Bindraum und Knotenraum liegt.
    //
    //  Am Modell nachvollzogen: der Pelvis beider Charaktere und
    //  mehrere Waffen wandern zwischen Frame 0 und 5. Es ist also
    //  kein Problem einzelner Bones, sondern der UEBERGANG.
    //
    //  Mit restFrames = 0 gibt es ihn nicht mehr.
    //
    //  Ruheframes vor der Animation, und im Sequenzmodus dieselbe
    //  Luecke zwischen den Clips - sonst interpoliert das Ende des
    //  einen in den Anfang des naechsten.
    const int offsetFrames = std::max(0, options_.restFrames);
    const double msPerFrame = 1000.0 / static_cast<double>(GetFrameRate());
    const double gapMs = static_cast<double>(offsetFrames) * msPerFrame;

    clipRanges_.clear();
    double offsetMs = gapMs;
    double totalMs = 0.0;

    for (size_t c = 0; c < file.clips.size(); ++c) {
        const Clip& clip = file.clips[c];
        const double base = options_.sequenceMode ? offsetMs : gapMs;

        ClipRange range;
        range.name = Utf8ToWide(clip.name);
        range.start = static_cast<int>(std::lround(base / msPerFrame));
        range.end = static_cast<int>(
            std::lround((base + clip.durationMs) / msPerFrame));
        clipRanges_.push_back(range);

        // ---- Kanaele nach Knoten sortieren ----
        //
        // Ein Knoten hat bis zu drei Transformkanaele, und die
        // muessen ZUSAMMEN ausgewertet werden: aus Position,
        // Rotation und Skalierung entsteht EINE Matrix. Sie
        // nacheinander in dieselbe Matrix zu schreiben - erst die
        // Translation, dann die Rotation darueber - loescht
        // jeweils den vorigen Teil.
        struct NodeChannels {
            const Channel* location = nullptr;
            const Channel* rotation = nullptr;
            const Channel* scale = nullptr;
            std::vector<const Channel*> extras;   // active, material._*
        };
        std::vector<NodeChannels> perNode(file.nodes.size());

        for (const Channel& channel : clip.channels) {
            if (!channel.decoded) continue;
            if (channel.node < 0 ||
                static_cast<size_t>(channel.node) >= perNode.size()) {
                continue;
            }
            NodeChannels& slot = perNode[static_cast<size_t>(channel.node)];

            if (channel.isBool || channel.kind == kChannelOther) {
                slot.extras.push_back(&channel);
            } else if (channel.kind == kChannelLocation) {
                slot.location = &channel;
            } else if (channel.kind == kChannelRotation) {
                slot.rotation = &channel;
            } else if (channel.kind == kChannelScale) {
                slot.scale = &channel;
            }
        }

        // ---- Transformkanaele, Eltern vor Kindern ----
        //
        // Dieselbe Begruendung wie bei der Bind-Pose: SetNodeTM
        // rechnet gegen die Elternlage ZUM ZEITPUNKT t.
        // ------------------------------------------------------------
        //  VEREINIGTE Keyzeiten des ganzen Clips
        //
        //  Die Elternmatrix darf nicht aus der Szene kommen. Dort
        //  steht an einem Zeitpunkt, an dem der Elternteil keinen
        //  Key hat, ein von Max INTERPOLIERTER Wert - und dieser
        //  Fehler schaukelt sich ueber die Hierarchie auf. Bei
        //  einem Fingerknochen, sieben Ebenen unter dem Becken,
        //  ist davon nichts mehr uebrig als Zittern.
        //
        //  Genau das ist der Unterschied zum .x-Zweig, der sauber
        //  laeuft: dort wird die Weltmatrix je Frame direkt aus
        //  den Daten gesetzt, ohne Umweg ueber die Szene.
        //
        //  Deshalb: alle Keyzeiten des Clips vereinigen, aussen
        //  ueber die Zeit laufen, innen ueber die Knoten (Eltern
        //  vor Kindern). Dann ist die Elternmatrix immer die zum
        //  SELBEN Zeitpunkt, aus denselben Kanaldaten gerechnet.
        // ------------------------------------------------------------
        std::vector<double> alleZeiten;
        for (size_t n = 0; n < perNode.size(); ++n) {
            const NodeChannels& slot = perNode[n];
            for (const Channel* channel : {slot.location, slot.rotation,
                                           slot.scale}) {
                if (channel == nullptr) continue;
                alleZeiten.insert(alleZeiten.end(), channel->timesMs.begin(),
                                  channel->timesMs.end());
            }
        }
        std::sort(alleZeiten.begin(), alleZeiten.end());
        alleZeiten.erase(std::unique(alleZeiten.begin(), alleZeiten.end()),
                         alleZeiten.end());

        // ------------------------------------------------------------
        //  Mit gerundeten Keyzeiten: auf dem FRAMERASTER abtasten
        //
        //  Die vereinigten Zeiten aller Kanaele liegen nicht auf dem
        //  Raster - 157_skin11_huicheng.model: 0, 16, 25, 32, 33, 41
        //  ms ... (6210 Zeitpunkte in einem 7-s-Clip). Gerundet
        //  fielen 0 und 16 ms auf Frame 0, und der SPAETERE Wert
        //  ueberschrieb den richtigen. An jedem Frame stand so ein
        //  Wert bis zu einen halben Frame zu spaet; die rechte Hand,
        //  die sich im ersten Intervall um 23 Grad dreht, stand bei
        //  Frame 0 um 9 Grad falsch. Gemessen bis 0.3.12 in allen
        //  .model-Dateien (Knotenvergleich gegen die Datei).
        //
        //  Jetzt wird jeder Kanal genau bei k * keyStepMs_
        //  interpoliert. Die Keys der Datei auf dem Raster kommen
        //  damit unveraendert durch, die anderen werden richtig
        //  eingemischt statt sich gegenseitig zu ueberschreiben.
        //  Nebenbei: rund 30-mal weniger SetNodeTM-Aufrufe.
        // ------------------------------------------------------------
        if (options_.snapKeysToFrames && keyStepMs_ > 0.0 && !alleZeiten.empty()) {
            const double ende = alleZeiten.back();
            const int letzter = static_cast<int>(std::ceil(ende / keyStepMs_ - 1e-6));
            std::vector<double> raster;
            raster.reserve(static_cast<size_t>(letzter) + 1);
            for (int k = 0; k <= letzter; ++k)
                raster.push_back(std::min(ende, static_cast<double>(k) * keyStepMs_));
            alleZeiten.swap(raster);
        }

        if (!alleZeiten.empty()) {
            std::vector<Matrix44> welt(file.nodes.size());

            for (double timeMs : alleZeiten) {
                const TimeValue t = MsToTicks(base + timeMs,
                                              options_.snapKeysToFrames,
                                              keyStepMs_);

                for (int index : order) {
                    const size_t n = static_cast<size_t>(index);
                    if (n >= nodes_.size() || nodes_[n] == nullptr) continue;

                    const ModelNode& source = file.nodes[n];
                    const NodeChannels& slot = perNode[n];

                    // Fehlende Kanaele: die Ruhelage, nicht die
                    // aktuelle Knotenmatrix - sonst wandert der
                    // Wert mit jedem Clip weiter.
                    Vec3 position = source.translation;
                    Quat rotation = source.rotation;
                    Vec3 scaling = source.scale;

                    // JE KANAL ein eigener Zwischenspeicher.
                    //
                    // Mit einem gemeinsamen ueberschreibt der
                    // zweite Aufruf das Ergebnis des ersten - der
                    // Zeiger aus SampleChannel zeigt ja dorthin,
                    // und gelesen wird er erst danach.
                    float sPos[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    float sRot[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    float sScl[4] = {0.0f, 0.0f, 0.0f, 0.0f};

                    if (slot.location != nullptr) {
                        if (const float* v = SampleChannel(*slot.location, timeMs, sPos)) {
                            position.x = v[0]; position.y = v[1]; position.z = v[2];
                        }
                    }
                    if (slot.rotation != nullptr) {
                        if (const float* v = SampleChannel(*slot.rotation, timeMs, sRot)) {
                            rotation.x = v[0]; rotation.y = v[1];
                            rotation.z = v[2]; rotation.w = v[3];
                        }
                    }
                    if (slot.scale != nullptr) {
                        if (const float* v = SampleChannel(*slot.scale, timeMs, sScl)) {
                            scaling.x = v[0]; scaling.y = v[1]; scaling.z = v[2];
                        }
                    }

                    // Versteckte Props wieder sichtbar machen -
                    // siehe ImportOptions::ignoreZeroScale.
                    if (options_.ignoreZeroScale) {
                        const float grenze = options_.zeroScaleThreshold;
                        if (std::fabs(scaling.x) <= grenze &&
                            std::fabs(scaling.y) <= grenze &&
                            std::fabs(scaling.z) <= grenze) {
                            scaling.x = scaling.y = scaling.z = 1.0f;
                            ++zeroScaleFixed_;
                        }
                    }

                    const Matrix44 local =
                        MatFromQuatPosScale(rotation, position, scaling);

                    welt[n] = (source.parent >= 0 &&
                               static_cast<size_t>(source.parent) < welt.size())
                                  ? MatMul(local, welt[static_cast<size_t>(source.parent)])
                                  : local;

                    Matrix3 tm = ToMatrix3(MaxSpace(welt[n], options_.engineSpace),
                                           options_.scale);

                    if (options_.upAxisFix && !options_.engineSpace &&
                        source.parent < 0)
                        tm = tm * ToMatrix3(UpAxisMatrix(), 1.0f);

                    nodes_[n]->SetNodeTM(t, tm);
                    ++stats_.keys;
                }
            }
        }

// ---- Sichtbarkeit und benannte Kanaele ----
        for (size_t n = 0; n < perNode.size(); ++n) {
            if (n >= nodes_.size() || nodes_[n] == nullptr) continue;
            INode* node = nodes_[n];

            for (const Channel* channel : perNode[n].extras) {
                if (channel->isBool) {
                    if (!options_.visibilityTracks) continue;

                    // ------------------------------------------
                    //  Sichtbarkeit gehoert auf das MESH, nicht
                    //  auf den Bone.
                    //
                    //  Die active/enable-Kanaele haengen am
                    //  Knoten, gemeint ist aber, ob das Teil zu
                    //  sehen ist. Auf den Bone gelegt lassen sie
                    //  Bones im Viewport verschwinden - beim
                    //  Rigging stoert das, und man sucht den
                    //  Fehler beim Skelett.
                    //
                    //  Der XFBIN-Importer macht es genauso: die
                    //  Spur landet auf der Geometrie.
                    //
                    //  Gesucht werden die Meshes, die an DIESEM
                    //  Knoten haengen. Findet sich keines, wird
                    //  die Spur weggelassen statt auf den Bone
                    //  gelegt.
                    // ------------------------------------------
                    std::vector<INode*> targets;
                    for (const BuiltMesh& built : builtMeshes_) {
                        if (built.node == nullptr) continue;
                        if (built.instance < 0 ||
                            static_cast<size_t>(built.instance) >=
                                file.instances.size()) {
                            continue;
                        }
                        if (file.instances[static_cast<size_t>(built.instance)]
                                .node == static_cast<int>(n)) {
                            targets.push_back(built.node);
                        }
                    }

                    if (targets.empty()) continue;

                    for (INode* target : targets) {
                        if (target->GetVisController() == nullptr) {
                            Control* control = NewDefaultFloatController();
                            if (control == nullptr) continue;
                            target->SetVisController(control);
                        }

                        for (int k = 0; k < channel->keyCount; ++k) {
                            const size_t at =
                                static_cast<size_t>(k) *
                                static_cast<size_t>(channel->components);
                            if (at >= channel->boolValues.size()) break;
                            const TimeValue t = MsToTicks(
                                base + channel->timesMs[static_cast<size_t>(k)],
                                options_.snapKeysToFrames, keyStepMs_);
                            target->SetVisibility(
                                t, channel->boolValues[at] ? 1.0f : 0.0f);
                            ++stats_.keys;
                        }
                    }
                    continue;
                }

                // material._Color und Verwandte. Sie gehoeren an
                // den Materialparameter, nicht an den Knoten. Bis
                // die Zuordnung Kanal -> Materialslot belegt ist,
                // werden sie als Benutzereigenschaft abgelegt -
                // sichtbar, aber ohne falsche Behauptung.
                if (!channel->kindName.empty()) {
                    const std::wstring note =
                        Msg() << channel->keyCount << L" Keys, nicht zugeordnet";
                    node->SetUserPropString(
                        Utf8ToWide(channel->kindName).c_str(), note.c_str());
                }
            }
        }

        // ------------------------------------------------------
        //  Wie viele Knoten bewegt dieser Clip ueberhaupt?
        //
        //  Nicht jeder Clip animiert alles. In 039_skin8.model
        //  hat der zweite Charakter (Bip002, der Tanzpartner
        //  144_skin10_dance) NUR im Clip "dance" Kanaele - in den
        //  uebrigen 16 steht er auf seiner Ruhelage.
        //
        //  Das sieht im Viewport aus wie ein Importfehler ("einer
        //  bleibt in der Bind-Pose"), ist aber die Datei. Die Zahl
        //  im Protokoll erspart die Suche danach.
        // ------------------------------------------------------
        if (options_.verbose) {
            const long moved = std::count_if(
                perNode.begin(), perNode.end(),
                [](const NodeChannels& slot) {
                    return slot.location != nullptr || slot.rotation != nullptr ||
                           slot.scale != nullptr;
                });
            Note(Msg() << L"  Clip '" << Utf8ToWide(clip.name) << L"': "
                       << moved << L" von " << file.nodes.size()
                       << L" Knoten werden bewegt");
        }

        offsetMs += clip.durationMs + gapMs;
        totalMs = std::max(totalMs, base + clip.durationMs);
        ++stats_.clips;
    }

    if (ip_ != nullptr && totalMs > 0.0) {
        ip_->SetAnimRange(Interval(0, MsToTicks(totalMs, options_.snapKeysToFrames, keyStepMs_)));
    }

}

// ============================================================
//  .model - Einstieg
// ============================================================

bool SceneBuilder::ImportModel(const ModelFile& file,
                               const std::wstring& sourcePath) {
    if (ip_ == nullptr) {
        error_ = L"Keine Core-Schnittstelle.";
        return false;
    }

    Stopwatch watch;
    clock_.reset();
    SceneRedrawGuard redraw(ip_);
    HoldSuspendGuard hold;

    const size_t slash = sourcePath.find_last_of(L"/\\");
    baseDir_ = (slash == std::wstring::npos) ? std::wstring()
                                             : sourcePath.substr(0, slash);

    Note(Msg() << L".model: " << file.nodes.size() << L" Knoten, "
               << file.groups.size() << L" Gruppen, "
               << file.instances.size() << L" Instanzen, "
               << file.palettes.size() << L" Paletten, "
               << file.materials.size() << L" Materialien, "
               << file.clips.size() << L" Clips");
    Note(Msg() << L"Optionen: Skelett=" << (options_.buildSkeleton ? 1 : 0)
               << L" Meshes=" << (options_.buildMeshes ? 1 : 0)
               << L" Skin=" << (options_.buildSkin ? 1 : 0)
               << L" Material=" << (options_.buildMaterials ? 1 : 0)
               << L" Animation=" << (options_.buildAnimation ? 1 : 0)
               << L" Skalierung=" << options_.scale);

    if (options_.buildMaterials) {
        materials_.reserve(file.materials.size());
        std::transform(file.materials.begin(), file.materials.end(),
                       std::back_inserter(materials_),
                       [&](const Material& material) {
                           return MakeMaterial(material, file.textures, baseDir_);
                       });
    } else {
        materials_.assign(file.materials.size(), nullptr);
    }

    Note(L"Materialien fertig, baue Knoten ...");
    // Den Bindfaktor IMMER messen, auch ohne Skelett - die
    // Meshes brauchen ihn. Ohne das waere die Mesh-Groesse
    // davon abhaengig, ob "Skeleton" angehakt ist, und das
    // waere aus der Oberflaeche heraus nicht zu erraten.
    if (!options_.buildSkeleton && options_.bindScaleOnly) {
        std::vector<float> lengths;
        for (const Palette& palette : file.palettes) {
            for (const Matrix44& m : palette.invBind) {
                const float len = std::sqrt(m.m[0][0] * m.m[0][0] +
                                            m.m[0][1] * m.m[0][1] +
                                            m.m[0][2] * m.m[0][2]);
                if (len > 1e-6f) lengths.push_back(len);
            }
        }
        if (!lengths.empty()) {
            const size_t middle = lengths.size() / 2;
            std::nth_element(lengths.begin(), lengths.begin() + middle,
                             lengths.end());
            if (lengths[middle] > 1e-6f) bindScale_ = 1.0f / lengths[middle];
        }
    }

    if (options_.buildSkeleton) BuildModelNodes(file);

    Note(Msg() << L"Knoten fertig (" << stats_.nodes << L"), baue Meshes ...");
    if (options_.buildMeshes) BuildModelMeshes(file);
    TagModelGroups(file);
    Note(Msg() << L"Meshes fertig (" << stats_.meshes << L" Meshes, "
               << stats_.vertices << L" Vertices), baue Skin ...");

    // Kein Abbruch, wenn der Skin scheitert: ein Modell ohne Skin
    // ist brauchbar, ein abgebrochener Import nicht.
    if (options_.buildSkin && options_.buildSkeleton && !BuildModelSkin(file)) {
        Warn(L"Skin konnte nicht aufgebaut werden - die Meshes stehen "
             L"ungewichtet in der Szene.");
    }

    ShapeBones();

    Note(Msg() << L"Skin fertig (" << stats_.skinned
               << L" Meshes gewichtet), baue Animation ...");
    if (options_.storeSkinPose && options_.buildSkeleton) {
        Note(Msg() << L"Bind-Pose als Skin Pose abgelegt: "
                   << StoreSkinPose(nodes_)
                   << L" Knoten (Animation -> Assume Skin Pose holt sie zurueck)");
    }
    if (options_.buildAnimation && options_.buildSkeleton) {
        BuildModelAnimation(file);
    }

    // ------------------------------------------------------------
    //  Ausdehnung von Bones und Meshes NACH dem Aufbau
    //
    //  Das ist die Zahl, die bisher jedes Mal von Hand aus dem
    //  Dump gerechnet werden musste. Stimmen Lage und Massstab,
    //  liegen beide Kaesten uebereinander; sonst steht hier
    //  schwarz auf weiss, um welchen Faktor und in welche
    //  Richtung sie auseinanderliegen.
    // ------------------------------------------------------------
    if (zeroScaleFixed_ > 0) {
        Warn(Msg() << zeroScaleFixed_
                   << L" Skalierungswert(e) nahe null auf 1 gesetzt - die "
                   << L"betroffenen Teile sind in der Datei absichtlich "
                   << L"versteckt.");
    }

    ReportExtents();

    // Eine Zeile, die beim Ueberfliegen des Logs sofort sagt, ob
    // der Lauf sauber war.
    //
    // ACHTUNG: hier zaehlen nur die Warnungen des SZENENAUFBAUS.
    // Die des PARSERS stehen schon in result.warnings, bevor der
    // Aufbau beginnt - deshalb stand im Log einmal "Keine
    // Warnungen." und darunter "Warnungen (1)".
    //
    // Gezaehlt wird jetzt beides zusammen.
    const size_t total = warnings_.size() + parserWarnings_;
    if (total == 0) {
        Note(L"Keine Warnungen.");
    } else {
        Note(Msg() << total << L" Warnung(en) - siehe unten.");
    }

    stats_.bones = static_cast<int>(nodes_.size());
    stats_.ms = watch.ms();
    Note(Msg() << L"fertig: " << stats_.keys << L" Keys in "
               << stats_.clips << L" Clips, " << warnings_.size()
               << L" Warnung(en)");
    return true;
}

// ============================================================
//  .x - Bones
// ============================================================

void SceneBuilder::BuildXBones(const XFile& file) {
    nodes_.assign(file.bones.size(), nullptr);

    for (size_t i = 0; i < file.bones.size(); ++i) {
        const XBone& bone = file.bones[i];
        const std::wstring name = bone.name.empty()
                                      ? (L"bone_" + std::to_wstring(i))
                                      : Utf8ToWide(bone.name);
        nodes_[i] = MakeNode(name, options_.boneObjects);
    }

    for (size_t i = 0; i < file.bones.size(); ++i) {
        const XBone& bone = file.bones[i];
        if (nodes_[i] == nullptr || bone.parent < 0) continue;
        if (static_cast<size_t>(bone.parent) >= nodes_.size()) continue;

        INode* parent = nodes_[static_cast<size_t>(bone.parent)];
        if (parent != nullptr) parent->AttachChild(nodes_[i], 0);
    }

    // Frame 0 IST die Bind-Pose. Es gibt keine separate
    // Bind-Matrix in der Datei.
    //
    // ELTERN VOR KINDERN - siehe ParentsFirst(). Die Bones stehen
    // in der Datei alphabetisch, nicht nach Hierarchie.
    std::vector<int> parents(file.bones.size());
    std::transform(file.bones.begin(), file.bones.end(), parents.begin(),
                   [](const XBone& bone) { return bone.parent; });

    for (int index : ParentsFirst(parents)) {
        const size_t i = static_cast<size_t>(index);
        const XBone& bone = file.bones[i];
        if (nodes_[i] == nullptr || bone.pos.empty() || bone.quat.empty()) continue;

        // KEINE Achsenkorrektur bei .x. Die JUMPX-Dateien kommen
        // bereits Z-up an - Skelett, Skin und Animation stehen
        // dort ohne Zutun richtig. Die Korrektur gilt nur fuer
        // .model, siehe ImportOptions::upAxisFix.
        const Matrix44 world = XBoneWorld(bone, 0);
        SetNodeWorld(nodes_[i], world);
        nodeWorldPos_.push_back(Vec3{world.m[3][0], world.m[3][1],
                                     world.m[3][2]});
    }

    stats_.bones = static_cast<int>(file.bones.size());
}

// ============================================================
//  .x - Meshes
// ============================================================

void SceneBuilder::BuildXMeshes(const XFile& file) {
    builtMeshes_.clear();

    for (size_t m = 0; m < file.meshes.size(); ++m) {
        const XMesh& source = file.meshes[m];
        if (source.positions.empty() || source.indices.empty()) continue;

        if (!IndicesInRange(source.indices, source.positions.size())) {
            Warn(Msg() << L"Mesh " << m
                       << L": Indices ausserhalb des Vertexbereichs, "
                       << L"uebersprungen.");
            continue;
        }

        TriObject* tri = CreateNewTriObject();
        if (tri == nullptr) continue;

        Mesh& mesh = tri->GetMesh();
        const int vertexCount = static_cast<int>(source.positions.size());
        const int faceCount = static_cast<int>(source.indices.size() / 3);

        mesh.setNumVerts(vertexCount);
        mesh.setNumFaces(faceCount);

        for (int v = 0; v < vertexCount; ++v) {
            mesh.setVert(v, ToPoint3(source.positions[static_cast<size_t>(v)],
                                     options_.scale));
        }

        for (int f = 0; f < faceCount; ++f) {
            const size_t base = static_cast<size_t>(f) * 3;
            mesh.faces[f].setVerts(static_cast<int>(source.indices[base]),
                                   static_cast<int>(source.indices[base + 1]),
                                   static_cast<int>(source.indices[base + 2]));
            mesh.faces[f].setEdgeVisFlags(1, 1, 1);
            mesh.faces[f].smGroup = 1;
        }

        if (source.uvs.size() == source.positions.size()) {
            mesh.setNumMaps(2, TRUE);
            mesh.setMapSupport(1, TRUE);
            mesh.setNumMapVerts(1, vertexCount);
            mesh.setNumMapFaces(1, faceCount);
            for (int v = 0; v < vertexCount; ++v) {
                const Vec2& uv = source.uvs[static_cast<size_t>(v)];
                // V umdrehen: DirectX zaehlt V von oben, Max von
                    // unten. Siehe ImportOptions::flipUvV.
                    mesh.setMapVert(1, v, Point3(uv.x,
                                                 options_.flipUvV ? 1.0f - uv.y
                                                                  : uv.y,
                                                 0.0f));
            }
            for (int f = 0; f < faceCount; ++f) {
                const size_t base = static_cast<size_t>(f) * 3;
                mesh.mapFaces(1)[f].setTVerts(
                    static_cast<int>(source.indices[base]),
                    static_cast<int>(source.indices[base + 1]),
                    static_cast<int>(source.indices[base + 2]));
            }
        }

        mesh.InvalidateTopologyCache();
        mesh.buildNormals();

        INode* node = ip_->CreateObjectNode(tri);
        if (node == nullptr) continue;

        const std::wstring name = source.name.empty()
                                      ? (L"mesh_" + std::to_wstring(m))
                                      : Utf8ToWide(source.name);
        node->SetName(const_cast<MCHAR*>(name.c_str()));

        if (source.material >= 0 &&
            static_cast<size_t>(source.material) < materials_.size()) {
            Mtl* mtl = materials_[static_cast<size_t>(source.material)];
            if (mtl != nullptr) node->SetMtl(mtl);
        }

        BuiltMesh built;
        built.node = node;
        built.source = static_cast<int>(m);
        builtMeshes_.push_back(built);

        ++stats_.meshes;
        stats_.vertices += vertexCount;
        stats_.faces += faceCount;

        if (source.droppedTriangles > 0) {
            Warn(Msg() << L"Mesh '" << name << L"': " << source.droppedTriangles
                       << L" Dreiecke mit 0xFFFF-Fuellung verworfen.");
        }
    }
}

// ============================================================
//  .x - Skin
//
//  Das fehlte bis 0.1.2 ganz: der Parser las boneIndices und
//  weights, die Max-Schicht benutzte sie nie. Im Protokoll stand
//  dann "geskinnt 0" - richtig gemeldet und trotzdem uebersehen,
//  weil das Modell im Viewport vollstaendig aussieht. Ein Mesh
//  ohne Skin faellt erst auf, wenn man die Zeitleiste bewegt.
//
//  Anders als bei .model gibt es hier KEINE Paletten: die
//  Bone-Indices im Vertex zeigen direkt in die globale Boneliste
//  (der Parser prueft gegen deren Laenge und nullt 255 als
//  "kein Bone").
// ============================================================

// ------------------------------------------------------------
//  .x - Material
//
//  XMaterial hat genau ein Feld: den Texturindex. Mehr steht
//  nicht in der Datei, also entsteht ein StdMat mit genau einer
//  Diffusmap - alles Weitere waere erfunden.
//
//  Bis 0.1.3 fehlte auch das: das Protokoll meldete
//  "Materialien 0", obwohl der Parser die Texturtabelle gelesen
//  hatte. Dieselbe Luecke wie beim Skin, eine Schicht weiter.
// ------------------------------------------------------------
void SceneBuilder::BuildXMaterials(const XFile& file) {
    materials_.assign(file.materials.size(), nullptr);
    if (ip_ == nullptr || !options_.buildMaterials) return;

    for (size_t m = 0; m < file.materials.size(); ++m) {
        const XMaterial& source = file.materials[m];

        StdMat2* mtl = static_cast<StdMat2*>(
            ip_->CreateInstance(MATERIAL_CLASS_ID, Class_ID(DMTL_CLASS_ID, 0)));
        if (mtl == nullptr) continue;

        std::wstring name = L"material_" + std::to_wstring(m);

        if (source.texture >= 0 &&
            static_cast<size_t>(source.texture) < file.textures.size()) {
            const std::wstring uri =
                Utf8ToWide(file.textures[static_cast<size_t>(source.texture)]);
            if (!uri.empty()) name = uri;

            BitmapTex* bitmap = static_cast<BitmapTex*>(
                ip_->CreateInstance(TEXMAP_CLASS_ID, Class_ID(BMTEX_CLASS_ID, 0)));
            if (bitmap != nullptr) {
                const std::wstring found = FindTexture(uri);
                const std::wstring full =
                    found.empty()
                        ? (baseDir_.empty() ? uri : (baseDir_ + L"\\" + uri))
                        : found;

                bitmap->SetMapName(full.c_str());
                bitmap->SetName(uri.c_str());
                mtl->SetSubTexmap(ID_DI, bitmap);

                // Im Viewport anzeigen.
                //
                // Laut SDK-Doku ist dafuer Interface::ActivateTexture
                // zustaendig: "This method does not cause the texture
                // map to be drawn in the viewport but should be called
                // with TRUE as the argument before this can occur."
                //
                // SetActiveTexmap allein genuegt nicht - es merkt sich
                // nur den Zeiger fuer den interaktiven Renderer.
                mtl->SetActiveTexmap(bitmap);
                ip_->ActivateTexture(bitmap, mtl);

                if (found.empty()) {
                    Warn(Msg() << L"Textur '" << uri
                               << L"' nicht gefunden - auch nicht unter anderer "
                               << L"Endung. Der Pfad wird trotzdem gesetzt.");
                } else if (found != (baseDir_ + L"\\" + uri)) {
                    Note(Msg() << L"  Textur '" << uri << L"' gefunden als '"
                               << found << L"'");
                } else {
                    Note(Msg() << L"  Textur '" << uri << L"'");
                }
            }
        }

        mtl->SetName(name.c_str());
        materials_[m] = mtl;
        ++stats_.materials;
    }
}

bool SceneBuilder::BuildXSkin(const XFile& file) {
    if (ip_ == nullptr) return false;
    if (builtMeshes_.empty() || nodes_.empty()) return true;

    // Ueber die gebauten Meshes laufen, nicht ueber file.meshes:
    // ein uebersprungenes Mesh wuerde die Zuordnung sonst
    // verschieben. Siehe BuiltMesh.
    for (const BuiltMesh& built : builtMeshes_) {
        INode* node = built.node;
        if (node == nullptr) continue;
        if (built.source < 0 ||
            static_cast<size_t>(built.source) >= file.meshes.size()) {
            continue;
        }

        const XMesh& source = file.meshes[static_cast<size_t>(built.source)];
        if (source.boneIndices.empty() || source.weights.empty()) continue;

        Modifier* skinMod = static_cast<Modifier*>(
            ip_->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
        if (skinMod == nullptr) {
            Warn(L"Skin-Modifier liess sich nicht anlegen.");
            return false;
        }

        Object* baseObject = node->GetObjectRef();
        if (baseObject == nullptr) continue;

        IDerivedObject* derived = nullptr;
        if (baseObject->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
            derived = static_cast<IDerivedObject*>(baseObject);
        } else {
            derived = CreateDerivedObject(baseObject);
            if (derived == nullptr) {
                Warn(L"Derived Object liess sich nicht anlegen - kein Skin.");
                continue;
            }
            node->SetObjectRef(derived);
        }
        derived->AddModifier(skinMod, nullptr, 0);

        ISkinImportData* skinImport = static_cast<ISkinImportData*>(
            skinMod->GetInterface(I_SKINIMPORTDATA));
        if (skinImport == nullptr) {
            Warn(L"ISkinImportData nicht verfuegbar.");
            continue;
        }

        // Nur die Bones anmelden, die dieses Mesh wirklich
        // benutzt. Alle 131 anzumelden kostet Zeit und macht die
        // Bone-Liste im Skin-Modifier unbrauchbar lang.
        std::vector<bool> used(nodes_.size(), false);
        for (size_t i = 0; i < source.boneIndices.size(); ++i) {
            if (source.weights[i] <= 0.0f) continue;
            const size_t bone = source.boneIndices[i];
            if (bone < used.size()) used[bone] = true;
        }

        std::vector<size_t> boneList;
        for (size_t b = 0; b < used.size(); ++b) {
            if (used[b] && nodes_[b] != nullptr) boneList.push_back(b);
        }

        if (boneList.empty()) {
            Warn(Msg() << L"Mesh '" << node->GetName()
                       << L"': keine benutzten Bones, kein Skin.");
            continue;
        }

        // update = TRUE nur beim LETZTEN, sonst rechnet Max die
        // Gewichtstabelle bei jedem Bone neu durch.
        for (size_t i = 0; i < boneList.size(); ++i) {
            const bool last = (i + 1 == boneList.size());
            skinImport->AddBoneEx(nodes_[boneList[i]], last ? TRUE : FALSE);
        }

        // Bind-Pose: bei .x IST Frame 0 die Bind-Pose, und die
        // Knoten stehen genau dort - GetNodeTM(0) ist also schon
        // die richtige Matrix, sie muss nicht aus der Datei
        // zurueckgerechnet werden.
        const Matrix3 meshTm = node->GetNodeTM(0);
        for (size_t index : boneList) {
            Matrix3 boneTm = nodes_[index]->GetNodeTM(0);
            skinImport->SetSkinTm(nodes_[index], meshTm, boneTm);
        }

        const size_t vertexCount = source.positions.size();
        int rejected = 0;

        for (size_t v = 0; v < vertexCount; ++v) {
            Tab<INode*> tabBones;
            Tab<float>  tabWeights;

            int used_slots = 0;
            for (int k = 0; k < 4; ++k) {
                const size_t at = v * 4 + static_cast<size_t>(k);
                if (at >= source.weights.size()) break;
                if (source.weights[at] <= 0.0f) continue;
                const size_t bone = source.boneIndices[at];
                if (bone >= nodes_.size() || nodes_[bone] == nullptr) continue;
                ++used_slots;
            }

            if (used_slots == 0) {
                ++rejected;
                continue;
            }

            tabBones.SetCount(used_slots);
            tabWeights.SetCount(used_slots);

            int slot = 0;
            for (int k = 0; k < 4 && slot < used_slots; ++k) {
                const size_t at = v * 4 + static_cast<size_t>(k);
                if (at >= source.weights.size()) break;
                if (source.weights[at] <= 0.0f) continue;
                const size_t bone = source.boneIndices[at];
                if (bone >= nodes_.size() || nodes_[bone] == nullptr) continue;

                tabBones[slot] = nodes_[bone];
                tabWeights[slot] = source.weights[at];
                ++slot;
            }

            if (!skinImport->AddWeights(node, static_cast<int>(v),
                                        tabBones, tabWeights)) {
                ++rejected;
            }
        }

        if (rejected > 0) {
            Warn(Msg() << L"Mesh '" << node->GetName() << L"': " << rejected
                       << L" von " << vertexCount << L" Vertices ohne Gewichte.");
        }
        if (source.clearedBoneSlots > 0) {
            Warn(Msg() << L"Mesh '" << node->GetName() << L"': "
                       << source.clearedBoneSlots
                       << L" Bone-Slots mit Fuellwert 255 genullt.");
        }

        if (rejected < static_cast<int>(vertexCount)) ++stats_.skinned;
        Note(Msg() << L"  Skin: " << node->GetName() << L", "
                   << boneList.size() << L" Bones");
    }

    return true;
}

// ============================================================
//  .x - Animation
// ============================================================

void SceneBuilder::BuildXAnimation(const XFile& file) {
    {
        std::vector<int> parentsOfBones(file.bones.size());
        std::transform(file.bones.begin(), file.bones.end(), parentsOfBones.begin(),
                       [](const XBone& bone) { return bone.parent; });
        SwapControllersKeepBind(parentsOfBones);
    }

    // .x speichert je Bone einen Wert je Frame, ohne Zeitstempel -
    // das Raster ist dort per Definition ein Frame. Deshalb kein
    // keyStepMs_ noetig; FrameToTicks rechnet direkt.

    if (file.frames <= 0) return;

    AnimateGuard animate;

    // Versatz: Frame 0 traegt dann nur die Ruhelage, die
    // Animation faengt dahinter an. Siehe
    // ImportOptions::restFrames - die .x-Dateien haben keine
    // eigene Bind-Pose, Frame 0 IST der erste Clipframe.
    const int offset = std::max(0, options_.restFrames);

    clipRanges_.clear();
    for (const XClip& clip : file.clips) {
        ClipRange range;
        range.name = Utf8ToWide(clip.name) +
                     (clip.synthetic ? L" (synthetisch)" : L"");
        range.start = clip.start + offset;
        range.end = clip.end + offset;
        clipRanges_.push_back(range);
        ++stats_.clips;
    }

    // ELTERN VOR KINDERN - hier genauso wie bei der Bind-Pose.
    //
    // Das war beim ersten Anlauf uebersehen: die Bind-Pose wurde
    // topologisch gesetzt, die Animation nicht. Ergebnis war eine
    // saubere Ruhestellung und ein zerrissenes Modell, sobald man
    // die Zeitleiste bewegte - was aussieht wie ein Fehler in der
    // Quaternion-Konvention und keiner ist.
    //
    // Warum es auch hier zaehlt, obwohl je Bone ALLE Frames auf
    // einmal gesetzt werden: SetNodeTM rechnet die Weltmatrix in
    // eine lokale um, relativ zur Elternlage ZUM ZEITPUNKT t. Hat
    // der Elternteil bei t noch keinen Key, steht er auf seiner
    // Frame-0-Lage, und der berechnete lokale Wert ist falsch.
    // Sobald der Elternteil spaeter seine Keys bekommt, dreht er
    // das Kind zusaetzlich mit.
    //
    // Bone-aussen, Frame-innen ist dabei in Ordnung, SOLANGE die
    // Bones topologisch laufen: der Elternteil hat dann alle
    // seine Keys, bevor das Kind den ersten bekommt.
    std::vector<int> parents(file.bones.size());
    std::transform(file.bones.begin(), file.bones.end(), parents.begin(),
                   [](const XBone& bone) { return bone.parent; });

    for (int index : ParentsFirst(parents)) {
        const size_t i = static_cast<size_t>(index);
        const XBone& bone = file.bones[i];
        if (i >= nodes_.size() || nodes_[i] == nullptr) continue;
        if (bone.pos.empty()) continue;

        INode* node = nodes_[i];

        // Welche Frames gebraucht werden. Ohne Reduktion alle;
        // mit Reduktion nur die, an denen sich etwas aendert -
        // plus der erste und der letzte Frame jedes Stillstands,
        // sonst interpoliert Max ueber die Pause hinweg.
        const size_t frameCount = bone.pos.size();
        std::vector<char> keep(frameCount, 1);

        if (options_.reduceKeys && frameCount > 2) {
            const float eps = options_.keyEpsilon;
            auto same = [eps](const Vec3& a, const Vec3& b) {
                return std::fabs(a.x - b.x) < eps &&
                       std::fabs(a.y - b.y) < eps &&
                       std::fabs(a.z - b.z) < eps;
            };
            auto sameQuat = [eps](const eg3d::Quat& a, const eg3d::Quat& b) {
                return std::fabs(a.x - b.x) < eps &&
                       std::fabs(a.y - b.y) < eps &&
                       std::fabs(a.z - b.z) < eps &&
                       std::fabs(a.w - b.w) < eps;
            };

            for (size_t f = 1; f + 1 < frameCount; ++f) {
                const bool prevSame =
                    same(bone.pos[f], bone.pos[f - 1]) &&
                    sameQuat(bone.quat[f], bone.quat[f - 1]);
                const bool nextSame =
                    same(bone.pos[f], bone.pos[f + 1]) &&
                    sameQuat(bone.quat[f], bone.quat[f + 1]);
                if (prevSame && nextSame) keep[f] = 0;
            }
        }

        for (size_t f = 0; f < frameCount; ++f) {
            if (!keep[f]) continue;

            // Die Konjugation steckt in XBoneWorld und NUR dort.
            // Steht sie an zwei Stellen, ist eine davon falsch -
            // und in der Bind-Pose kuerzt sie sich weg, der Fehler
            // faellt also erst in der Animation auf.
            const Matrix44 world = XBoneWorld(bone, static_cast<int>(f));
            Matrix3 tm = ToMatrix3(world, options_.scale);
            node->SetNodeTM(FrameToTicks(static_cast<int>(f) + offset), tm);
            ++stats_.keys;
        }

        // Der vierte Wert je Frame ist ein Sichtbarkeitsschalter.
        if (bone.HasVisible() && options_.visibilityTracks) {
            if (node->GetVisController() == nullptr) {
                Control* control = NewDefaultFloatController();
                if (control != nullptr) node->SetVisController(control);
            }
            if (node->GetVisController() != nullptr) {
                for (size_t f = 0; f < bone.visible.size(); ++f) {
                    node->SetVisibility(
                        FrameToTicks(static_cast<int>(f) + offset),
                        bone.visible[f] ? 1.0f : 0.0f);
                    ++stats_.keys;
                }
            }
        }
    }

    if (ip_ != nullptr) {
        // Der Bereich fangt bei 0 an, damit die Ruheframes
        // sichtbar sind, und endet hinter dem letzten Key.
        ip_->SetAnimRange(Interval(0, FrameToTicks(file.frames - 1 + offset)));
    }
}

// ============================================================
//  .x - Einstieg
// ============================================================

bool SceneBuilder::ImportX(const XFile& file, const std::wstring& sourcePath) {
    if (ip_ == nullptr) {
        error_ = L"Keine Core-Schnittstelle.";
        return false;
    }

    Stopwatch watch;
    clock_.reset();
    SceneRedrawGuard redraw(ip_);
    HoldSuspendGuard hold;

    const size_t slash = sourcePath.find_last_of(L"/\\");
    baseDir_ = (slash == std::wstring::npos) ? std::wstring()
                                             : sourcePath.substr(0, slash);

    Note(Msg() << L".x: " << file.bones.size() << L" Bones, "
               << file.meshes.size() << L" Meshes, " << file.frames
               << L" Frames, " << file.clips.size() << L" Clips");

    BuildXMaterials(file);

    Note(Msg() << L"Materialien fertig (" << stats_.materials
               << L"), baue Bones ...");
    if (options_.buildSkeleton) BuildXBones(file);

    Note(Msg() << L"Bones fertig (" << stats_.bones << L"), baue Meshes ...");
    if (options_.buildMeshes) BuildXMeshes(file);

    Note(Msg() << L"Meshes fertig (" << stats_.meshes << L"), baue Skin ...");
    if (options_.buildSkin && options_.buildSkeleton && !BuildXSkin(file)) {
        Warn(L"Skin konnte nicht aufgebaut werden - die Meshes stehen "
             L"ungewichtet in der Szene.");
    }

    // Bones formen, BEVOR die Animation laeuft: ShapeBones liest
    // die Knotenlagen bei Frame 0, und danach stehen dort die
    // Keys der Animation.
    ShapeBones();

    Note(Msg() << L"Skin fertig (" << stats_.skinned
               << L" Meshes gewichtet), baue Animation ...");
    // .x hat keine eigene Bind-Pose: Frame 0 IST sie (siehe
    // README, "Drei Fallen beim .x"). Abgelegt wird also genau das,
    // was BuildXBones gesetzt hat.
    if (options_.storeSkinPose && options_.buildSkeleton) {
        Note(Msg() << L"Bind-Pose als Skin Pose abgelegt: "
                   << StoreSkinPose(nodes_) << L" Bones");
    }
    if (options_.buildAnimation && options_.buildSkeleton) BuildXAnimation(file);
    Note(Msg() << L"fertig: " << stats_.keys << L" Keys");

    if (file.nanBones > 0) {
        Warn(Msg() << file.nanBones << L" Bone(s) mit NaN in " << file.nanFrames
                   << L" Frames - der Parser hat sie durch den naechsten "
                   << L"gueltigen Frame ersetzt.");
    }
    if (file.denormBones > 0) {
        Warn(Msg() << file.denormBones << L" Bone(s) mit nicht normierten "
                   << L"Rotationen in " << file.denormKeys
                   << L" Frames - normiert.");
    }

    if (zeroScaleFixed_ > 0) {
        Warn(Msg() << zeroScaleFixed_
                   << L" Skalierungswert(e) nahe null auf 1 gesetzt - die "
                   << L"betroffenen Teile sind in der Datei absichtlich "
                   << L"versteckt.");
    }

    ReportExtents();

    // Eine Zeile, die beim Ueberfliegen des Logs sofort sagt, ob
    // der Lauf sauber war.
    //
    // ACHTUNG: hier zaehlen nur die Warnungen des SZENENAUFBAUS.
    // Die des PARSERS stehen schon in result.warnings, bevor der
    // Aufbau beginnt - deshalb stand im Log einmal "Keine
    // Warnungen." und darunter "Warnungen (1)".
    //
    // Gezaehlt wird jetzt beides zusammen.
    const size_t total = warnings_.size() + parserWarnings_;
    if (total == 0) {
        Note(L"Keine Warnungen.");
    } else {
        Note(Msg() << total << L" Warnung(en) - siehe unten.");
    }

    stats_.ms = watch.ms();
    return true;
}

// ============================================================
//  Eg3dImportInterface - die Naht zu MAXScript
// ============================================================

void Eg3dImportInterface::SetError(const std::wstring& text) {
    error_ = text;
}

void Eg3dImportInterface::TakeWarnings(const std::vector<std::string>& source) {
    warnings_.reserve(warnings_.size() + source.size());
    std::transform(source.begin(), source.end(), std::back_inserter(warnings_),
                   Utf8ToWide);
}

int Eg3dImportInterface::Open(const MCHAR* path) {
    Close();

    if (path == nullptr) {
        SetError(L"open: kein Pfad angegeben.");
        return 0;
    }

    sourcePath_ = path;

    std::vector<uint8_t> data;
    std::string readError;
    if (!ReadWholeFile(WideToUtf8(sourcePath_), data, readError)) {
        SetError(Msg() << L"open: " << Utf8ToWide(readError));
        return 0;
    }

    // Erkennung an der Signatur, nicht an der Endung. Im Bestand
    // liegen .model-Dateien mit .x-Endung und umgekehrt.
    const std::string source = WideToUtf8(sourcePath_);

    if (LooksLikeEg3d(data.data(), data.size())) {
        model_ = std::make_unique<ModelFile>();
        lastResult_ = ReadModelBuffer(data.data(), data.size(), source, *model_);
        if (!lastResult_.ok) {
            SetError(Msg() << L"open: " << Utf8ToWide(lastResult_.error));
            model_.reset();
            return 0;
        }
        TakeWarnings(lastResult_.warnings);
        kind_ = L"model";
        error_.clear();
        return 1;
    }

    if (LooksLikeJumpx(data.data(), data.size())) {
        xfile_ = std::make_unique<XFile>();
        lastResult_ = ReadXBuffer(data.data(), data.size(), source, *xfile_);
        if (!lastResult_.ok) {
            SetError(Msg() << L"open: " << Utf8ToWide(lastResult_.error));
            xfile_.reset();
            return 0;
        }
        TakeWarnings(lastResult_.warnings);
        kind_ = L"x";
        error_.clear();
        return 1;
    }

    SetError(L"open: weder EG3D noch JUMPX - die Signatur passt zu keinem "
             L"der beiden Formate.");
    return 0;
}

int Eg3dImportInterface::Close() {
    model_.reset();
    xfile_.reset();
    kind_.clear();
    sourcePath_.clear();
    warnings_.clear();
    log_.clear();
    clipRanges_.clear();
    error_.clear();
    lastResult_ = ReadResult();
    stats_ = ImportStats();
    return 1;
}

int Eg3dImportInterface::IsOpen() {
    return (model_ || xfile_) ? 1 : 0;
}

const MCHAR* Eg3dImportInterface::GetKind() {
    returnBuffer_ = kind_;
    return returnBuffer_.c_str();
}

// ------------------------------------------------------------
//  Kurzfassung fuer den Dialog - englisch und knapp.
//
//  Vorher standen hier acht Angaben in einer Zeile, von denen die
//  Haelfte rechts abgeschnitten wurde. Ein Label bricht nicht um,
//  und was man nicht sieht, hilft nicht.
//
//  Geblieben sind die vier Zahlen, nach denen man beim Oeffnen
//  einer unbekannten Datei tatsaechlich schaut: wie gross ist sie,
//  hat sie ein Skelett, hat sie Animation. Alles Weitere steht im
//  Protokoll.
// ------------------------------------------------------------
const MCHAR* Eg3dImportInterface::GetSummary() {
    Msg out;
    if (model_) {
        out << L"EG3D v" << model_->version << L" - "
            << model_->VertexCount() << L" verts, "
            << model_->nodes.size() << L" nodes, "
            << model_->materials.size() << L" materials, "
            << model_->clips.size() << L" clips";
    } else if (xfile_) {
        out << L"JUMPX - "
            << xfile_->VertexCount() << L" verts, "
            << xfile_->bones.size() << L" bones, "
            << xfile_->frames << L" frames, "
            << xfile_->clips.size() << L" clips";
    } else {
        out << L"nothing opened";
    }
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

int Eg3dImportInterface::Dump(const MCHAR* outPath) {
    error_.clear();

    if (outPath == nullptr) {
        SetError(L"dump: kein Pfad angegeben.");
        return 0;
    }
    if (!model_ && !xfile_) {
        SetError(L"dump: nichts geoeffnet.");
        return 0;
    }

    // Derselbe Dump wie aus eg3ddump.exe, Zeile fuer Zeile. Genau
    // das macht ihn brauchbar: wer im Plugin etwas Merkwuerdiges
    // sieht, vergleicht mit dem Werkzeug statt zu raten.
    std::ofstream out(WideToUtf8(std::wstring(outPath)).c_str(),
                      std::ios::binary);
    if (!out) {
        SetError(L"dump: Ausgabedatei liess sich nicht anlegen.");
        return 0;
    }

    const DumpSections sections = DumpSections::All();
    if (model_) {
        DumpModel(*model_, lastResult_, sections, out);
    } else {
        DumpX(*xfile_, lastResult_, sections, out);
    }
    return out.good() ? 1 : 0;
}

int Eg3dImportInterface::ImportAll() {
    error_.clear();

    if (!model_ && !xfile_) {
        SetError(L"import: nichts geoeffnet.");
        return 0;
    }

    Interface* ip = GetCOREInterface();
    if (ip == nullptr) {
        SetError(L"import: keine Core-Schnittstelle.");
        return 0;
    }

    SceneBuilder builder(ip, options_);

    // Die Warnungen des Parsers stehen schon in warnings_, bevor der
    // Aufbau beginnt. Ohne diese Zeile zaehlt die Zusammenfassung am
    // Ende nur die des Aufbaus - und meldet "Keine Warnungen.",
    // waehrend darunter welche stehen.
    builder.SetParserWarnings(warnings_.size());

    const bool ok = model_ ? builder.ImportModel(*model_, sourcePath_)
                           : builder.ImportX(*xfile_, sourcePath_);

    stats_ = builder.Stats();
    const std::vector<std::wstring>& built = builder.Warnings();
    warnings_.insert(warnings_.end(), built.begin(), built.end());

    // Das Protokoll ERSETZT das vorige, es haengt nicht an: ein
    // zweiter Importlauf soll seinen eigenen Verlauf zeigen, nicht
    // den von vorhin mit dazu.
    log_ = builder.Log();
    clipRanges_ = builder.ClipRanges();
    if (!ok) SetError(builder.Error());

    if (ip != nullptr) ip->RedrawViews(ip->GetTime());
    return ok ? 1 : 0;
}

const MCHAR* Eg3dImportInterface::GetVersion() {
    returnBuffer_ = EG3DIMPORT_VERSION_STR;
    return returnBuffer_.c_str();
}

const MCHAR* Eg3dImportInterface::GetLastError() {
    returnBuffer_ = error_;
    return returnBuffer_.c_str();
}

const MCHAR* Eg3dImportInterface::GetWarnings() {
    Msg out;
    for (size_t i = 0; i < warnings_.size(); ++i) {
        if (i > 0) out << L"\n";
        out << warnings_[i];
    }
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

const MCHAR* Eg3dImportInterface::GetLog() {
    Msg out;
    for (size_t i = 0; i < log_.size(); ++i) {
        if (i > 0) out << L"\n";
        out << log_[i];
    }
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

int Eg3dImportInterface::SaveLog(const MCHAR* outPath) {
    // Den alten Fehler loeschen, BEVOR es losgeht.
    //
    // Sonst zeigt die Oberflaeche bei einem spaeteren Fehlschlag
    // eine Meldung von vorhin - beim Log-Knopf stand
    // "open: weder EG3D noch JUMPX", obwohl das Speichern
    // scheiterte und der Import laengst gelaufen war.
    error_.clear();

    if (outPath == nullptr) {
        SetError(L"saveLog: kein Pfad angegeben.");
        return 0;
    }

    std::ofstream out(WideToUtf8(std::wstring(outPath)).c_str(),
                      std::ios::binary);
    if (!out) {
        SetError(L"saveLog: Datei liess sich nicht anlegen.");
        return 0;
    }

    // UTF-8 mit BOM: sonst zeigt der Windows-Editor die
    // chinesischen Knotennamen als Kraehenfuesse, und das erste,
    // was jemand meldet, ist ein Zeichensatzproblem statt des
    // eigentlichen Fehlers.
    out << "\xEF\xBB\xBF";
    out << "EG3D Import " << WideToUtf8(EG3DIMPORT_VERSION_STR) << "\n";
    out << "Datei: " << WideToUtf8(sourcePath_) << "\n";
    out << "Format: " << WideToUtf8(kind_) << "\n\n";

    out << "--- Protokoll ---\n";
    for (const std::wstring& line : log_) out << WideToUtf8(line) << "\n";

    out << "\n--- Warnungen (" << warnings_.size() << ") ---\n";
    for (const std::wstring& line : warnings_) out << WideToUtf8(line) << "\n";

    out << "\n--- Ergebnis ---\n";
    out << WideToUtf8(GetStats()) << "\n";

    if (!error_.empty()) {
        out << "\n--- Fehler ---\n" << WideToUtf8(error_) << "\n";
    }
    return out.good() ? 1 : 0;
}

int Eg3dImportInterface::SetRestFrames(int frames) {
    if (frames < 0) {
        SetError(L"setRestFrames: die Zahl darf nicht negativ sein.");
        return 0;
    }
    options_.restFrames = frames;
    return 1;
}

const MCHAR* Eg3dImportInterface::GetClipRanges() {
    Msg out;
    for (size_t i = 0; i < clipRanges_.size(); ++i) {
        if (i > 0) out << L"\n";
        out << clipRanges_[i].name << L"\t" << clipRanges_[i].start
            << L"\t" << clipRanges_[i].end;
    }
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

int Eg3dImportInterface::SetBindFromInvBind(int on) {
    options_.bindPoseFromInvBind = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetAnimBindScale(int on) {
    options_.animBindScale = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetAnimRootFix(int on) {
    options_.animRootFix = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetMeshBindScale(int on) {
    options_.meshBindScale = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetIgnoreZeroScale(int on) {
    options_.ignoreZeroScale = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetLinearKeys(int on) {
    options_.linearKeys = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetFlipUvV(int on) {
    options_.flipUvV = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetMeshAxisFix(int on) {
    options_.meshAxisFix = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetBindIncludesMesh(int on) {
    options_.bindIncludesMesh = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetSnapKeys(int on) {
    options_.snapKeysToFrames = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetRetargetAnimation(int on) {
    options_.retargetAnimation = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetBindScaleOnly(int on) {
    options_.bindScaleOnly = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetUpAxisFix(int on) {
    options_.upAxisFix = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetMatchBindScale(int on) {
    options_.matchBindScale = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetReduceKeys(int on) {
    options_.reduceKeys = (on != 0);
    return 1;
}

int Eg3dImportInterface::SetDebug(int on) {
    options_.verbose = (on != 0);
    return 1;
}

const MCHAR* Eg3dImportInterface::GetStats() {
    Msg out;
    out << L"Knoten " << stats_.nodes << L", Meshes " << stats_.meshes
        << L", Vertices " << stats_.vertices << L", Dreiecke " << stats_.faces
        << L", Bones " << stats_.bones << L", geskinnt " << stats_.skinned
        << L", Materialien " << stats_.materials << L", Clips " << stats_.clips
        << L", Keys " << stats_.keys << L", ";
    out.Fixed(stats_.ms, 1) << L" ms";
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

const MCHAR* Eg3dImportInterface::GetClipNames() {
    Msg out;
    if (model_) {
        for (size_t i = 0; i < model_->clips.size(); ++i) {
            if (i > 0) out << L"\n";
            out << Utf8ToWide(model_->clips[i].name);
        }
    } else if (xfile_) {
        for (size_t i = 0; i < xfile_->clips.size(); ++i) {
            if (i > 0) out << L"\n";
            out << Utf8ToWide(xfile_->clips[i].name);
        }
    }
    returnBuffer_ = out.str();
    return returnBuffer_.c_str();
}

int Eg3dImportInterface::SetOption(const MCHAR* name, int value) {
    if (name == nullptr) return 0;

    const std::wstring key(name);
    const bool on = (value != 0);

    if (key == L"skeleton")        options_.buildSkeleton = on;
    else if (key == L"meshes")     options_.buildMeshes = on;
    else if (key == L"skin")       options_.buildSkin = on;
    else if (key == L"materials")  options_.buildMaterials = on;
    else if (key == L"animation")  options_.buildAnimation = on;
    else if (key == L"sequence")   options_.sequenceMode = on;
    else if (key == L"noteTrack")  options_.noteTrack = on;
    else if (key == L"visibility") options_.visibilityTracks = on;
    else if (key == L"boneObjects") options_.boneObjects = on;
    else if (key == L"skipParticleTemplates") options_.skipParticleTemplates = on;
    else if (key == L"engineSpace") options_.engineSpace = on;
    else if (key == L"skinPose")   options_.storeSkinPose = on;
    else {
        SetError(Msg() << L"setOption: unbekannte Option '" << key << L"'.");
        return 0;
    }
    return 1;
}

const MCHAR* Eg3dImportInterface::TakePendingFile() {
    returnBuffer_ = pendingFile_;
    pendingFile_.clear();
    return returnBuffer_.c_str();
}

int Eg3dImportInterface::SetScale(float value) {
    if (!(value > 0.0f)) {
        SetError(L"setScale: der Faktor muss groesser als 0 sein.");
        return 0;
    }
    options_.scale = value;
    return 1;
}

bool Eg3dImportInterface::RunScriptText(const std::wstring& script) {
    // Die Signatur hat sich mit Max 2022 geaendert: davor
    // (MCHAR*, BOOL, FPValue*), ab 2022 steht an zweiter Stelle
    // ein MAXScript::ScriptSource - ein Namensraum, den es vorher
    // gar nicht gab.
    //
    // MAX_RELEASE kommt aus plugapi.h und ist damit immer die
    // Wahrheit ueber das SDK, gegen das gerade uebersetzt wird:
    // 2016 = 18000, 2022 = 24000, je Jahrgang 1000 mehr.
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    return ExecuteMAXScriptScript(script.c_str(),
                                  MAXScript::ScriptSource::NonEmbedded,
                                  TRUE) != FALSE;
#else
    return ExecuteMAXScriptScript(script.c_str(), TRUE) != FALSE;
#endif
}

void Eg3dImportInterface::RunMacro() {
    RunScriptText(L"try (macros.run \"EG3D\" \"Eg3dImport\") catch "
                  L"(format \"[EG3D] Oberflaeche liess sich nicht oeffnen: %\\n\" "
                  L"(getCurrentException()))");
}

}  // namespace eg3d
