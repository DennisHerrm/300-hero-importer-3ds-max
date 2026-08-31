// ============================================================
//  EG3D Import - Max-Schicht
//
//  DIE EINZIGEN DATEIEN, DIE MAX-HEADER EINBINDEN, SIND
//  eg3dimport.h, eg3dimport.cpp UND eg3dimport_dll.cpp.
//
//  Alles unter src/eg3d_*.* kommt ohne Max aus und laesst sich
//  deshalb mit eg3ddump.exe in Sekunden pruefen. Wer hier eine
//  Parserfunktion einbaut, verliert genau das - dann faellt ein
//  Formatfehler erst nach einem SDK-Build auf.
// ============================================================
#ifndef EG3D_IMPORT_H
#define EG3D_IMPORT_H

#define EG3DIMPORT_VERSION_STR _T("0.1.0")
#define EG3DIMPORT_VERSION_NUM 100

// ------------------------------------------------------------
//  Klassen-IDs
//
//  Selbst gewuerfelt, nicht aus einem Beispiel abgeschrieben.
//  Eine Kollision mit einem anderen Plugin ergibt einen
//  Ladefehler, den niemand seinem eigenen Plugin zuordnet.
// ------------------------------------------------------------
#define EG3DIMPORT_CLASS_ID       Class_ID(0x5c3a71e2, 0x14d90b76)
#define EG3DIMPORT_SCENE_CLASS_ID Class_ID(0x5c3a71e3, 0x14d90b77)
#define EG3DIMPORT_INTERFACE_ID   Interface_ID(0x5c3a71e4, 0x14d90b78)

// max.h zieht Windows mit herein; NOMINMAX und
// WIN32_LEAN_AND_MEAN setzt das Buildsystem, nicht dieser Header.
#include <max.h>

// Die folgenden Header zieht NICHT jedes SDK ueber max.h mit
// herein. Max 2016 ist der strengste Fall - dort fehlt sonst die
// Basisklasse, und danach meldet jede override-Methode einen
// eigenen Fehler, der nichts mit der Ursache zu tun hat.
//
// PRUEFE_INCLUDES.py haelt die Zuordnung Symbol -> Header
// aktuell; wer ein neues SDK-Symbol benutzt und den Header
// vergisst, faellt dort auf statt erst im 2016er-Build.
#include <utilapi.h>    // UtilityObj, IUtil
#include <iparamb2.h>   // ClassDesc2, Parameterbloecke
#include <ifnpub.h>     // FPStaticInterface, Interface_ID, p_end
#include <hold.h>       // theHold
#include <object.h>     // Object, GeomObject
#include <impexp.h>     // SceneImport, ImpInterface
#include <imtl.h>       // Mtl, ID_DI, ID_OP
#include <istdplug.h>   // MultiMtl, GetKeyControlInterface
#include <stdmat.h>     // StdMat2, BitmapTex, ALPHA_FILE
#include <triobj.h>     // TriObject, CreateNewTriObject
#include <notetrck.h>   // DefNoteTrack, NoteKey, NoteKeyTab
#include <modstack.h>   // IDerivedObject
#include <iskin.h>      // ISkin, ISkinImportData
#include <control.h>    // Control, NewDefaultFloatController
#include <decomp.h>     // AffineParts, decomp_affine
#include <maxscript/maxscript.h>   // ExecuteMAXScriptScript

#include "eg3d_dump.h"
#include "eg3d_model.h"
#include "eg3d_x.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ------------------------------------------------------------
//  Class-IDs der Standardmaterialien
//
//  Das sind Standard-Defines, aber je nach SDK-Jahrgang liegen
//  sie in unterschiedlichen Headern. Die Werte unten sind die
//  offiziellen aus plugapi.h; dieselbe Absicherung steht in
//  XFBIN Import, aus demselben Grund.
//
//  Es sind PartA-Werte, keine Class_ID: der Aufruf muss
//  Class_ID(DMTL_CLASS_ID, 0) lauten. CreateInstance nimmt eine
//  Class_ID, und ein blankes int dort ergibt C2664.
// ------------------------------------------------------------
#ifndef DMTL_CLASS_ID
#define DMTL_CLASS_ID 0x00000002
#endif
#ifndef MULTI_CLASS_ID
#define MULTI_CLASS_ID 0x00000200
#endif
#ifndef BMTEX_CLASS_ID
#define BMTEX_CLASS_ID 0x00000240
#endif

namespace eg3d {

// ============================================================
//  RAII-Guards
//
//  Alle drei machen dasselbe Versprechen: was der Konstruktor
//  abschaltet, schaltet der Destruktor wieder an - auch bei einer
//  Ausnahme, auch bei einem fruehen return. Genau dafuer gibt es
//  RAII, und genau hier ist es kein Stilmittel: ein Importlauf
//  hat ein Dutzend Abbruchstellen, und ein vergessenes
//  EnableSceneRedraw hinterlaesst ein totes Max.
// ============================================================

// Haelt die Viewports an. 300 Knoten einzeln zu zeichnen dauert
// laenger als der ganze Import.
class SceneRedrawGuard {
public:
    explicit SceneRedrawGuard(Interface* ip) : ip_(ip) {
        if (ip_ != nullptr) ip_->DisableSceneRedraw();
    }
    ~SceneRedrawGuard() {
        if (ip_ != nullptr) ip_->EnableSceneRedraw();
    }
    SceneRedrawGuard(const SceneRedrawGuard&) = delete;
    SceneRedrawGuard& operator=(const SceneRedrawGuard&) = delete;
    SceneRedrawGuard(SceneRedrawGuard&&) = delete;
    SceneRedrawGuard& operator=(SceneRedrawGuard&&) = delete;
private:
    Interface* ip_;
};

// Haelt das Undo-System an. theHold zaehlt Suspend/Resume intern,
// Verschachteln ist also erlaubt. Ohne das legt Max je Knoten
// einen Undo-Record an - bei 300 Knoten und 3000 Keys ist das der
// Unterschied zwischen Sekunden und Minuten, und ein spaeteres
// Undo laesst eine halb zurueckgerollte Szene stehen.
class HoldSuspendGuard {
public:
    HoldSuspendGuard() { theHold.Suspend(); }
    ~HoldSuspendGuard() { theHold.Resume(); }
    HoldSuspendGuard(const HoldSuspendGuard&) = delete;
    HoldSuspendGuard& operator=(const HoldSuspendGuard&) = delete;
    HoldSuspendGuard(HoldSuspendGuard&&) = delete;
    HoldSuspendGuard& operator=(HoldSuspendGuard&&) = delete;
};

// ------------------------------------------------------------
//  AnimateGuard - der wichtigste der drei
//
//  Ohne AnimateOn() erzeugt SetValue KEINE Keys. Es setzt
//  stillschweigend den Wert des Controllers und meldet Erfolg.
//  Das ist der Fehler, der bei jedem Importer einmal passiert:
//  der Import laeuft durch, meldet 3000 Keys, und die Zeitleiste
//  ist leer.
//
//  SuspendAnimate() davor, weil der Benutzer den Auto-Key-Knopf
//  gedrueckt haben kann. Der Guard stellt beim Verlassen genau
//  den Zustand wieder her, den er vorgefunden hat.
// ------------------------------------------------------------
class AnimateGuard {
public:
    AnimateGuard() {
        SuspendAnimate();
        AnimateOn();
    }
    ~AnimateGuard() {
        AnimateOff();
        ResumeAnimate();
    }
    AnimateGuard(const AnimateGuard&) = delete;
    AnimateGuard& operator=(const AnimateGuard&) = delete;
    AnimateGuard(AnimateGuard&&) = delete;
    AnimateGuard& operator=(AnimateGuard&&) = delete;
};

// ============================================================
//  Msg - Meldungen ohne feste Puffergroesse
//
//  Die Meldungen enthalten Namen aus der Datei: Knoten, Clips,
//  Materialien, Pfade. Deren Laenge steht nicht im Voraus fest.
//
//  Mit swprintf_s in ein wchar_t[N] geschrieben ist das eine
//  Falle mit Ansage: swprintf_s erkennt den Ueberlauf, ruft den
//  Invalid-Parameter-Handler, und der BEENDET DEN PROZESS. Max
//  stuerzt mit STATUS_INVALID_PARAMETER ab, ohne dass irgendwo
//  ein Nullzeiger im Spiel waere. Bei XFBIN Import ist genau das
//  passiert - eine Warnung mit 39 Namen darin, 656 Zeichen lang,
//  Puffer 420.
// ============================================================
class Msg {
public:
    template <typename T>
    Msg& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    // Zahl mit fester Nachkommastelle, wie %.1f
    Msg& Fixed(double value, int digits = 1) {
        const std::streamsize oldPrecision = stream_.precision(digits);
        const std::ios::fmtflags oldFlags = stream_.flags();
        stream_ << std::fixed << value;
        stream_.flags(oldFlags);
        stream_.precision(oldPrecision);
        return *this;
    }

