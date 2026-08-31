#include "eg3d_parallel.h"

namespace eg3d {
namespace {
// Prozessweit, absichtlich ohne Sperre: gesetzt wird sie einmal
// beim Start oder aus der Oberflaeche, gelesen von den
// Arbeitsthreads. Ein int-Schreibzugriff ist auf x64 atomar,
// und ein verpasster Wert kostet hoechstens einen Durchlauf mit
// der alten Threadzahl.
int g_maxThreads = 0;   // 0 = automatisch
}

int  MaxThreads()            { return g_maxThreads; }
void SetMaxThreads(int count) { g_maxThreads = (count < 0) ? 0 : count; }

} // namespace eg3d
