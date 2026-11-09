// ============================================================
//  eg3ddump - liest .model und .x ohne 3ds Max
//
//  Zweck ist die Gegenprobe: eg3ddump schreibt einen Dump,
//  tools/pydump.py schreibt aus denselben Dateien Zeile fuer
//  Zeile dasselbe, und fc /L beziehungsweise diff vergleicht.
//  Was hier durchfaellt, faellt vor dem ersten SDK-Build durch.
//
//  Aufrufe:
//    eg3ddump <datei>                     Zusammenfassung
//    eg3ddump <datei> --all               alles
//    eg3ddump <ordner> --all -o dump.txt  alle Dateien im Ordner
//
//  Abschnitte einzeln:  --verts --tris --anim --bind --layout
//  Sonst:               --threads N   (1 schaltet Parallelitaet ab)
//                       --time        Zeiten je Datei
// ============================================================

#include "../src/eg3d_dump.h"
#include "../src/eg3d_model.h"
#include "../src/eg3d_parallel.h"
#include "../src/eg3d_x.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <clocale>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

using namespace eg3d;

namespace {

bool IsDirectory(const std::string& path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return S_ISDIR(info.st_mode);
#endif
}

// Dateien eines Ordners, alphabetisch. Die Sortierung ist nicht
// Kosmetik: der Vergleich gegen Python haengt daran, dass beide
// Seiten dieselbe Reihenfolge waehlen.
std::vector<std::string> ListDirectory(const std::string& path) {
    std::vector<std::string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA((path + "\\*").c_str(), &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.push_back(found.cFileName);
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR* dir = opendir(path.c_str());
    if (dir) {
        while (const struct dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (!IsDirectory(path + "/" + name)) names.push_back(name);
        }
        closedir(dir);
    }
#endif
    std::sort(names.begin(), names.end());
    return names;
}

bool HasExtension(const std::string& name, const char* ext) {
    const size_t len = std::strlen(ext);
    if (name.size() < len) return false;
    std::string tail = name.substr(name.size() - len);
    for (size_t i = 0; i < tail.size(); ++i)
        tail[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(tail[i])));
    return tail == ext;
}

void PrintUsage() {
    std::cout <<
        "eg3ddump - 300 Heroes Modelle lesen (.model / .x)\n"
        "\n"
        "  eg3ddump <datei|ordner> [optionen]\n"
        "\n"
        "  --all        alle Abschnitte (gross! .x mit --anim wird "
        "schnell dreistellig MB)\n"
        "  --verts      Vertices einzeln\n"
        "  --tris       Dreiecke einzeln\n"
        "  --bind       Bind-Matrizen einzeln\n"
        "  --anim       Kurven und Keys\n"
        "  --layout     Puffer-Layouttabelle (findet eine falsche Basis)\n"
        "  --summary    nur Kopfzahlen\n"
        "  --threads N  Threadzahl (1 = ohne Parallelitaet)\n"
        "  --time       Zeiten je Datei auf stderr\n"
        "  -o <datei>   Ausgabe in eine Datei statt nach stdout\n";
}

} // namespace