    std::wstring str() const { return stream_.str(); }

    // Damit Log(Msg() << ...) auch ohne abschliessendes .str()
    // uebersetzt. Nimmt die Moeglichkeit weg, es zu vergessen.
    operator std::wstring() const { return stream_.str(); }

private:
    std::wostringstream stream_;
};

class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    double ms() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start_).count();
    }
    void reset() { start_ = std::chrono::steady_clock::now(); }
private:
    std::chrono::steady_clock::time_point start_;
};

// ============================================================
//  Importoptionen
//
//  Ein Aggregat mit Vorgaben statt einer langen Parameterliste.
//  Wer eine Option ergaenzt, muss keine Aufrufstelle anfassen -
//  und niemand vertauscht zwei bool-Argumente.
// ============================================================
struct ImportOptions {
    float scale = 1.0f;            // Einheitenfaktor
    bool  buildSkeleton = true;
    bool  buildMeshes = true;
    bool  buildSkin = true;
    bool  buildMaterials = true;
    bool  buildAnimation = true;
    bool  sequenceMode = true;     // alle Clips hintereinander
    bool  noteTrack = true;        // Clipnamen als Notizen
    // ------------------------------------------------------
    //  Sichtbarkeitsspuren
    //
    //  STANDARD AUS. Die active/enable-Kanaele legen einen
    //  Float-Controller auf die Sichtbarkeit des Knotens - auch
    //  auf Bones. Im Viewport sieht man dann Bones, die im Lauf
    //  der Animation verschwinden und wiederkommen, was beim
    //  Rigging stoert und beim ersten Blick verwirrt.
    //
    //  Die Daten sind im Format vorhanden und richtig gelesen
    //  (1093 active-Kanaele im Bestand); wer sie braucht,
    //  schaltet sie ein.
    //  Wieder AN, weil sie jetzt nur noch auf MESHES landen -
    //  siehe BuildModelAnimation. Auf Bones gehoerten sie nie.
    bool  visibilityTracks = true;
    bool  skipParticleTemplates = true;
    // Echte Bone-Objekte statt Point-Helper. Standard AN: Bones
    // sind das, was ein Rigger erwartet, und der Skin-Modifier
    // nimmt beide gleichermassen. Point-Helper bleiben als
    // Rueckfallebene - wenn CreateInstance das Bone-Objekt nicht
    // liefert, entsteht ein Helper statt gar nichts.
    bool  boneObjects = true;

    // Ausfuehrliches Protokoll. Kostet Zeit und Speicher, deshalb
    // aus - aber wenn ein Import schiefgeht, ist es das Erste,
    // was man einschaltet.
    //  Immer AN. Der Schalter dafuer stand meistens aus - genau
    //  dann, wenn etwas schieflief und man die Zeilen gebraucht
    //  haette. Ein Import dauert Sekunden; die paar hundert
    //  Zeilen mehr kosten nichts und ersparen einen zweiten Lauf.
    bool  verbose = true;

    // ------------------------------------------------------
    //  Ruheframes vor der Animation
    //
    //  Die .x-Dateien haben KEINE separate Bind-Pose: Frame 0
    //  ist zugleich der erste Frame des ersten Clips (bei
    //  063_skin7.x startet "bat_idle" bei Frame 0, und schon
    //  zwischen Frame 0 und 1 bewegt sich etwas).
    //
    //  Mit einem Versatz > 0 wird die Animation nach hinten
    //  geschoben und Frame 0 traegt nur die Ruhelage - eine
    //  unbewegte Pose zum Ansehen und als Referenz fuer den
    //  Skin-Modifier. Es ist keine echte T-Pose; die steht
    //  nirgends in der Datei.
    //
    //  Derselbe Versatz trennt auch die Clips im Sequenzmodus
    //  voneinander, damit das Ende des einen nicht in den
    //  Anfang des naechsten interpoliert.
    // ------------------------------------------------------
    //  STANDARD 0 - am Modell bestimmt.
    //
    //  Mit Ruheframes > 0 steht bei Frame 0 die BIND-POSE (von
    //  BuildModelNodes gesetzt, ohne Key) und der erste
    //  Animationskey liegt bei Frame 5. Max interpoliert
    //  dazwischen - und genau in diesen fuenf Frames dreht und
    //  skaliert sich alles, was zwischen den beiden Raeumen
    //  liegt. Am Pelvis beider Charaktere und an mehreren Waffen
    //  ist es am deutlichsten.
    //
    //  Es ist also kein Problem einzelner Bones, sondern der
    //  UEBERGANG von der Bind-Pose in die Animation. Ohne
    //  Ruheframes gibt es ihn nicht: die Animation beginnt bei
    //  Frame 0, und was dort steht, ist der erste Clipframe.
    //
    //  Wer die Ruhepose sehen will, schaltet sie wieder ein -
    //  dann ist der Uebergang sichtbar, aber er ist dann auch
    //  gewollt.
    int   restFrames = 0;

