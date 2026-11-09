// ============================================================
//  EG3D Import - Dump
//
//  WOZU
//
//  Der Dump ist kein Protokoll, sondern das Messgeraet. Er
//  schreibt alles Dekodierte zeilenweise heraus, und
//  tools/pydump.py schreibt aus denselben Dateien Zeile fuer
//  Zeile dasselbe. Wenn beide Seiten zeichengleich sind, ist der
//  C++-Parser nachweislich richtig - nicht "sieht gut aus im
//  Viewport".
//
//  Genau dieses Verfahren hat bei XFBIN Import und BAF Toolkit
//  jeden Formatfehler VOR dem ersten Max-Start gefunden. Es
//  findet ihn auch dann, wenn er sich in der Bind-Pose wegkuerzt.
//
//  Derselbe Code laeuft spaeter AUS MAX HERAUS (Plugin-Funktion
//  dump()). Damit laesst sich zusaetzlich pruefen, dass beim Weg
//  durch die Max-Schicht nichts verlorengeht.
//
//  REGEL FUER JEDE ZEILE
//
//  Ganzzahlen mit std::to_string, Gleitkommazahlen ausschliesslich
//  mit FormatFloat. Kein printf("%f"), kein ostream <<: beides
//  haengt an der Locale, und ein Max-Prozess auf Deutsch schreibt
//  dann Dezimalkommata.
// ============================================================
#pragma once

#include "eg3d_model.h"
#include "eg3d_x.h"

#include <iosfwd>
#include <string>

namespace eg3d {

// Welche Abschnitte geschrieben werden. Die vertex- und
// keyweisen Abschnitte sind die grossen; sie einzeln zuschaltbar
// zu haben spart beim Eingrenzen viel Zeit.
struct DumpSections {
    bool container = true;
    bool meta      = true;   // Materialien, Texturen, Tags
    bool geometry  = true;   // Submesh- und Attributkoepfe, Mesh-Koepfe
    bool vertices  = false;
    bool triangles = false;
    bool skeleton  = true;   // Paletten, Knoten, Bone-Koepfe
    bool bindpose  = false;  // Bind-Matrizen einzeln
    bool animation = false;  // Kurven und Keys
    bool clips     = true;   // Clipkoepfe
    bool layout    = false;
    bool warnings  = true;

    static DumpSections All();
    static DumpSections Summary();
};

void DumpModel(const ModelFile& file, const ReadResult& result,
               const DumpSections& sections, std::ostream& out);
void DumpX(const XFile& file, const ReadResult& result,
           const DumpSections& sections, std::ostream& out);

} // namespace eg3d
