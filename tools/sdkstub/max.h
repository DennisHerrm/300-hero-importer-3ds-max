// ============================================================
//  max.h - ATTRAPPE des 3ds Max SDK
//
//  KEIN SDK. Siehe tools/sdkstub/README.md.
//
//  Nachgebildet ist genau das, was src/eg3dimport*.cpp benutzt,
//  mit den Signaturen aus der Autodesk-Doku. Jede Funktion gibt
//  einen Platzhalter zurueck; nichts davon laeuft.
//
//  Der Sinn: der Uebersetzer prueft Typen, Signaturen und
//  override-Deklarationen, ohne dass ein SDK installiert sein
//  muss - und BEIDE Zweige jeder MAX_RELEASE-Weiche, weil
//  PRUEFE_PLUGIN.py den Wert je Lauf setzt.
//
//  MAX_RELEASE wird NICHT hier gesetzt. Es kommt ueber -D von
//  aussen; ohne das faellt der Stub auf 2025 zurueck.
// ============================================================
#ifndef EG3D_MAXSTUB_H
#define EG3D_MAXSTUB_H

#include "winstub.h"

#include <cstdint>
#include <cstring>
#include <string>

#ifndef MAX_RELEASE
#define MAX_RELEASE 27000
#endif
#define VERSION_3DSMAX MAX_RELEASE

// ------------------------------------------------------------
//  Zeichen und Zeichenketten
// ------------------------------------------------------------
using MCHAR = wchar_t;
using MSTR = std::wstring;

#ifndef _T
#define _T(x) L##x
#endif

// ------------------------------------------------------------
//  Klassen-IDs
// ------------------------------------------------------------
using SClass_ID = unsigned long;

class Class_ID {
public:
    Class_ID() = default;
    Class_ID(unsigned long a, unsigned long b) : a_(a), b_(b) {}
    unsigned long PartA() const { return a_; }
    unsigned long PartB() const { return b_; }
    bool operator==(const Class_ID& o) const { return a_ == o.a_ && b_ == o.b_; }
    bool operator!=(const Class_ID& o) const { return !(*this == o); }
private:
    unsigned long a_ = 0;
    unsigned long b_ = 0;
};

// Freie Funktion des SDK: CreateInstance(SClass_ID, Class_ID) legt
// eine Instanz einer registrierten Klasse an - ohne
// Interface-Zeiger.
inline void* CreateInstance(SClass_ID, Class_ID) { return nullptr; }

#define GEOMOBJECT_CLASS_ID   0x00000010
#define HELPER_CLASS_ID       0x00000020
#define OSM_CLASS_ID          0x00000810
#define UTILITY_CLASS_ID      0x00000adf
#define SCENE_IMPORT_CLASS_ID 0x00000a11
// SuperClassIDs der Controller und die Class_IDs der linearen
// Varianten. Namen aus dem SDK: control.h / animtbl.h.
#define CTRL_POSITION_CLASS_ID 0x9003
#define CTRL_ROTATION_CLASS_ID 0x9004
#define CTRL_SCALE_CLASS_ID    0x9005
// ACHTUNG: im echten SDK sind das EINZELNE Zahlen (PartA), keine
// Class_ID-Objekte. Der Aufrufer muss Class_ID(WERT, 0) bilden.
//
// Im Stub standen sie zuerst als Class_ID(...) - damit uebersetzte
// der Stub-Lauf sauber, waehrend alle zwoelf echten Jahrgaenge an
//
//   error C2664: Konvertierung von Argument 2 von "int" in
//   "Class_ID" nicht moeglich
//
// scheiterten. Genau der Fall aus Regel 13: eine Attrappe, die
// nicht nachgeschlagen, sondern geraten wurde.
#define LININTERP_POSITION_CLASS_ID 0x118F7E02
#define LININTERP_ROTATION_CLASS_ID 0x118F7E03
#define LININTERP_SCALE_CLASS_ID    0x118F7E04

#define MATERIAL_CLASS_ID     0x00000c00
#define TEXMAP_CLASS_ID       0x00000c10
#define REF_TARGET_CLASS_ID   0x00000f00
#define GEN_DERIVOB_CLASS_ID  0x00000010