    // ------------------------------------------------------
    //  Keyzeiten auf ganze Frames runden
    //
    //  Die Datei speichert die Deltas als gerundete
    //  Millisekunden: 33, 66, 100 statt 33.333, 66.667, 100.
    //  Direkt in Ticks gerechnet sind 33 ms bei 30 fps
    //  0.99 Frames - ein Prozent Fehler je Key, der sich
    //  aufsummiert. Nach hundert Keys ist ein ganzer Frame
    //  Versatz da, und die Keys liegen zwischen den Frames.
    //
    //  Diese Korrektur ist unabhaengig von allem anderen und
    //  laesst sich einzeln pruefen.
    // ------------------------------------------------------
    bool  snapKeysToFrames = true;

    // ------------------------------------------------------
    //  Achsenkorrektur an der WURZEL
    //
    //  Die Dateien sind Y-up (Engine-Konvention), 3ds Max ist
    //  Z-up. Das ist der ganze Unterschied - und weil er das
    //  ganze Modell betrifft, gehoert er an die Wurzel und nicht
    //  an jeden einzelnen Bone.
    //
    //  Eine Drehung um +90 Grad um X auf die WURZELKNOTEN. Alle
    //  Kinder erben sie ueber die Hierarchie, die Animation
    //  bleibt in sich unveraendert - sie ist ja relativ zum
    //  Elternteil.
    //
    //  Das ist der Weg, den jeder Importer geht, und es ist der
    //  Grund, warum die Versuche mit Korrekturmatrizen je Bone
    //  scheitern mussten: sie greifen an 176 Stellen ein, wo ein
    //  einziger Eingriff genuegt.
    //
    //  NUR FUER .model. Die .x-Dateien kommen bereits Z-up an -
    //  dort steht ohne Zutun alles richtig, und eine Drehung
    //  wuerde kaputtmachen, was funktioniert.
    // ------------------------------------------------------
    //  STANDARD AUS: in 0.1.8 lag die Drehung in der ANIMATION,
    //  nicht in der Ruhelage - eine Wurzeldrehung dreht aber
    //  beides. Zuschaltbar, damit sich pruefen laesst, ob sie
    //  hier hilft.
    //  Wirkt NUR auf die Animation, nicht auf die Ruhelage.
    //
    //  STANDARD AUS - am Modell bestaetigt. Die Kombination, mit
    //  der 039_skin8.model richtig ankommt (Bones und Mesh
    //  deckungsgleich), ist:
    //
    //      bindPoseFromInvBind  AN
    //      bindScaleOnly        AN   (misst, wendet nicht an)
    //      snapKeysToFrames     AN
    //      upAxisFix            AUS
    //      meshAxisFix          AUS
    //      bindIncludesMesh     AUS
    //      retargetAnimation    AUS
    //      matchBindScale       AUS
    //
    //  Das ist im Kern 0.1.8 plus zwei Korrekturen: die
    //  Keyzeiten auf ganze Frames (33 ms sind 0.99 Frames, der
    //  Fehler summiert sich) und AttachChild mit keepTM = 1 bei
    //  starren Meshes (ihre Vertices stehen schon absolut, die
    //  Traegermatrix kam ein zweites Mal obendrauf).
    //
    //  Die uebrigen Optionen bleiben als Werkzeuge fuer Dateien
    //  drin, bei denen es anders liegt - sie sind alle einzeln
    //  schaltbar und einzeln zu beurteilen.
    bool  upAxisFix = false;

    // ------------------------------------------------------
    //  Animationskorrektur an den WURZELN
    //
    //  Der Stand 0.1.8 war bei Skinning, Bonelage und Massstab
    //  richtig - Ruhelage und Meshes passten zusammen. Nur ab dem
    //  ersten Animationsframe kippte das ganze Skelett um 90 Grad
    //  in X und wurde etwas kleiner.
    //
    //  Beides zusammen ist EINE Matrix: eine Drehung um -90 Grad
    //  um X mal dem Bindfaktor der jeweiligen Palette. Sie wird
    //  nur an den WURZELN angewandt - bei den Kindern kuerzt sie
    //  sich in der relativen Rechnung weg, genau wie es der
    //  Kommentar im Blender-Addon fuer die Drehung beschreibt.
    //
    //  Der Massstab kommt je Teilbaum aus der Palette, in der
    //  seine Bones stehen (039_skin8.model: 0.592375 fuer den
    //  ersten Charakter, 1.0 fuer den zweiten). Ein globaler Wert
    //  traefe nur einen von beiden.
    // ------------------------------------------------------
    bool  animRootFix = false;

    // ------------------------------------------------------
    //  Animation auf den Bindmassstab bringen - JE BONE
    //
    //  Am Szenendump gemessen, ueber alle 270 Bones:
    //
    //      Bip001 Pelvis   f0: 1.688 1.688 1.688
    //                      f30: 1.000 1.000 1.000
    //
    //  105 von 270 Bones springen von 1.688 auf 1.000. Das sind
    //  genau die, deren Ruhelage aus den Bindmatrizen kommt
    //  (Massstab 1/0.592375 = 1.688) - die Animation liefert sie
    //  aus den meta[7]-Kanaelen mit Massstab 1.0.
    //
    //  Es ist also KEIN Drehungsproblem, sondern ein
    //  Massstabssprung. Die Korrektur ist entsprechend eine
    //  reine Skalierung, je Bone mit dem Faktor SEINER Palette -
    //  nicht an den Wurzeln, denn hier kuerzt sich nichts weg:
    //  jeder Bone bekommt seine Weltmatrix absolut gesetzt.
    // ------------------------------------------------------
    bool  animBindScale = true;

    // ------------------------------------------------------
    //  Mesh-Achse: -90 Grad um X auf die VERTICES
    //
    //  Das Blender-Addon haelt fest: "The engine's world space
    //  is Y up, the mesh is authored Z up." Beides steht in
    //  derselben Datei, und welche Seite gedreht werden muss,
    //  haengt davon ab, aus welcher Quelle die Knoten kommen.
    //
    //  Mit Bind-Pose aus den Bindmatrizen stehen die Bones
    //  aufrecht (nachgemessen: Z = 0.00 .. 3.05) und das
    //  Hauptmesh ebenfalls (Z = 0.00 .. 3.46) - im Viewport
    //  liegt es trotzdem flach. Diese Option dreht die Vertices
    //  nach, ohne die Bones anzufassen.
    //
    //  STANDARD AN. Am Modell bestimmt: mit "Use Time 0 as
    //  Reference Pose" (bindPoseFromInvBind AUS) stehen Skelett
    //  und Animation richtig und im selben Raum, das Mesh liegt
    //  flach - und -90 Grad um X stellt es auf.
    //
    //  Damit ist das Achsenpaar aus dem Addon-Kommentar
    //  vollstaendig aufgeloest: "The engine's world space is Y
    //  up, the mesh is authored Z up." Die Knoten kommen aus dem
    //  Y-up-Raum, die Vertices aus dem Z-up-Raum, und genau eine
    //  Drehung bringt sie zusammen.
    // ------------------------------------------------------
    bool  meshAxisFix = false;

