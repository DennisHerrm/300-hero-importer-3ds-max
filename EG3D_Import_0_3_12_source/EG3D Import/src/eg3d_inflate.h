// ============================================================
//  EG3D Import - zlib / DEFLATE
//
//  Das .x-Format (JUMPX) legt seine beiden Bloecke als zwei
//  hintereinanderliegende zlib-Streams ab. Ohne Entpacker sieht
//  hinter dem Header alles nach Zufallsbytes aus - genau deshalb
//  galt das Format seit 2015 als ungeloest.
//
//  WARUM NICHT DIE ECHTE ZLIB
//
//  Weil sie als Abhaengigkeit teurer waere als der Code hier.
//  Das Paket muesste sie mitliefern oder finden, und zwar fuer
//  zwoelf Max-Jahrgaenge und zwei Toolsets. Ein Entpacker ist
//  dagegen ein abgeschlossenes, im RFC 1951 vollstaendig
//  beschriebenes Stueck Arbeit, das sich gegen die Python-zlib
//  ueber alle Testdateien byteweise gegenpruefen laesst - und
//  genau das macht VERGLEICHE.bat.
//
//  ZWEI STREAMS HINTEREINANDER
//
//  Die Python-Referenz benutzt decompressobj().unused_data, um zu
//  finden, wo der erste Stream endet. Deshalb meldet Inflate hier
//  die Zahl der VERBRAUCHTEN Eingabebytes zurueck; der zweite
//  Aufruf setzt dort auf.
// ============================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace eg3d {

struct InflateResult {
    bool        ok = false;
    size_t      consumed = 0;      // verbrauchte Eingabebytes einschliesslich Adler-32
    std::string error;
};

// Ein zlib-Stream (RFC 1950: 2 Byte Kopf, DEFLATE, 4 Byte Adler-32).
// expectedSize ist ein Hinweis fuer die Vorbelegung; 0 heisst
// "unbekannt". Er wird NICHT als Abbruchbedingung benutzt - ein
// falscher Kopfwert soll nicht stillschweigend abschneiden.
InflateResult ZlibInflate(const uint8_t* data, size_t size,
                          size_t expectedSize, std::vector<uint8_t>& out);

// Roher DEFLATE-Strom ohne zlib-Kopf. Wird vom Selbsttest benutzt.
InflateResult RawInflate(const uint8_t* data, size_t size,
                         size_t expectedSize, std::vector<uint8_t>& out);

// Adler-32 nach RFC 1950. Getrennt exportiert, damit der
// Selbsttest die Pruefsumme auch ohne Entpacken rechnen kann.
uint32_t Adler32(const uint8_t* data, size_t size);

} // namespace eg3d