// PartA-Werte der Standardmaterialien aus plugapi.h. Bewusst
// KEINE fertigen Class_ID: im echten SDK sind es ebenfalls
// blanke Zahlen, und der Aufrufer muss Class_ID(X, 0) schreiben.
#define DMTL_CLASS_ID   0x00000002
#define MULTI_CLASS_ID  0x00000200
#define BMTEX_CLASS_ID  0x00000240

#define BONE_OBJ_CLASSID  Class_ID(0x28bf6e8d, 0x2ecca840)

// Parameterblock des Bone-Objekts, aus plugapi.h uebernommen.
// Die Reihenfolge ist Teil der Schnittstelle: die Werte werden
// ueber den Index gesetzt, nicht ueber den Namen.
enum BoneObj_Block { boneobj_params };
enum BoneObj_Params {
    boneobj_width, boneobj_height, boneobj_taper, boneobj_length,
    boneobj_sidefins, boneobj_sidefins_size,
    boneobj_sidefins_starttaper, boneobj_sidefins_endtaper,
    boneobj_frontfin, boneobj_frontfin_size,
    boneobj_frontfin_starttaper, boneobj_frontfin_endtaper,
    boneobj_backfin, boneobj_backfin_size,
    boneobj_backfin_starttaper, boneobj_backfin_endtaper,
    boneobj_genmap
};
#define POINTHELP_CLASS_ID 0x00000c80
#define SKIN_CLASSID      Class_ID(0x00009912, 0x00000000)
#define TRIOBJ_CLASS_ID   0x00000009
#define EDITTRIOBJ_CLASS_ID 0xe44f10b3
#define DUMMY_CLASS_ID    0x876234

// ------------------------------------------------------------
//  Zeit
// ------------------------------------------------------------
using TimeValue = int;
#define TIME_TICKSPERSEC 4800
inline TimeValue GetTicksPerFrame() { return 160; }
inline int GetFrameRate() { return 30; }

class Interval {
public:
    Interval() = default;
    Interval(TimeValue s, TimeValue e) : start_(s), end_(e) {}
    TimeValue Start() const { return start_; }
    TimeValue End() const { return end_; }
    void SetStart(TimeValue t) { start_ = t; }
    void SetEnd(TimeValue t) { end_ = t; }
private:
    TimeValue start_ = 0;
    TimeValue end_ = 0;
};

#define FOREVER Interval(-1, 1)

// ------------------------------------------------------------
//  Geometrie
// ------------------------------------------------------------
class Point2 {
public:
    float x = 0.0f, y = 0.0f;
    Point2() = default;
    Point2(float ax, float ay) : x(ax), y(ay) {}
};

class Point3 {
public:
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Point3() = default;
    Point3(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
    Point3 operator+(const Point3& o) const { return Point3(x + o.x, y + o.y, z + o.z); }
    Point3 operator-(const Point3& o) const { return Point3(x - o.x, y - o.y, z - o.z); }
    Point3 operator*(float s) const { return Point3(x * s, y * s, z * s); }
    float& operator[](int i) { return (i == 0) ? x : (i == 1) ? y : z; }
    float operator[](int i) const { return (i == 0) ? x : (i == 1) ? y : z; }
};

class Point4 {
public:
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    Point4() = default;
    Point4(float ax, float ay, float az, float aw) : x(ax), y(ay), z(az), w(aw) {}
};

using Color = Point3;

class Quat {
public:
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    Quat() = default;
    Quat(float ax, float ay, float az, float aw) : x(ax), y(ay), z(az), w(aw) {}
    Quat Inverse() const { return Quat(-x, -y, -z, w); }
    void Normalize() {}
};

class AngAxis {
public:
    Point3 axis;
    float angle = 0.0f;
    AngAxis() = default;
    AngAxis(const Point3& a, float ang) : axis(a), angle(ang) {}
};

class Matrix3 {
public:
    Matrix3() = default;
    explicit Matrix3(int) { IdentityMatrix(); }
    void IdentityMatrix() {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) m_[r][c] = (r == c) ? 1.0f : 0.0f;
    }
    void SetRow(int r, const Point3& p) { m_[r][0] = p.x; m_[r][1] = p.y; m_[r][2] = p.z; }
    Point3 GetRow(int r) const { return Point3(m_[r][0], m_[r][1], m_[r][2]); }
    void SetTrans(const Point3& p) { SetRow(3, p); }
    Point3 GetTrans() const { return GetRow(3); }
    Matrix3 operator*(const Matrix3& o) const { return o; }
    Matrix3& operator*=(const Matrix3&) { return *this; }
    Point3 operator*(const Point3& p) const { return p; }
private:
    float m_[4][3] = {};
};