    // ------------------------------------------------------
    //  V-Achse der UVs umdrehen
    //
    //  Diese Formate kommen aus einer DirectX-Pipeline, dort
    //  zeigt V nach UNTEN. 3ds Max und Blender rechnen mit V
    //  nach oben - ohne Umdrehen sitzt die Textur senkrecht
    //  gespiegelt auf dem Modell.
    //
    //  Das Blender-Addon hat dieselbe Option, ebenfalls mit
    //  Standard AN ("DirectX-UVs zeigen mit V nach unten").
    //
    //  Standard AN. Wer eine Datei findet, bei der es umgekehrt
    //  ist, schaltet es ab.
    // ------------------------------------------------------
    bool  flipUvV = true;

    // ------------------------------------------------------
    //  Lineare Controller statt TCB
    //
    //  Max legt beim Setzen von Keys TCB-Controller an, deren
    //  Continuity-Standard 25 zwischen jedem Keypaar
    //  ueberschwingt. Bei gebackenen Daten mit einem Key je
    //  Frame ist das reines Zittern.
    //
    //  Standard AN - dasselbe macht das Blender-Addon fuer diese
    //  Dateien.
    // ------------------------------------------------------
    bool  linearKeys = true;

    // ------------------------------------------------------
    //  Bones mit Skalierung nahe null wieder sichtbar machen
    //
    //  Manche Props werden versteckt, indem ihr Bone auf ~0
    //  skaliert wird - nicht ueber ein Sichtbarkeitsflag. Bei
    //  336_skin1.model stehen die beiden Schwert-Bones
    //  (Bone001, Bone002) in allen 31 Clips auf 0.001. Damit
    //  faellt die Waffe zusammen und alles, was daran haengt.
    //
    //  Mit dieser Option werden solche Skalierungskeys auf 1
    //  gezwungen. Das WEICHT BEWUSST VON DER DATEI AB - deshalb
    //  Standard AUS und ein Hinweis im Protokoll, wie viele
    //  Keys geaendert wurden.
    //
    //  Dieselbe Option gibt es im Blender-Addon ab 2.8.0
    //  ("Ignore zero-scale bones").
    // ------------------------------------------------------
    bool  ignoreZeroScale = false;

    // Skalierungen bis zu diesem Wert gelten als "versteckt".
    float zeroScaleThreshold = 0.01f;

    // ------------------------------------------------------
    //  Mesh mit dem gemessenen Bindfaktor skalieren
    //
    //  Der letzte Rest des Massstabsunterschieds. In jeder der
    //  176 Bindmatrizen von 039_skin8.model steckt eine
    //  Skalierung von 0.592376 (Median, Kehrwert 1.688118); die
    //  Knotendaten haben durchgaengig 1.0.
    //
    //  Mit "Use Time 0 as Reference Pose" kommen die Knoten aus
    //  meta[5], also mit Massstab 1.0. Die Vertices leben aber im
    //  Bindraum - deshalb werden sie mit 1/1.688118 = 0.592376
    //  multipliziert, damit beide dieselbe Groesse haben.
    //
    //  Der Faktor wird NICHT fest eingetragen, sondern je Datei
    //  aus den Bindmatrizen gemessen (siehe bindScaleOnly). Er
    //  steht im Protokoll, und bei einer Datei ohne
    //  Massstabsunterschied ist er 1.0 und die Option wirkungslos.
    // ------------------------------------------------------
    bool  meshBindScale = false;

    // ------------------------------------------------------
    //  Animation auf die Bind-Lage uebertragen
    //
    //  Frame 0 steht richtig (Bind-Pose aus den Bindmatrizen),
    //  die Animation kippt um 90 Grad weg - weil sie aus einem
    //  anderen Raum kommt. Mit dieser Option wird je Bone die
    //  Abbildung Ruhelage->Bind-Lage bestimmt und auf jeden
    //  Animationsframe angewandt.
    //
    //  In 0.1.17 stand die Matrix auf der falschen SEITE
    //  (R * anim statt anim * C) - deshalb war das Ergebnis
    //  schlechter als 0.1.8. Die Messung hatte laengst gezeigt,
    //  dass die Beziehung von RECHTS konstant ist: fuer Palette 0
    //  bei allen 91 Bones dieselbe Matrix, von links dagegen je
    //  Bone eine andere.
    //
    //  Standard AUS, bis es geprueft ist - jede Aenderung
    //  einzeln.
    // ------------------------------------------------------
    //  ENTFERNT in 0.3.10: der Animationspfad rechnet die
    //  Weltmatrizen jetzt selbst aus den Kanaldaten, es gibt
    //  keinen Ort mehr, an dem eine Retargeting-Matrix greifen
    //  koennte. Das Feld bleibt nur, damit alte Skripte mit
    //  setRetargetAnimation nicht abbrechen.
    bool  retargetAnimation = false;

    // ------------------------------------------------------
    //  Ruhende Keys weglassen
    //
    //  JUMPX speichert JEDEN Bone in JEDEM Frame, ohne
    //  Keyreduktion: 131 Bones mal 668 Frames sind 87.508 Keys,
    //  auch wenn die Haelfte davon stillsteht. Das ist so im
    //  Format und kein Fehler.
    //
    //  Mit dieser Option werden Keys weggelassen, die sich vom
    //  vorigen UND vom naechsten nicht unterscheiden. Der erste
    //  und der letzte Frame eines Stillstands bleiben stehen -
    //  sonst wuerde Max ueber die Luecke hinweg interpolieren und
    //  aus einer Pause eine Bewegung machen.
    // ------------------------------------------------------
    // ------------------------------------------------------
    //  Bind-Pose aus den inversen Bindmatrizen
    //
    //  An: die Knoten stehen bei Frame 0 dort, wo die Palette
    //  sie beim Binden hatte (inv(IBM), eine Weltmatrix je Bone).
    //  Aus: die Knoten stehen auf meta[5] - das ist Frame 0 des
    //  autoPlay-Clips, nicht die Ruhestellung.
    //
    //  Standard AUS - und zwar nach der Regel, die ueberall
    //  sonst gilt.
    //
    //  Die glTF-Spezifikation schreibt die Skinning-Formel so:
    //
    //      jointMatrix[j] = inverse(globalTransform)
    //                     * globalJointTransform[j]
    //                     * inverseBindMatrix[j]
    //
    //  Die inverse Bindmatrix POSITIONIERT den Bone also nicht.
    //  Sie wird zur Laufzeit mit seiner AKTUELLEN Weltmatrix
    //  multipliziert. Der Bone selbst steht auf seiner
    //  Knotentransformation - bei uns meta[5].
    //
    //  In Max ist ISkinImportData::SetSkinTm genau die Stelle,
    //  die dem entspricht: dort wird hinterlegt, wo der Bone beim
    //  Binden stand, unabhaengig davon, wo er jetzt steht. Die
    //  Bindmatrix gehoert dorthin und sonst nirgends.
    //
    //  Die Knoten stattdessen auf inv(IBM) zu setzen war der
    //  Umweg in 0.1.8: er rueckt die Ruhelage gerade und bringt
    //  die Animation aus dem Tritt, weil die Kanaele weiterhin
    //  im Knotenraum liegen. Was wirklich fehlte, war der
    //  Massstab - siehe matchBindScale.
    //
    //  Die Option bleibt zum Vergleichen. AN heisst: Ruhelage aus
    //  den Bindmatrizen, Animation aus den Kanaelen - das passt
    //  nur zusammen, wenn beide Quellen dieselbe Pose meinen, und
    //  in 039_skin8.model tun sie das nicht.
    // ------------------------------------------------------
    //  GEKOPPELT an die Quelle der Skin-Bindlage: ist diese
    //  Option AUS, nimmt auch ISkinImportData::SetSkinTm die
    //  Lage aus der SZENE statt aus der Datei. Beides muss aus
    //  demselben Raum kommen, sonst bleibt die Differenz in der
    //  Deformation stehen.
    //
    //  AUS entspricht damit genau dem, was der .x-Zweig tut -
    //  und dort gibt es das Problem nicht.
    //
    //  STANDARD AN - "Use Time 0 as Reference Pose" ist AUS.
    //
    //  Das ist der Standardweg, den auch die grossen Importer
    //  anbieten, wenn Bind-Pose und Animation auseinanderliegen:
    //
    //    Unreal: "Scene does not contain a Bind Pose. [...] import
    //            with [Use Time 0 as Reference Pose] enabled"
    //    anima:  "First Frame: This will ignore any bind pose
    //            definition included in the FBX and will force the
    //            use of the first frame of the animation as the
    //            bind pose"
    //
    //  Bei diesem Format passt das genau: meta[5] IST Frame 0 des
    //  autoPlay-Clips. Kommen die Knoten von dort, liegen Ruhelage
    //  und Animation im selben Raum, und der Sprung beim ersten
    //  Animationsframe kann gar nicht entstehen.
    //
    //  Die Bindmatrizen werden weiterhin gebraucht - aber nur
    //  dort, wo sie hingehoeren: in ISkinImportData::SetSkinTm.
    //  Der Skin-Modifier haelt die Bindstellung selbst fest,
    //  unabhaengig davon, wo die Bones gerade stehen.
    bool  bindPoseFromInvBind = true;

