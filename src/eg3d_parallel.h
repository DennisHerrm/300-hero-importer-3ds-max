// ============================================================
//  EG3D Import - ParallelFor
//
//  WO PARALLELISIERT WIRD UND WO NICHT
//
//  Autodesk sagt es in der Entwicklerdoku unter "Thread Safety"
//  ohne Umschweife: 3ds Max ist nicht threadsicher, und das
//  Referenzsystem sowie die Knotenauswertung sind ausdruecklich
//  einfaedig. Wer aus einem Nebenthread INode anlegt, Controller
//  setzt oder Meshes fuellt, baut sich Abstuerze, die auf einem
//  Rechner auftreten und auf dem naechsten nicht.
//
//  Deshalb gilt hier eine harte Trennung:
//
//    Parser-Schicht (diese Datei, eg3d_model, eg3d_x, eg3d_json,
//    eg3d_inflate)  -> darf Threads benutzen. Sie arbeitet auf
//    einem Bytepuffer und eigenen Strukturen, kennt kein SDK.
//
//    Max-Schicht (eg3dimport.cpp)  -> laeuft ausschliesslich im
//    Hauptthread. Kein ParallelFor, kein std::async, nichts.
//
//  Das ist keine Vorsichtsmassnahme, sondern die Aufteilung, die
//  ueberhaupt etwas bringt: das Teure am Import sind das
//  Entpacken, das Dekodieren der Vertexattribute und das
//  Abtasten der Animationskurven - alles reine Rechenarbeit auf
//  Rohdaten.
//
//  DETERMINISMUS
//
//  Jeder Durchlauf schreibt ausschliesslich in seinen eigenen
//  Ausgabeplatz, es gibt keine gemeinsam beschriebene Variable
//  und keine Reduktion. Damit ist das Ergebnis Bit fuer Bit
//  dasselbe wie einfaedig - Voraussetzung dafuer, dass der
//  Vergleich gegen die Python-Referenz ueberhaupt aussagekraeftig
//  ist. Wer hier eine Summe ueber Threads bildet, verliert das.
// ============================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace eg3d {

// Obergrenze fuer die Threadzahl. 0 = automatisch.
// Wird von der Oberflaeche gesetzt; 1 schaltet die Parallelitaet
// ab, was beim Eingrenzen eines Fehlers die erste Massnahme ist.
int  MaxThreads();
void SetMaxThreads(int count);

// Fuehrt body(i) fuer i in [0, count) aus, verteilt auf Threads.
//
// Ausnahmen: eine im Arbeitsthread geworfene Ausnahme wird
// aufgefangen, gemerkt und nach dem Zusammenfuehren im
// AUFRUFENDEN Thread erneut geworfen. Sie ueber eine
// Threadgrenze entkommen zu lassen ruft std::terminate - der
// Max-Prozess waere dann wortlos weg.
template <typename Body>
void ParallelFor(size_t count, const Body& body) {
    if (count == 0) return;

    unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) hardware = 1;
    int limit = MaxThreads();
    if (limit <= 0) limit = static_cast<int>(hardware);

    size_t workers = static_cast<size_t>(limit);
    if (workers > count) workers = count;

    // Unter diesem Wert kostet das Starten der Threads mehr als
    // die Arbeit einbringt. Ein Modell mit drei Submeshes soll
    // nicht drei Threads aufmachen.
    const size_t kMinPerWorker = 2;
    if (workers > 1 && count / workers < kMinPerWorker)
        workers = std::max<size_t>(1, count / kMinPerWorker);

    if (workers <= 1) {
        for (size_t i = 0; i < count; ++i) body(i);
        return;
    }

    std::mutex         errorMutex;
    std::exception_ptr firstError;

    auto run = [&](size_t begin, size_t end) {
        try {
            for (size_t i = begin; i < end; ++i) body(i);
        } catch (...) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (!firstError) firstError = std::current_exception();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    const size_t chunk = (count + workers - 1) / workers;
    for (size_t w = 1; w < workers; ++w) {
        const size_t begin = w * chunk;
        if (begin >= count) break;
        const size_t end = std::min(count, begin + chunk);
        threads.emplace_back(run, begin, end);
    }
    run(0, std::min(count, chunk));   // der aufrufende Thread arbeitet mit

    for (std::thread& t : threads) t.join();
    if (firstError) std::rethrow_exception(firstError);
}

} // namespace eg3d
