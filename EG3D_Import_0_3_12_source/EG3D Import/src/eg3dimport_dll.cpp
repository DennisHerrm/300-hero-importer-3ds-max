// ============================================================
//  EG3D Import - DLL-Einstieg
//
//  Zu den Exports: die Lib*-Funktionen sind NICHT extern "C",
//  also exportiert __declspec(dllexport) den dekorierten Namen.
//  3ds Max sucht aber den undekorierten. eg3dimport.def sorgt
//  dafuer, dass beide sauber herauskommen - sie darf NICHT
//  entfallen, auch wenn das doppelt gemoppelt aussieht.
// ============================================================

#include "eg3dimport.h"

// ClassDesc2 steht in iparamb2.h. Die neueren SDKs ziehen den
// Header ueber max.h mit herein, 2016 nicht - dort fehlt sonst
// die Basisklasse, und danach meldet jede override-Methode einen
// eigenen Fehler.
#include <iparamb2.h>

// SceneImport steht in impexp.h, RegisterNotification und die
// NOTIFY_*-Konstanten in notify.h. Auch die zieht 2016 nicht ueber
// max.h mit.
#include <impexp.h>
#include <notify.h>

#include <string>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE instance, ULONG reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        hInstance = instance;
        // Der Loaderlock ist der einzige Grund, hier nichts
        // weiter zu tun: alles, was Max anfassen wuerde, gehoert
        // nach LibInitialize oder spaeter.
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

// Der Interface-Deskriptor steht in eg3dimport.cpp, zusammen mit
// den Methoden, die er anmeldet. Getrennt zu halten hiesse, zwei
// Dateien synchron zu pflegen - und genau das geht schief.

// ============================================================
//  Utility-Plugin
//
//  Traegt das Plugin in den Utilities-Reiter ein. Die eigentliche
//  Oberflaeche liegt in MAXScript; diese Klasse existiert, damit
//  Max eine Klasse zum Laden hat und der Plugin Manager etwas
//  anzeigt.
// ============================================================

class Eg3dImportUtil : public UtilityObj {
public:
    void BeginEditParams(Interface* ip, IUtil*) override {
        if (ip != nullptr) ip->PushPrompt(_T("EG3D Import"));
        eg3d::Eg3dImportInterface* iface = eg3d::GetEg3dInterface();
        if (iface != nullptr) iface->RunMacro();
    }
    void EndEditParams(Interface* ip, IUtil*) override {
        if (ip != nullptr) ip->PopPrompt();
    }
    // Absichtlich leer und absichtlich ueberschrieben: theUtil ist
    // ein statisches Objekt. Wuerde Max es loeschen, zerlegte das
    // den Heap. Dass die Basisklasse dasselbe tut, ist kein Grund,
    // es wegzulassen - die Absicht steht sonst nirgends.
    // cppcheck-suppress uselessOverride
    void DeleteThis() override {}
};

namespace {
Eg3dImportUtil theUtil;
}

class Eg3dImportClassDesc : public ClassDesc2 {
public:
    int   IsPublic() override           { return TRUE; }

    // Hier ist ein statisches Objekt richtig: von einem
    // UtilityObj gibt es genau eines, und DeleteThis() ist leer.
    // Bei SceneImport unten liegt der Fall anders - siehe dort.
    void* Create(BOOL) override         { return &theUtil; }

    const MCHAR* ClassName() override   { return _T("Eg3dImport"); }

    // NonLocalizedClassName gibt es erst ab Max 2022
    // (MAX_RELEASE 24000). Davor kennt ClassDesc die Methode
    // nicht, und override waere ein Uebersetzungsfehler.
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("Eg3dImport"); }
#endif

    SClass_ID    SuperClassID() override { return UTILITY_CLASS_ID; }
    Class_ID     ClassID() override      { return EG3DIMPORT_CLASS_ID; }
    const MCHAR* Category() override     { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("Eg3dImport"); }
    HINSTANCE    HInstance() override    { return hInstance; }
};

namespace {
Eg3dImportClassDesc theClassDesc;
}