    // ------------------------------------------------------
    //  Die Bindmatrix enthaelt die Mesh-Transformation
    //
    //  Unity: bindPose = bone.worldToLocalMatrix
    //                  * renderer.transform.localToWorldMatrix
    //
    //  Also boneWorld = inv(IBM) * meshWorld. Ohne den zweiten
    //  Teil fehlt genau die Drehung und der Massstab, die seit
    //  0.1.9 gesucht wurden.
    // ------------------------------------------------------
    //  Standard AUS: die Messung zeigt zwar, dass inv(IBM) *
    //  meshWorld genau die Ruhelage ergibt - aber das Addon
    //  benutzt die Bindmatrizen unveraendert und dreht
    //  stattdessen den Clip-Raum. Der Weg, der nachweislich
    //  funktioniert, gewinnt.
    bool  bindIncludesMesh = false;

    // ------------------------------------------------------
    //  Den gemessenen Massstab auf Ruhelage UND Animation
    //  anwenden.
    //
    //  Das ist die eigentliche Korrektur. Nachgemessen an
    //  039_skin8.model steckt in JEDER der 176 Bindmatrizen eine
    //  Skalierung von 0.592376 und in KEINER
    //  Knotentransformation - Kehrwert 1.688118.
    //
    //  Die glTF-Spezifikation nennt so etwas eine "Bind Shape
    //  Matrix" und haelt fest, dass sie entweder in die
    //  Meshdaten oder in die Bindmatrizen einmultipliziert wird.
    //  Sie gehoert zum Mesh, nicht zum Bone - deshalb wird sie
    //  hier auf die Knotenseite gerechnet und nicht aus den
    //  Bindmatrizen entfernt.
    // ------------------------------------------------------
    //  STANDARD AUS - und zwar aufgrund der eigenen Messung.
    //
    //  Die Korrektur ist ueber alle Bones gemessen worden und
    //  trifft nur auf 91 von 176 zu. Eine Korrektur, der die
    //  Haelfte der Bones nicht folgt, ist keine: sie rueckt die
    //  einen gerade und verdreht die anderen. Genau das war in
    //  0.1.11 zu sehen.
    //
    //  Die Messung bleibt eingeschaltet und steht im Protokoll -
    //  sie ist die Diagnose, nicht die Behandlung. Bei einer
    //  Datei, in der 176 von 176 folgen, waere die Korrektur
    //  richtig; bei 039_skin8.model ist sie es nicht.
    //
    //  Was stattdessen gilt: der Standardweg. Die Knoten stehen
    //  auf meta[5], die Bindmatrix geht ausschliesslich in
    //  ISkinImportData::SetSkinTm. Dass die Bones dann nicht
    //  ueber dem Mesh liegen, ist dabei KEIN Fehler - Max
    //  rechnet die Deformation ueber die hinterlegte Bindmatrix,
    //  nicht ueber die sichtbare Bonelage.
    // ------------------------------------------------------
    //  STANDARD AUS - das ist die Palettenmatrix aus 0.1.13, die
    //  die Animation zerlegt hat. Sie bleibt nur als Diagnose
    //  drin: das Protokoll zeigt, ob eine Palette ueberhaupt eine
    //  einheitliche Differenz hat (Palette 0: 91 von 91,
    //  Palette 2: 0 von 80).
    bool  matchBindScale = false;

    // ------------------------------------------------------
    //  Massstab MESSEN und melden - nicht anwenden.
    //
    //  0.1.8 hat den Faktor nirgends angewandt, weder auf die
    //  Ruhelage noch auf die Animation, und war damit der beste
    //  Stand. Beim Rekonstruieren hatte ich ihn faelschlich
    //  hineingeschrieben; der Vergleich mit der echten 0.1.8
    //  hat es gezeigt (dort kommt bindScale_ gar nicht vor).
    //
    //  Die Zahl bleibt im Protokoll, weil sie etwas ueber die
    //  Datei sagt (039_skin8.model: 1.688118). Sie ist eine
    //  Diagnose, keine Korrektur.
    // ------------------------------------------------------
    bool  bindScaleOnly = true;

    bool  reduceKeys = false;
    float keyEpsilon = 1e-5f;
};

// ============================================================
//  Ergebnis eines Importlaufs
// ============================================================
struct ImportStats {
    int nodes = 0;
    int meshes = 0;
    int vertices = 0;
    int faces = 0;
    int bones = 0;
    int skinned = 0;
    int materials = 0;
    int clips = 0;
    int keys = 0;
    double ms = 0.0;
};

// ============================================================
//  Der Importer
//
//  Kein Singleton, keine globalen Zwischenstaende: eine Instanz
//  je Importlauf, alles Angefasste als Member. Ein abgebrochener
//  Lauf hinterlaesst nichts, was der naechste erbt.
// ============================================================
class SceneBuilder {
public:
    SceneBuilder(Interface* ip, const ImportOptions& options)
        : ip_(ip), options_(options) {}

    // Nicht kopierbar - haelt Szenenzeiger.
    SceneBuilder(const SceneBuilder&) = delete;
    SceneBuilder& operator=(const SceneBuilder&) = delete;

    bool ImportModel(const ModelFile& file, const std::wstring& sourcePath);
    bool ImportX(const XFile& file, const std::wstring& sourcePath);