inline Matrix3 Inverse(const Matrix3& m) { return m; }
inline Matrix3 RotateXMatrix(float) { return Matrix3(1); }
inline Point3 Normalize(const Point3& p) { return p; }
inline float Length(const Point3&) { return 0.0f; }

// ------------------------------------------------------------
//  Tab<T> - Autodesks Array
// ------------------------------------------------------------
template <typename T>
class Tab {
public:
    Tab() = default;
    ~Tab() { delete[] data_; }
    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;
    void SetCount(int n) {
        delete[] data_;
        data_ = (n > 0) ? new T[static_cast<size_t>(n)]() : nullptr;
        count_ = n;
    }
    int Count() const { return count_; }
    T& operator[](int i) { return data_[i]; }
    const T& operator[](int i) const { return data_[i]; }
    void Append(int n, T* items) { (void)n; (void)items; }
    void ZeroCount() { count_ = 0; }
private:
    T* data_ = nullptr;
    int count_ = 0;
};

// ------------------------------------------------------------
//  Mesh
// ------------------------------------------------------------
using MtlID = unsigned short;

class Face {
public:
    DWORD v[3] = {0, 0, 0};
    DWORD smGroup = 0;
    void setVerts(int a, int b, int c) {
        v[0] = static_cast<DWORD>(a);
        v[1] = static_cast<DWORD>(b);
        v[2] = static_cast<DWORD>(c);
    }
    void setEdgeVisFlags(int, int, int) {}
    void setMatID(MtlID id) { (void)id; }
};

class TVFace {
public:
    DWORD t[3] = {0, 0, 0};
    void setTVerts(int a, int b, int c) {
        t[0] = static_cast<DWORD>(a);
        t[1] = static_cast<DWORD>(b);
        t[2] = static_cast<DWORD>(c);
    }
};

class Mesh {
public:
    int numVerts = 0;
    int numFaces = 0;
    Point3* verts = nullptr;
    Face* faces = nullptr;
    void setNumVerts(int n) { numVerts = n; }
    void setNumFaces(int n) { numFaces = n; }
    void setVert(int, const Point3&) {}
    Point3& getVert(int) { static Point3 p; return p; }
    void setNumMaps(int, BOOL = FALSE) {}
    void setMapSupport(int, BOOL = TRUE) {}
    void setNumMapVerts(int, int, BOOL = FALSE) {}
    void setMapVert(int, int, const Point3&) {}
    void setNumMapFaces(int, int, BOOL = FALSE) {}
    TVFace* mapFaces(int) { static TVFace f; return &f; }
    Point3* mapVerts(int) { static Point3 p; return &p; }
    void InvalidateGeomCache() {}
    void InvalidateTopologyCache() {}
    void buildNormals() {}
    void BuildStripsAndEdges() {}
    void SpecifyNormals() {}
};

// ------------------------------------------------------------
//  Referenzsystem
// ------------------------------------------------------------
class ReferenceTarget;
class ReferenceMaker;
class Animatable;

