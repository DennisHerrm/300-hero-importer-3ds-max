# sdkstub — Übersetzbarkeitsprüfung ohne 3ds Max SDK

**Das hier ist kein SDK.** Es ist ein Satz Attrappen-Header, der genau so
viel vom Max-SDK nachbildet, dass `src/eg3dimport*.cpp` dagegen
übersetzt. Nichts davon läuft; jede Funktion gibt einen Platzhalter
zurück.

## Wozu

Das Plugin bindet Max-Header ein und lässt sich deshalb nur auf einem
Rechner mit installiertem SDK bauen. Das heißt normalerweise: jeder
Tippfehler, jede falsche Signatur, jedes vergessene `override` fällt
erst nach einem SDK-Build auf — und wenn zwölf Jahrgänge im Spiel sind,
nach zwölf.

Mit dem Stub laufen diese Prüfungen vorher und überall:

- Syntax und Typen des ganzen Plugin-Quelltexts
- Signaturen der überschriebenen SDK-Methoden (`override` erzwingt es)
- Vollständigkeit der `#include`s
- die `MAX_RELEASE`-Weichen, beide Zweige (siehe unten)
- Warnungen mit `-Wall -Wextra`

Was er **nicht** prüft: ob die Aufrufe zur Laufzeit das Richtige tun, ob
Max sie in der erwarteten Reihenfolge aufruft, ob eine Klassen-ID
kollidiert. Ein grüner Stub-Build ersetzt keinen echten Build — er
sortiert nur die Fehlerklasse aus, die keinen echten Build gebraucht
hätte.

## Aufruf

```
python3 tools/PRUEFE_PLUGIN.py
```

Das übersetzt `src/eg3dimport.cpp`, `src/eg3dimport_dll.cpp` und die
Parser-Quellen gegen den Stub, einmal je nachgebildetem Jahrgang. Ohne
Argumente werden die Grenzfälle gefahren: 2016 (`MAX_RELEASE 18000`),
2021 (23000), 2022 (24000, ab hier `NonLocalizedClassName` und die neue
`ExecuteMAXScriptScript`-Signatur) und 2027 (29000).

## Warum die Jahrgangsweichen der eigentliche Punkt sind

Zwischen 2016 und 2027 hat Autodesk mehrfach Signaturen geändert. Der
Quelltext hat dafür `#if`-Weichen, und **ein normaler Build sieht immer
nur einen Zweig**. Der andere kann seit Monaten kaputt sein, ohne dass es
auffällt — bis jemand den anderen Jahrgang baut.

Der Stub definiert `MAX_RELEASE` selbst und fährt deshalb beide Zweige
in einem Lauf. Das ist der Grund, warum er existiert.

## Grenzen

Der Stub bildet nur nach, was dieses Plugin benutzt. Wer eine neue
SDK-Funktion aufruft, bekommt einen Fehler „nicht deklariert" — dann ist
die Attrappe in `max.h` zu ergänzen, mit der Signatur aus der
Autodesk-Doku.

## Die Regel, die aus Schaden entstanden ist

**Jede Attrappe wird nachgeschlagen, nie geraten.**

Die erste Fassung dieses Stubs hat acht Fehler durchgewunken, die beim
ersten echten Build gegen Max 2016 alle auf einmal kamen:

| geraten | richtig |
|---|---|
| `end` schließt die Deskriptorliste | `p_end` |
| `NoteKey` hat Standardkonstruktor | `NoteKey(TimeValue, const MSTR&, DWORD = 0)` |
| `Animatable::AppendNoteTrack` | `Animatable::AddNoteTrack` |
| `STDMAT_CLASS_ID` als fertige `Class_ID` | `Class_ID(DMTL_CLASS_ID, 0)` |
| `Mtl::SetTwoSided`, `Mtl::SetOpacity` | sitzen an `StdMat` |
| `Interface::AddModifier` | gibt es nicht — `CreateDerivedObject` + `IDerivedObject::AddModifier` |
| `UtilityObj` kommt über `max.h` | steht in `utilapi.h` |
| `BMTEX_CLASS_ID` als fertige `Class_ID` | `Class_ID(BMTEX_CLASS_ID, 0)` |

Ein Stub, der eine Signatur erfindet, ist schlimmer als keiner: er
erzeugt Vertrauen, das nichts trägt. Die Attrappe zu schreiben ist genau
dann wertvoll, wenn sie dazu zwingt, die Signatur in der Doku
nachzuschlagen — und wertlos, sobald man sie sich ausdenkt.

Alle acht sind jetzt korrigiert und werden vom Stub gefangen; die
Gegenprobe (Fehler wieder einbauen, Prüfung muss anschlagen) gehört zur
Abnahme.