    const ImportStats& Stats() const { return stats_; }
    const std::vector<std::wstring>& Warnings() const { return warnings_; }

    // Wie viele Warnungen der Parser schon gemeldet hat - fuer die
    // Zusammenfassung am Ende des Protokolls.
    void SetParserWarnings(size_t count) { parserWarnings_ = count; }
    const std::vector<std::wstring>& Log() const { return log_; }

    // Clipgrenzen in importierten Frames, in der Reihenfolge der
    // Clips. Wird von der Oberflaeche fuer die Note Tracks
    // abgeholt.
    struct ClipRange {
        std::wstring name;
        int start = 0;
        int end = 0;
    };
    const std::vector<ClipRange>& ClipRanges() const { return clipRanges_; }
    const std::wstring& Error() const { return error_; }

private:
    // ---- gemeinsame Helfer ----
    INode* MakeNode(const std::wstring& name, bool asBone);
    void   SetNodeWorld(INode* node, const Matrix44& world);
    Mtl*   MakeMaterial(const Material& material,
                        const std::vector<Texture>& textures,
                        const std::wstring& baseDir);
    // ------------------------------------------------------
    //  KEIN WriteNoteTrack mehr in C++.
    //
    //  Note Tracks werden in MAXScript gebaut (addNoteTrack /
    //  addNewNoteKey), so wie es der XFBIN-Importer macht. Der
    //  Grund ist nicht Bequemlichkeit: der Track muss an
    //  rootNode haengen, und rootNode ist die SZENENWURZEL, eine
    //  MAXScript-Konstante. Exporter suchen ihn genau dort.
    //
    //  Der C++-Weg ueber Interface::GetRootNode() und
    //  Animatable::AddNoteTrack legt zwar einen Track an, aber
    //  ohne Namen und ohne dass die Oberflaeche ihn zeigt - und
    //  ob er derselbe Knoten ist, den MAXScript unter rootNode
    //  versteht, ist eine Annahme, die niemand geprueft hat.
    //  Der Importer, der es nachweislich richtig macht, macht es
    //  in MAXScript; also hier auch.
    //
    //  C++ liefert nur die Grenzen: siehe ClipRanges().
    // ------------------------------------------------------
    // Reihenfolge, in der Knoten gesetzt werden duerfen: Eltern
    // vor Kindern. Siehe den Kommentar an der Definition - das
    // ist keine Kosmetik, sondern die Ursache dafuer, dass ein
    // Modell zerrissen ankommt.
    static std::vector<int> ParentsFirst(const std::vector<int>& parents);

    void   Warn(const std::wstring& text);

    // Protokollzeile. Anders als Warn() ist das kein Befund,
    // sondern der Verlauf: welcher Schritt lief, wie lange, mit
    // welchem Ergebnis. Bei einem Absturz mitten im Import ist
    // die LETZTE Zeile die Information, auf die es ankommt -
    // deshalb wird protokolliert, bevor der Schritt laeuft, nicht
    // danach.
    void   Note(const std::wstring& text);

    // ---- .model ----
    //
    // void, nicht bool: diese Schritte KOENNEN nicht fehlschlagen,
    // sie warnen und machen weiter. Ein Knoten, der sich nicht
    // anlegen laesst, ist eine Warnung - kein Grund, die anderen
    // 299 wegzuwerfen. Ein bool, das immer true ist, taeuscht eine
    // Fehlerbehandlung vor, die es nicht gibt (cppcheck meldet den
    // toten Zweig, ein Leser sieht ihn nicht).
    //
    // BuildModelSkin ist die Ausnahme: fehlt der Skin-Modifier,
    // bekommt kein einziges Mesh Gewichte, und das ist eine andere
    // Aussage als "ein paar Vertices fehlen".
    void BuildModelNodes(const ModelFile& file);
    void BuildModelMeshes(const ModelFile& file);
    bool BuildModelSkin(const ModelFile& file);
    void BuildModelAnimation(const ModelFile& file);

    // ---- .x ----
    void BuildXBones(const XFile& file);
    void BuildXMaterials(const XFile& file);
    void BuildXMeshes(const XFile& file);
    bool BuildXSkin(const XFile& file);
    void BuildXAnimation(const XFile& file);

    // Setzt Laenge, Breite und Hoehe der Bone-Objekte. Laeuft
    // ERST, wenn die Hierarchie steht: die Laenge eines Bones ist
    // der Abstand zu seinem Kind, und das muss dafuer schon an
    // seinem Platz sein.
    void ShapeBones();

    // Kantenlaenge der Szene, fuer die Groesse der Helfer. Ein
    // Point-Helper mit fester Groesse 20 ist bei einem Modell von
    // 2 Einheiten ein Sternenhaufen und bei einem von 2000
    // unsichtbar.
    float SceneExtent() const;

    // Sucht die Texturdatei neben dem Modell - auch unter anderer
    // Endung. Leer, wenn nichts gefunden wurde.
    std::wstring FindTexture(const std::wstring& uri) const;

    // Schreibt die Ausdehnung von Bones, Mesh-Knoten und
    // Vertices ins Protokoll. Beantwortet in drei Zeilen, was
    // sonst aus dem Dump gerechnet werden muss.
    void ReportExtents();

    // Lineare Controller statt TCB - siehe die Definition.
    void UseLinearControllers(INode* node);

    // Massstab fuer die Mesh-Vertices, inklusive Bindfaktor.
    float MeshScale(int palette) const;

    // Misst bindFix_ aus den Bindmatrizen und den Knotendaten.
    void MeasureBindFix(const ModelFile& file);

    Interface*    ip_ = nullptr;
    ImportOptions options_;
    ImportStats   stats_;

    std::vector<INode*>      nodes_;      // parallel zur Quellliste
    // ------------------------------------------------------
    //  Erzeugte Meshes MIT ihrer Herkunft
    //
    //  Nicht nur der Knoten: auch aus welcher Instanz und
    //  welchem Submesh er stammt. Bis 0.1.6 lief stattdessen ein
    //  Zaehler mit, der beim Skin dieselbe Schleife noch einmal
    //  abfuhr - und sobald BuildModelMeshes ein Submesh
    //  UEBERSPRINGT (leer, oder Indices ausserhalb des
    //  Vertexbereichs), laufen Zaehler und Liste auseinander.
    //
    //  Bei 039_skin8.model sind das 27 Submeshes, von denen 11
    //  ankommen. Danach bekam jedes Mesh die Gewichte eines
    //  anderen.
    //
    //  Die Herkunft mitzuspeichern kostet zwei ints und macht
    //  die Zuordnung unabhaengig davon, was uebersprungen wurde.
    // ------------------------------------------------------
    struct BuiltMesh {
        INode* node = nullptr;
        int    instance = -1;   // Index in file.instances (.model)
        int    submesh = -1;    // Index innerhalb der Gruppe
        int    source = -1;     // Index in file.meshes (.x)
    };
    std::vector<BuiltMesh>   builtMeshes_;
    std::vector<Vec3>        nodeWorldPos_;  // fuer Bonelaenge und Groesse
    std::vector<Mtl*>        materials_;
    std::vector<std::wstring> warnings_;