class Animatable {
public:
    virtual ~Animatable() = default;
    virtual Class_ID ClassID() { return Class_ID(); }
    virtual SClass_ID SuperClassID() { return 0; }
    virtual void DeleteThis() { delete this; }
    virtual void GetClassName(MSTR& s, bool = true) { s = _T("stub"); }
    virtual int NumSubs() { return 0; }
    virtual Animatable* SubAnim(int) { return nullptr; }
    // Der Name ist AddNoteTrack (Animatable.h). Der Stub hatte
    // AppendNoteTrack erfunden - der echte Build meldete C2039.
    void AddNoteTrack(class NoteTrack*) {}
    void DeleteNoteTrack(class NoteTrack*, BOOL = TRUE) {}
    BOOL HasNoteTracks() { return FALSE; }
    int NumNoteTracks() { return 0; }
    class NoteTrack* GetNoteTrack(int) { return nullptr; }
    virtual void* GetInterface(ULONG) { return nullptr; }
};

class ReferenceMaker : public Animatable {};
class ReferenceTarget : public ReferenceMaker {};

// ------------------------------------------------------------
//  Note Tracks
// ------------------------------------------------------------
// Nachgeschlagen in notetrck.h (Autodesk-Doku 2016):
//     NoteKey(TimeValue t, const MSTR& n, DWORD f = 0)
// KEIN Standardkonstruktor. Der Stub hatte hier zuerst einen,
// und der echte Build meldete C2512.
class NoteKey {
public:
    NoteKey(TimeValue t, const MSTR& n, DWORD f = 0)
        : time(t), note(n), flags(f) {}
    TimeValue time;
    MSTR note;
    DWORD flags;
};

using NoteKeyTab = Tab<NoteKey*>;

class NoteTrack : public ReferenceTarget {};

class DefNoteTrack : public NoteTrack {
public:
    NoteKeyTab keys;
};

// ------------------------------------------------------------
//  Controller
// ------------------------------------------------------------
enum GetSetMethod { CTRL_RELATIVE, CTRL_ABSOLUTE };

class Control : public ReferenceTarget {
public:
    virtual void SetValue(TimeValue, void*, int = 1,
                          GetSetMethod = CTRL_ABSOLUTE) {}
    virtual void GetValue(TimeValue, void*, Interval&,
                          GetSetMethod = CTRL_ABSOLUTE) {}

    // Control::SetPositionController / SetRotationController /
    // SetScaleController - an einem Transform-Controller, um die
    // Unter-Controller auszutauschen.
    BOOL SetPositionController(Control*) { return TRUE; }
    BOOL SetRotationController(Control*) { return TRUE; }
    BOOL SetScaleController(Control*) { return TRUE; }
};

class IKeyControl {
public:
    virtual ~IKeyControl() = default;
    virtual int GetNumKeys() = 0;
    virtual void SetNumKeys(int) = 0;
    virtual void AppendKey(void*) = 0;
    virtual void SortKeys() = 0;
};

inline IKeyControl* GetKeyControlInterface(Animatable*) { return nullptr; }

// ------------------------------------------------------------
//  Objekte und Knoten
// ------------------------------------------------------------
// IParamBlock2 steht im echten SDK in iparamb2.h; die Attrappe
// liegt hier, damit die Reihenfolge der Includes keine Rolle
// spielt.
class IParamBlock2;

class Object : public ReferenceTarget {
public:
    virtual void SetName(const MCHAR*) {}
    // GetParamBlockByID sitzt an Animatable und nimmt eine
    // Block-ID (short), nicht einen laufenden Index.
    virtual IParamBlock2* GetParamBlockByID(short) { return nullptr; }
};

class IParamBlock2 : public ReferenceTarget {
public:
    // SetValue ist ueberladen; der Rueckgabewert ist BOOL.
    virtual BOOL SetValue(int, TimeValue, float) { return TRUE; }
    virtual BOOL SetValue(int, TimeValue, int) { return TRUE; }
    virtual BOOL SetValue(int, TimeValue, Point3&) { return TRUE; }
    virtual BOOL GetValue(int, TimeValue, float&, Interval&) { return TRUE; }
};

class GeomObject : public Object {};

class TriObject : public GeomObject {
public:
    Mesh mesh;
    Mesh& GetMesh() { return mesh; }
};

inline TriObject* CreateNewTriObject() { return new TriObject(); }