// ------------------------------------------------------------
//  --locale <name>
//
//  Ein C-Programm startet IMMER im C-Locale, egal was in der
//  Umgebung steht - LC_ALL=de_DE zu setzen aendert nichts, solange
//  niemand setlocale() ruft. 3ds Max ruft es (die CRT-Initialisie-
//  rung tut es), dieses Werkzeug nicht.
//
//  Genau deshalb ist der Locale-Fehler im Zahlenparser hier nie
//  aufgefallen und im Plugin sofort: das Werkzeug lief im
//  C-Locale, wo strtod den Dezimalpunkt kennt.
//
//  Mit --locale de_DE.UTF-8 laesst sich der Fall nachstellen. Die
//  Ausgabe MUSS dieselbe sein wie ohne - alles andere ist ein
//  Locale-Leck. PRUEFE_LOCALE.py fahrt genau diesen Vergleich.
// ------------------------------------------------------------
int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    DumpSections sections = DumpSections::Summary();
    std::string outputPath;
    bool showTime = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all")            sections = DumpSections::All();
        else if (arg == "--summary")   sections = DumpSections::Summary();
        else if (arg == "--verts")     sections.vertices = true;
        else if (arg == "--tris")      sections.triangles = true;
        else if (arg == "--bind")      sections.bindpose = true;
        else if (arg == "--anim")      sections.animation = true;
        else if (arg == "--layout")    sections.layout = true;
        else if (arg == "--time")      showTime = true;
        else if (arg == "--threads" && i + 1 < argc) SetMaxThreads(std::atoi(argv[++i]));
        else if (arg == "--locale" && i + 1 < argc) {
            const char* wanted = argv[++i];
            if (std::setlocale(LC_ALL, wanted) == nullptr) {
                std::cerr << "Locale nicht verfuegbar: " << wanted << "\n";
                return 2;
            }
        }
        else if (arg == "-o" && i + 1 < argc)        outputPath = argv[++i];
        else if (arg == "-h" || arg == "--help")     { PrintUsage(); return 0; }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unbekannte Option: " << arg << "\n";
            return 2;
        } else inputs.push_back(arg);
    }

    if (inputs.empty()) { PrintUsage(); return 2; }

    std::vector<std::string> files;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (IsDirectory(inputs[i])) {
            const std::vector<std::string> names = ListDirectory(inputs[i]);
            for (size_t n = 0; n < names.size(); ++n) {
                if (HasExtension(names[n], ".model") || HasExtension(names[n], ".x")) {
                    const char sep =
#ifdef _WIN32
                        '\\';
#else
                        '/';
#endif
                    files.push_back(inputs[i] + sep + names[n]);
                }
            }
        } else {
            files.push_back(inputs[i]);
        }
    }

    if (files.empty()) {
        std::cerr << "keine .model- oder .x-Dateien gefunden\n";
        return 2;
    }

    std::ofstream fileOut;
    if (!outputPath.empty()) {
        fileOut.open(outputPath.c_str(), std::ios::binary);
        if (!fileOut) { std::cerr << "kann " << outputPath << " nicht schreiben\n"; return 2; }
    }
    // Explizit binaer und ohne Locale-Facette. Unter Windows
    // wuerde ein Textstream sonst \n zu \r\n machen, und der
    // Vergleich gegen die Python-Ausgabe scheitert an
    // Zeilenenden statt an Inhalten.
    std::ostream& out = outputPath.empty() ? std::cout : fileOut;

    int failed = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string& path = files[i];
        const auto begin = std::chrono::steady_clock::now();

        std::vector<uint8_t> buffer;
        std::string error;
        if (!ReadWholeFile(path, buffer, error)) {
            std::cerr << "FEHLER " << error << "\n";
            ++failed;
            continue;
        }

        if (LooksLikeEg3d(buffer.data(), buffer.size())) {
            ModelFile model;
            const ReadResult result =
                ReadModelBuffer(buffer.data(), buffer.size(), path, model);
            if (!result.ok) {
                std::cerr << "FEHLER " << result.error << "\n";
                ++failed;
                continue;
            }
            DumpModel(model, result, sections, out);
        } else if (LooksLikeJumpx(buffer.data(), buffer.size())) {
            XFile x;
            const ReadResult result = ReadXBuffer(buffer.data(), buffer.size(), path, x);
            if (!result.ok) {
                std::cerr << "FEHLER " << result.error << "\n";
                ++failed;
                continue;
            }
            DumpX(x, result, sections, out);
        } else {
            std::cerr << "UEBERSPRUNGEN " << path
                      << ": weder EG3D noch JUMPX\n";
            continue;
        }

        if (showTime) {
            const auto end = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(end - begin).count();
            std::cerr << "zeit " << path << " " << FormatFloat(ms, 3) << " ms\n";
        }
    }

    out.flush();
    if (failed > 0) {
        std::cerr << failed << " Datei(en) konnten nicht gelesen werden\n";
        return 1;
    }
    return 0;
}