    std::vector<std::wstring> log_;
    std::vector<ClipRange>   clipRanges_;
    std::wstring             error_;
    std::wstring             baseDir_;
    Stopwatch                clock_;

    // ------------------------------------------------------
    //  Gemessene Korrektur Knotenraum -> Bindraum
    //
    //  Sie wirkt von RECHTS: bindWorld = nodeWorld * bindFix_.
    //  Das ist der Punkt, an dem der erste Anlauf scheiterte -
    //  von links gerechnet ist die Differenz je Bone anders und
    //  sieht aus wie zwei verschiedene Posen. Von rechts ist sie
    //  ueber alle 176 Bindmatrizen dieselbe.
    // ------------------------------------------------------
    // ------------------------------------------------------
    //  Korrektur Knotenraum -> Bindraum, JE PALETTE
    //
    //  Nicht global. Das war der Irrtum in 0.1.11: gemessen ueber
    //  ALLE Bones folgten nur 91 von 176 der Korrektur - und 91
    //  ist genau die Bonezahl der ersten Palette (g0_s0). Die
    //  zweite (g2_s0, 80 Bones) hat eine andere: 039_skin8.model
    //  enthaelt ZWEI Charaktere, Bip001 und Bip002.
    //
    //  Das passt zur glTF-Spezifikation, die die "Bind Shape
    //  Matrix" beim SKIN ansiedelt, nicht beim Modell. Eine
    //  Palette ist ein Skin - also hat jede ihre eigene.
    // ------------------------------------------------------
    // Reiner Massstabsfaktor Knotenraum -> Bindraum (0.1.8).
    // Median ueber ALLE Bindmatrizen - nur noch fuer das
    // Protokoll und als Rueckfallwert.
    float                    bindScale_ = 1.0f;

    // Haeufigster Keyabstand in Millisekunden. Ein Keyabstand ist
    // genau ein Frame - siehe MsToTicks(). 0 = unbekannt, dann
    // wird ueber die Framerate gerechnet.
    double                   keyStepMs_ = 0.0;

    // ------------------------------------------------------
    //  Bindfaktor JE PALETTE
    //
    //  039_skin8.model enthaelt zwei Charaktere, und ihre
    //  Bindmatrizen tragen verschiedene Massstaebe:
    //
    //      Palette 0 (Bip001, 91 Bones):  0.592376
    //      Palette 2 (Bip002, 80 Bones):  1.000000
    //
    //  Ein globaler Median trifft dann genau einen von beiden.
    //  Der erste Charakter kam richtig, der zweite um Faktor
    //  1.688 zu klein - was im Viewport sofort auffiel und in
    //  der Messung schon dastand.
    // ------------------------------------------------------
    std::vector<float>       paletteBindScale_;

    // Je Knoten der Bindfaktor seines Teilbaums - fuer die
    // Animationskorrektur an den Wurzeln.
    std::vector<float>       nodeBindScale_;

    // Wie viele Skalierungskeys ignoreZeroScale angehoben hat.
    int                      zeroScaleFixed_ = 0;

    // Ruhelagen im meta[5]-Raum und die Abbildung auf die
    // Bind-Lage, je Bone. Siehe den Kommentar bei ihrer Berechnung
    // in BuildModelNodes.
    // Weltmatrix des Mesh-Knotens je Palette. Wird an ZWEI
    // Stellen gebraucht - bei den Knoten und beim Skin - und
    // muss dort dieselbe sein.
    // Warnungen, die schon beim Einlesen entstanden sind - fuer
    // die Zusammenfassung am Ende.
    size_t                   parserWarnings_ = 0;

    std::vector<Matrix44>    paletteMeshWorld_;
    std::vector<char>        paletteHasMesh_;

    std::vector<Matrix44>    restWorld_;
    std::vector<Matrix44>    retarget_;
    std::vector<char>        hasRetarget_;

    std::vector<Matrix44>    paletteFix_;
    std::vector<char>        paletteFixValid_;

    // Je Knoten die Palette, aus der seine Korrektur kommt (-1 =
    // keine).
    std::vector<int>         nodePalette_;

};

// ============================================================
//  FPStaticInterface - Eg3dCpp.xxx() in MAXScript
//
//  Die Oberflaeche liegt in MAXScript (scripts/Eg3dImport.mcr),
//  die Arbeit in C++. Diese Klasse ist die Naht dazwischen.
// ============================================================
class Eg3dImportInterface : public FPStaticInterface {
public:
    DECLARE_DESCRIPTOR(Eg3dImportInterface)

    BEGIN_FUNCTION_MAP
        FN_1(fn_open,        TYPE_INT,    Open,        TYPE_STRING)
        FN_0(fn_close,       TYPE_INT,    Close)
        FN_0(fn_isOpen,      TYPE_INT,    IsOpen)
        FN_0(fn_kind,        TYPE_STRING, GetKind)
        FN_0(fn_summary,     TYPE_STRING, GetSummary)
        FN_1(fn_dump,        TYPE_INT,    Dump,        TYPE_STRING)
        FN_0(fn_import,      TYPE_INT,    ImportAll)
        FN_0(fn_version,     TYPE_STRING, GetVersion)
        FN_0(fn_lastError,   TYPE_STRING, GetLastError)
        FN_0(fn_warnings,    TYPE_STRING, GetWarnings)
        FN_0(fn_stats,       TYPE_STRING, GetStats)
        FN_0(fn_clipNames,   TYPE_STRING, GetClipNames)
        FN_2(fn_setOption,   TYPE_INT,    SetOption,   TYPE_STRING, TYPE_INT)
        FN_1(fn_setScale,    TYPE_INT,    SetScale,    TYPE_FLOAT)
        FN_0(fn_pendingFile, TYPE_STRING, TakePendingFile)
        FN_0(fn_log,         TYPE_STRING, GetLog)
        FN_1(fn_saveLog,     TYPE_INT,    SaveLog,     TYPE_STRING)
        FN_1(fn_setDebug,    TYPE_INT,    SetDebug,    TYPE_INT)
        FN_1(fn_setRestFrames, TYPE_INT,  SetRestFrames, TYPE_INT)
        FN_1(fn_setReduceKeys, TYPE_INT,  SetReduceKeys, TYPE_INT)
        FN_1(fn_setBindFromInvBind, TYPE_INT, SetBindFromInvBind, TYPE_INT)
        FN_1(fn_setMatchBindScale, TYPE_INT, SetMatchBindScale, TYPE_INT)
        FN_1(fn_setUpAxisFix, TYPE_INT, SetUpAxisFix, TYPE_INT)
        FN_1(fn_setBindScaleOnly, TYPE_INT, SetBindScaleOnly, TYPE_INT)
        FN_1(fn_setRetarget, TYPE_INT, SetRetargetAnimation, TYPE_INT)
        FN_1(fn_setSnapKeys, TYPE_INT, SetSnapKeys, TYPE_INT)
        FN_1(fn_setBindMesh, TYPE_INT, SetBindIncludesMesh, TYPE_INT)
        FN_1(fn_setMeshAxis, TYPE_INT, SetMeshAxisFix, TYPE_INT)
        FN_1(fn_setFlipUv, TYPE_INT, SetFlipUvV, TYPE_INT)
        FN_1(fn_setLinearKeys, TYPE_INT, SetLinearKeys, TYPE_INT)
        FN_1(fn_setIgnoreZeroScale, TYPE_INT, SetIgnoreZeroScale, TYPE_INT)
        FN_1(fn_setMeshScale, TYPE_INT, SetMeshBindScale, TYPE_INT)
        FN_1(fn_setAnimRoot, TYPE_INT, SetAnimRootFix, TYPE_INT)
        FN_1(fn_setAnimScale, TYPE_INT, SetAnimBindScale, TYPE_INT)
        FN_0(fn_clipRanges,  TYPE_STRING, GetClipRanges)
    END_FUNCTION_MAP