class Mtl;
class Texmap;

class INode : public ReferenceTarget {
public:
    virtual void SetName(const MCHAR*) {}
    virtual const MCHAR* GetName() { return _T("stub"); }
    virtual void SetNodeTM(TimeValue, Matrix3&) {}
    virtual Matrix3 GetNodeTM(TimeValue, Interval* = nullptr) { return Matrix3(1); }
    virtual Matrix3 GetObjectTM(TimeValue, Interval* = nullptr) { return Matrix3(1); }
    virtual Matrix3 GetObjTMAfterWSM(TimeValue, Interval* = nullptr) { return Matrix3(1); }
    virtual void AttachChild(INode*, int = 1) {}
    virtual INode* GetParentNode() { return nullptr; }
    // Die Namen sind NumberOfChildren/GetChildNode - nicht
    // NumChildren/GetChild, wie man vermuten wuerde.
    virtual int NumberOfChildren() { return 0; }
    virtual INode* GetChildNode(int) { return nullptr; }
    virtual void SetMtl(Mtl*) {}
    virtual Mtl* GetMtl() { return nullptr; }
    virtual Object* GetObjectRef() { return nullptr; }
    virtual void SetObjectRef(Object*) {}
    virtual void SetWireColor(DWORD) {}
    virtual void BoneAsLine(int) {}
    virtual void ShowBone(int) {}
    virtual void SetRenderable(int) {}
    virtual void SetPrimaryVisibility(BOOL) {}
    virtual void SetSecondaryVisibility(BOOL) {}
    virtual void SetVisibility(TimeValue, float) {}
    virtual Control* GetVisController() { return nullptr; }
    virtual void SetVisController(Control*) {}
    virtual void SetTMController(Control*) {}
    virtual Control* GetTMController() { return nullptr; }
    virtual void InvalidateTreeTM() {}
    virtual void SetUserPropString(const MCHAR*, const MCHAR*) {}
};

inline DWORD RGB(int r, int g, int b) {
    return static_cast<DWORD>(r) | (static_cast<DWORD>(g) << 8) |
           (static_cast<DWORD>(b) << 16);
}

// ------------------------------------------------------------
//  Modifikatoren und Skin
// ------------------------------------------------------------
// Modifier erbt GetInterface von Animatable - ueber sie holt man
// ISkinImportData. Keine eigene Deklaration noetig.
class Modifier : public ReferenceTarget {};

class ISkin;

class ISkinImportData {
public:
    virtual ~ISkinImportData() = default;
    virtual BOOL AddBoneEx(INode*, BOOL) = 0;
    virtual BOOL SetSkinTm(INode*, Matrix3, Matrix3) = 0;
    virtual BOOL AddWeights(INode*, int, Tab<INode*>&, Tab<float>&) = 0;
};

#define I_SKINIMPORTDATA 0x00020000
#define I_SKIN 0x00010000

class ISkin {
public:
    virtual ~ISkin() = default;
    virtual int GetNumBones() = 0;
};

// ------------------------------------------------------------
//  Material
// ------------------------------------------------------------
class MtlBase : public ReferenceTarget {
public:
    virtual void SetName(const MCHAR*) {}
};

class Texmap : public MtlBase {};

// Mtl ist die Basisklasse und kennt WENIGER als StdMat2.
// SetTwoSided und SetOpacity sitzen an StdMat/StdMat2, nicht
// hier - der Stub hatte sie faelschlich an Mtl, und der echte
// Build meldete zweimal C2039.
class Mtl : public MtlBase {
public:
    // Mtl::SetActiveTexmap - "Stores the pointer to the active
    // texture map used in the interactive renderer."
    void SetActiveTexmap(Texmap*) {}
public:
    virtual void SetSubTexmap(int, Texmap*) {}
    virtual void SetAmbient(Color, TimeValue) {}
    virtual void SetDiffuse(Color, TimeValue) {}
    virtual void SetSpecular(Color, TimeValue) {}
};