// ============================================================
//  Eintrag in Max' eigenem Import-Dialog
//
//  Ohne SceneImport-Klasse taucht das Format unter File -> Import
//  nicht auf, und wer es dort sucht, findet es nicht.
//
//  Der Dialog laesst genau EINE Datei waehlen. Fuer .model und .x
//  reicht das: anders als bei XFBIN steckt ein vollstaendiges
//  Modell in einer Datei.
// ============================================================

// ------------------------------------------------------------
//  ACHTUNG: .model und .x sind BEIDE schon vergeben.
//
//  .model gehoert seit Max 2016 dem mitgelieferten ATF CATIA V4
//  Importer (Dassault CATIA V4 - dieselbe Endung, voellig anderes
//  Format). .x ist DirectX Retained Mode.
//
//  Waehlt der Benutzer im Import-Dialog "All formats", entscheidet
//  Max anhand der Endung - und der eingebaute Importer gewinnt.
//  Die Meldung lautet dann:
//
//      ATF CATIA V4 importer:
//      039_skin8.model is either corrupt or not a supported version.
//
//  Das sieht aus, als sei die DATEI kaputt. Sie ist es nicht; es
//  hat schlicht der falsche Importer zugegriffen.
//
//  Was hilft:
//
//    1. Im Import-Dialog unter "Dateityp" AUSDRUECKLICH
//       "300 Heroes (EG3D)" waehlen statt "All formats". Dann
//       ruft Max diese Klasse auf.
//    2. Der Weg ueber das Utilities-Panel oder das Makro geht
//       immer - er haengt nicht an der Endung.
//
//  Deshalb faengt ShortDesc mit "300 Heroes" an: in einer nach
//  Namen sortierten Liste steht der Eintrag damit weit oben und
//  ist auffindbar, ohne die ganze Liste durchzugehen.
// ------------------------------------------------------------

class Eg3dSceneImport : public SceneImport {
public:
    int ExtCount() override { return 2; }

    const MCHAR* Ext(int index) override {
        switch (index) {
        case 0:  return _T("model");
        case 1:  return _T("x");
        default: return _T("");
        }
    }

    // Landet in der Formatliste des Import-Dialogs; die Endungen
    // haengt Max selbst an:
    //
    //     300 Heroes (EG3D) (*.model,*.x)
    const MCHAR* ShortDesc() override {
        return _T("300 Heroes (EG3D)");
    }

    const MCHAR* LongDesc() override {
        return _T("300 Heroes (300英雄) - EG3D .model und JUMPX .x ")
               _T("(Geometrie, Skelett, Skin, Materialien, Animationen)");
    }

    const MCHAR* AuthorName() override       { return _T("DennisH"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override    { return _T(""); }
    const MCHAR* OtherMessage2() override    { return _T(""); }

    unsigned int Version() override { return EG3DIMPORT_VERSION_NUM; }

    void ShowAbout(HWND) override {}

    int DoImport(const MCHAR* name, ImpInterface*, Interface*,
                 BOOL suppressPrompts) override {
        if (name == nullptr) return 0;

        eg3d::Eg3dImportInterface* iface = eg3d::GetEg3dInterface();
        if (iface == nullptr) return 0;

        iface->SetPendingFile(name);

        // suppressPrompts ist gesetzt, wenn Max ohne Rueckfrage
        // importieren soll - etwa aus einem Skript. Dann waere ein
        // Fenster falsch: die Datei wird direkt gelesen und
        // aufgebaut.
        if (suppressPrompts) {
            if (iface->Open(name) == 0) return 0;
            return iface->ImportAll();
        }

        iface->RunMacro();
        return 1;
    }
};

class Eg3dSceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }

    // ------------------------------------------------------
    //  new, NICHT ein statisches Objekt.
    //
    //  Das Singleton-Muster aus der Autodesk-Doku gilt fuer
    //  UTILITY-Plugins - davon gibt es genau eines. SceneImport
    //  ist ein anderer Fall: Max legt eine Instanz an, fragt
    //  ExtCount, Ext und die Beschreibungen ab und gibt den
    //  Speicher danach wieder frei. Die Klasse hat kein
    //  DeleteThis, Max loescht also direkt.
    //
    //  Ein statisches Objekt zu loeschen zerlegt den Heap. Genau
    //  daran ist 3ds Max 2027 bei XFBIN Import abgestuerzt -
    //  beim Oeffnen des Import-Dialogs, bevor ueberhaupt etwas
    //  importiert wurde.
    // ------------------------------------------------------
    void* Create(BOOL) override { return new Eg3dSceneImport(); }