    // Benannt, nicht anonym: PRUEFE_API.py gleicht dieses enum
    // gegen die FUNCTION_MAP und gegen den Deskriptor in
    // eg3dimport.cpp ab. Eine Funktion, die in zweien von drei
    // Listen steht, ist in MAXScript schlicht nicht da - und das
    // sieht man im Quelltext nicht.
    enum Eg3dImportFnID {
        fn_open, fn_close, fn_isOpen, fn_kind, fn_summary, fn_dump,
        fn_import, fn_version, fn_lastError, fn_warnings, fn_stats,
        fn_clipNames, fn_setOption, fn_setScale, fn_pendingFile,
        fn_log, fn_saveLog, fn_setDebug, fn_setRestFrames, fn_clipRanges,
        fn_setReduceKeys, fn_setBindFromInvBind, fn_setMatchBindScale,
        fn_setUpAxisFix, fn_setBindScaleOnly, fn_setRetarget, fn_setSnapKeys, fn_setBindMesh, fn_setMeshAxis, fn_setMeshScale, fn_setFlipUv, fn_setLinearKeys, fn_setIgnoreZeroScale, fn_setAnimRoot, fn_setAnimScale
    };

    int Open(const MCHAR* path);
    int Close();
    int IsOpen();
    const MCHAR* GetKind();
    const MCHAR* GetSummary();
    int Dump(const MCHAR* outPath);
    int ImportAll();
    const MCHAR* GetVersion();
    const MCHAR* GetLastError();
    const MCHAR* GetWarnings();
    const MCHAR* GetStats();
    const MCHAR* GetClipNames();
    int SetOption(const MCHAR* name, int value);
    int SetScale(float value);

    // Zahl der Ruheframes vor der Animation (siehe
    // ImportOptions::restFrames).
    int SetRestFrames(int frames);

    // Ruhende Keys weglassen (siehe ImportOptions::reduceKeys).
    int SetReduceKeys(int on);

    // Bind-Pose aus inv(IBM) statt aus meta[5].
    int SetBindFromInvBind(int on);

    // Massstab angleichen (siehe ImportOptions::matchBindScale).
    int SetMatchBindScale(int on);

    // Y-up nach Z-up an der Wurzel (siehe ImportOptions::upAxisFix).
    int SetUpAxisFix(int on);

    // Reine Massstabsangleichung (siehe ImportOptions::bindScaleOnly).
    int SetBindScaleOnly(int on);

    // Animation auf die Bind-Lage uebertragen.
    int SetRetargetAnimation(int on);

    // Mesh-Transformation in der Bindmatrix beruecksichtigen.
    int SetBindIncludesMesh(int on);

    // Mesh-Vertices +90 Grad um X drehen.
    int SetMeshAxisFix(int on);

    // V-Achse der UVs umdrehen.
    int SetFlipUvV(int on);

    // Lineare Controller statt TCB.
    int SetLinearKeys(int on);

    // Bones mit Skalierung nahe null auf 1 zwingen.
    int SetIgnoreZeroScale(int on);

    // Mesh mit dem gemessenen Bindfaktor skalieren.
    int SetMeshBindScale(int on);

    // Animationskorrektur an den Wurzeln.
    int SetAnimRootFix(int on);

    // Animation je Bone auf den Bindmassstab bringen.
    int SetAnimBindScale(int on);

    // Keyzeiten auf ganze Frames runden.
    int SetSnapKeys(int on);

    // Clipgrenzen in importierten Frames, eine Zeile je Clip:
    //     name<TAB>startFrame<TAB>endFrame
    //
    // Fuer die Note Tracks, die in MAXScript gebaut werden -
    // siehe den Kommentar bei WriteNoteTrack().
    const MCHAR* GetClipRanges();

    // Holt den ueber File -> Import hinterlegten Pfad ab UND
    // loescht ihn. Das Loeschen gehoert dazu: sonst greift die
    // Oberflaeche beim naechsten Oeffnen wieder zur Datei von
    // vorhin, und niemand versteht, warum.
    const MCHAR* TakePendingFile();

    // Das Protokoll des letzten Laufs - Verlauf, nicht Befunde.
    const MCHAR* GetLog();

    // Protokoll in eine Datei schreiben. Getrennt von GetLog(),
    // weil ein Protokoll mit 300 Knoten im Listener unbrauchbar
    // ist und in einer Datei genau richtig.
    int SaveLog(const MCHAR* outPath);

    // Ausfuehrliches Protokoll: je Knoten, je Mesh, je Clip eine
    // Zeile statt nur je Schritt.
    int SetDebug(int on);

    // Von der SceneImport-Klasse benutzt: Pfad hinterlegen und die
    // Oberflaeche oeffnen.
    void SetPendingFile(const std::wstring& path) { pendingFile_ = path; }
    void RunMacro();
    bool RunScriptText(const std::wstring& script);

private:
    void SetError(const std::wstring& text);

    // Warnungen des Parsers uebernehmen (UTF-8 nach UTF-16).
    // Steht als Funktion da, weil beide Formatzweige sie brauchen
    // und zwei gleiche Schleifen genau einmal auseinanderlaufen.
    void TakeWarnings(const std::vector<std::string>& source);

    // Genau eine der beiden ist belegt - welche, sagt kind_.
    std::unique_ptr<ModelFile> model_;
    std::unique_ptr<XFile>     xfile_;
    std::wstring   kind_;
    std::wstring   sourcePath_;
    std::wstring   pendingFile_;

    // Das Ergebnis des letzten Lesevorgangs. Wird fuer den Dump
    // gebraucht: DumpModel/DumpX schreiben die Warnungen des
    // Parsers mit, und die stehen nur hier.
    ReadResult     lastResult_;

    ImportOptions  options_;
    ImportStats    stats_;
    std::wstring   error_;
    std::vector<std::wstring> warnings_;
    std::vector<std::wstring> log_;
    std::vector<SceneBuilder::ClipRange> clipRanges_;

    // Rueckgabepuffer fuer die TYPE_STRING-Methoden. MAXScript
    // kopiert den Inhalt sofort, aber der Zeiger muss den Aufruf
    // ueberleben - deshalb Member und nicht lokal.
    std::wstring   returnBuffer_;
};

Eg3dImportInterface* GetEg3dInterface();

}  // namespace eg3d

#endif  // EG3D_IMPORT_H
