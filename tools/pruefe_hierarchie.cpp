// ============================================================
//  pruefe_hierarchie - ParentsFirst gegen echte Daten
//
//  Aufruf (siehe tools/PRUEFE_HIERARCHIE.py):
//      pruefe_hierarchie <dumpdatei> <anzeigename>
//
//  Liest die parent=-Angaben aus einem eg3ddump-Ausgabe und
//  prueft drei Dinge:
//
//    1. Enthaelt die Reihenfolge jeden Knoten genau einmal?
//    2. Steht jeder Elternteil VOR seinem Kind?
//    3. Wie viele Verstoesse gaebe es OHNE die Sortierung?
//
//  Punkt 3 ist der wichtige: er zeigt, dass die Pruefung etwas
//  misst. Ueber den Bestand sind es 162 Verstoesse vorher und 0
//  nachher - ohne die Zahl "vorher" waere "0 nachher" wertlos.
//
//  Die Funktion ist hier ABSICHTLICH kopiert und nicht aus
//  eg3dimport.cpp eingebunden: die Datei bindet Max-Header ein
//  und liesse sich ohne SDK nicht uebersetzen. Aendert sich das
//  Original, muss diese Kopie mit - der Kommentar an beiden
//  Stellen sagt das.
// ============================================================
#include <algorithm>
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

std::vector<int> ParentsFirst(const std::vector<int>& parents) {
    const size_t count = parents.size();
    std::vector<int> order; order.reserve(count);
    std::vector<char> done(count, 0);
    std::vector<int> stack;
    for (size_t start = 0; start < count; ++start) {
        if (done[start]) continue;
        stack.clear();
        int at = static_cast<int>(start);
        while (at >= 0 && static_cast<size_t>(at) < count && !done[static_cast<size_t>(at)]) {
            if (std::find(stack.begin(), stack.end(), at) != stack.end()) break;
            stack.push_back(at);
            at = parents[static_cast<size_t>(at)];
        }
        for (size_t i = stack.size(); i > 0; --i) {
            const int index = stack[i-1];
            if (done[static_cast<size_t>(index)]) continue;
            done[static_cast<size_t>(index)] = 1;
            order.push_back(index);
        }
    }
    return order;
}

int main(int argc, char** argv) {
    std::ifstream in(argv[1]);
    std::string line;
    std::vector<int> parents;
    while (std::getline(in, line)) {
        const size_t at = line.find("parent=");
        if (line.rfind("xbone ",0)!=0 && line.rfind("node ",0)!=0) continue;
        if (at == std::string::npos) continue;
        parents.push_back(std::atoi(line.c_str()+at+7));
    }
    if (parents.empty()) { printf("keine Hierarchie gefunden\n"); return 0; }

    const std::vector<int> order = ParentsFirst(parents);

    // 1. Vollstaendig?
    if (order.size() != parents.size()) {
        printf("FEHLER: %zu von %zu Knoten in der Reihenfolge\n", order.size(), parents.size());
        return 1;
    }
    // 2. Keine Doppelten?
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size(); ++i)
        if (sorted[i] != static_cast<int>(i)) { printf("FEHLER: Index %zu fehlt oder doppelt\n", i); return 1; }
    // 3. Steht jeder Elternteil VOR seinem Kind?
    std::vector<int> position(parents.size(), -1);
    for (size_t i = 0; i < order.size(); ++i) position[static_cast<size_t>(order[i])] = static_cast<int>(i);
    int verstoesse = 0;
    for (size_t i = 0; i < parents.size(); ++i) {
        const int p = parents[i];
        if (p < 0 || static_cast<size_t>(p) >= parents.size()) continue;
        if (position[static_cast<size_t>(p)] > position[i]) ++verstoesse;
    }
    // Wie viele waeren es OHNE Sortierung?
    int vorher = 0;
    for (size_t i = 0; i < parents.size(); ++i) {
        const int p = parents[i];
        if (p < 0 || static_cast<size_t>(p) >= parents.size()) continue;
        if (static_cast<size_t>(p) > i) ++vorher;
    }
    printf("%-34s %4zu Knoten  Verstoesse: vorher %3d  nachher %d\n",
           argv[2], parents.size(), vorher, verstoesse);
    return verstoesse ? 1 : 0;
}