class StdMat : public Mtl {
public:
    virtual void SetOpacity(float, TimeValue) {}
    virtual void SetTwoSided(int) {}
    virtual void SetTexmapAmt(int, float, TimeValue) {}
    virtual void EnableMap(int, BOOL) {}
};

class StdMat2 : public StdMat {};

class BitmapTex : public Texmap {
public:
    void SetMapName(const MCHAR*) {}
    void SetAlphaSource(int) {}
};

class MultiMtl : public Mtl {
public:
    void SetNumSubMtls(int) {}
    void SetSubMtl(int, Mtl*) {}
};

#define ID_DI 1
#define ID_OP 6
#define ALPHA_FILE 0

// ------------------------------------------------------------
//  Undo und Animationszustand
// ------------------------------------------------------------
class Hold {
public:
    void Suspend() {}
    void Resume() {}
    void Begin() {}
    void Accept(const MCHAR*) {}
    void Cancel() {}
};
extern Hold theHold;

inline void AnimateOn() {}
inline void AnimateOff() {}
inline void SuspendAnimate() {}
inline void ResumeAnimate() {}
inline BOOL Animating() { return FALSE; }

// ------------------------------------------------------------
//  Interface
// ------------------------------------------------------------
class ImpInterface {
public:
    virtual ~ImpInterface() = default;
};

class Interface {
public:
    // Interface::ActivateTexture - der dokumentierte Weg, eine
    // Textur im Viewport anzuzeigen.
    void ActivateTexture(Texmap*, Mtl*, int = -1) {}
public:
    virtual ~Interface() = default;
    virtual void* CreateInstance(SClass_ID, Class_ID) { return nullptr; }
    virtual INode* CreateObjectNode(Object*) { return nullptr; }
    virtual INode* GetRootNode() { return nullptr; }
    virtual void DeleteNode(INode*, BOOL = TRUE, BOOL = TRUE) {}
    virtual void SelectNode(INode*, int = 1) {}
    virtual void ClearNodeSelection(BOOL = TRUE) {}
    virtual void DisableSceneRedraw() {}
    virtual void EnableSceneRedraw() {}
    virtual void RedrawViews(TimeValue, DWORD = 0, ReferenceTarget* = nullptr) {}
    virtual void SetAnimRange(const Interval&) {}
    virtual Interval GetAnimRange() { return Interval(0, 0); }
    virtual void SetTime(TimeValue, BOOL = TRUE) {}
    virtual TimeValue GetTime() { return 0; }
    virtual void PushPrompt(const MCHAR*) {}
    virtual void PopPrompt() {}
    virtual void ReplacePrompt(const MCHAR*) {}
    virtual HWND GetMAXHWnd() { return nullptr; }
    // KEIN AddModifier hier. Die Methode sitzt auf Interface12,
    // nicht auf Interface - Modifier gehen ueber
    // CreateDerivedObject und IDerivedObject::AddModifier.
    // Der Stub hatte sie faelschlich, der echte Build meldete
    // C2039.
    virtual const MCHAR* GetDir(int) { return _T(""); }
};

inline Interface* GetCOREInterface() { return nullptr; }

// UtilityObj und IUtil stehen im echten SDK in utilapi.h, das
// max.h NICHT mitzieht. Sie sind deshalb dort nachgebildet und
// bewusst nicht hier - sonst uebersetzt ein Quelltext ohne
// #include <utilapi.h> gegen den Stub und scheitert am echten
// SDK. Genau das ist passiert (C2504, C2061, C3668).

// ------------------------------------------------------------
//  MAXScript
// ------------------------------------------------------------
#if MAX_RELEASE >= 24000
namespace MAXScript {
enum class ScriptSource { NotSpecified, Embedded, NonEmbedded, Dynamic };
}
inline BOOL ExecuteMAXScriptScript(const MCHAR*, MAXScript::ScriptSource,
                                   BOOL = FALSE, void* = nullptr,
                                   BOOL = FALSE) { return TRUE; }
#else
inline BOOL ExecuteMAXScriptScript(const MCHAR*, BOOL = FALSE,
                                   void* = nullptr) { return TRUE; }
#endif

#endif  // EG3D_MAXSTUB_H