    const MCHAR* ClassName() override { return _T("EG3D Import"); }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("EG3D Import"); }
#endif
    SClass_ID    SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID     ClassID() override      { return EG3DIMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override     { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("Eg3dSceneImport"); }
    HINSTANCE    HInstance() override    { return hInstance; }
};

namespace {
Eg3dSceneImportClassDesc theSceneImportClassDesc;
}

// ============================================================
//  Eingebettete Oberflaeche
//
//  Die .mcr liegt als RCDATA in der DLL. Beim Systemstart wird
//  sie in den Temp-Ordner geschrieben und eingelesen - damit ist
//  das Makro registriert, ohne dass eine Datei installiert sein
//  muesste.
//
//  Eine INSTALLIERTE Datei hat Vorrang. Wer die .mcr von Hand
//  aendert, um etwas auszuprobieren, soll seine Fassung sehen und
//  nicht die eingebackene - sonst sucht man den Fehler an der
//  falschen Stelle.
// ============================================================

namespace {

const int kMcrResourceId = 1000;

std::wstring WriteEmbeddedScript() {
    HRSRC resource = FindResource(hInstance, MAKEINTRESOURCE(kMcrResourceId),
                                  RT_RCDATA);
    if (resource == nullptr) return std::wstring();

    HGLOBAL handle = LoadResource(hInstance, resource);
    if (handle == nullptr) return std::wstring();

    const void* data = LockResource(handle);
    const DWORD size = SizeofResource(hInstance, resource);
    if (data == nullptr || size == 0) return std::wstring();

    wchar_t tempDir[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return std::wstring();

    const std::wstring path = std::wstring(tempDir) + L"Eg3dImport_embedded.mcr";

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::wstring();

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);

    if (ok == FALSE || written != size) return std::wstring();
    return path;
}

bool InstalledScriptExists() {
    // Pfad: %APPDATA% + Autodesk\ApplicationPlugins\Eg3dImport
    //       + Contents\MacroScripts\Eg3dImport.mcr
    wchar_t appdata[MAX_PATH] = {0};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;

    const std::wstring path = std::wstring(appdata) +
        L"\\Autodesk\\ApplicationPlugins\\Eg3dImport\\Contents"
        L"\\MacroScripts\\Eg3dImport.mcr";

    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void OnStartup(void*, NotifyInfo*) {
    // Die installierte Fassung laedt Max ohnehin selbst.
    if (InstalledScriptExists()) return;

    const std::wstring path = WriteEmbeddedScript();
    if (path.empty()) return;

    const std::wstring command =
        L"try (fileIn @\"" + path + L"\") catch "
        L"(format \"[EG3D] Eingebettete Oberflaeche liess sich nicht laden: %\\n\" "
        L"(getCurrentException()))";

    eg3d::Eg3dImportInterface* iface = eg3d::GetEg3dInterface();
    if (iface != nullptr) iface->RunScriptText(command);
}

}  // namespace

// ============================================================
//  Die von Max verlangten DLL-Funktionen
// ============================================================

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("EG3D Import ") EG3DIMPORT_VERSION_STR
           _T(" - 300 Heroes .model / .x Importer");
}

__declspec(dllexport) int LibNumberClasses() { return 2; }

__declspec(dllexport) ClassDesc* LibClassDesc(int index) {
    switch (index) {
    case 0:  return &theClassDesc;
    case 1:  return &theSceneImportClassDesc;
    default: return nullptr;
    }
}

__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

__declspec(dllexport) int CanAutoDefer() { return FALSE; }

// LibInitialize laeuft, BEVOR MAXScript bereit ist. Deshalb wird
// das Einlesen der Oberflaeche auf NOTIFY_SYSTEM_STARTUP
// verschoben statt hier erledigt.
__declspec(dllexport) int LibInitialize() {
    RegisterNotification(OnStartup, nullptr, NOTIFY_SYSTEM_STARTUP);
    return TRUE;
}

__declspec(dllexport) int LibShutdown() {
    UnRegisterNotification(OnStartup, nullptr, NOTIFY_SYSTEM_STARTUP);
    return TRUE;
}
