# CODESTIL

Die Regeln, nach denen dieses Projekt geschrieben ist. Jede steht hier
mit ihrem Grund — eine Regel ohne Begründung wird beim ersten
Zeitdruck gebrochen, und zu Recht.

Die Grundlage sind die [C++ Core
Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines).
Wo dieses Projekt davon abweicht, steht unten warum.

---

## 1. Die Schichtgrenze ist die wichtigste Regel

**Nur `eg3dimport.h`, `eg3dimport.cpp` und `eg3dimport_dll.cpp` binden
Max-Header ein.** Alles unter `src/eg3d_*.*` kommt ohne Max aus.

Das ist keine Stilfrage, sondern eine über die Arbeitsgeschwindigkeit.
Ein Formatfehler in der parserfreien Schicht fällt in `eg3ddump.exe` in
Sekunden auf. Derselbe Fehler hinter einer Max-Abhängigkeit braucht
einen SDK-Build, einen Max-Neustart und einen Import — und wenn zwölf
Jahrgänge unterstützt werden, potenziell zwölfmal.

Wer eine Parserfunktion in `eg3dimport.cpp` schreibt, weil es dort
gerade praktisch ist, verliert genau das. Der Test: lässt sich die
Funktion in `eg3ddump` aufrufen? Wenn nein, steht sie an der falschen
Stelle.

## 2. Threads nur im Parser

Das Max-SDK ist **nicht threadsicher**. Kein `INode`, kein `Mesh`, kein
`Interface`-Aufruf außerhalb des Hauptthreads — auch nicht lesend.

`ParallelFor` gibt es deshalb nur in `eg3d_parallel.*` und wird nur von
der Parserschicht benutzt. Die Aufteilung ist deterministisch: gleiche
Eingabe, gleiche Ausgabe, unabhängig von der Threadzahl. Sonst wäre die
Python-Gegenprobe wertlos, und `--threads 1` gegen `--threads 16` ist
Teil der Abnahme.

## 3. RAII für alles, was zurückgestellt werden muss

Drei Guards in `eg3dimport.h`, alle nach demselben Muster: was der
Konstruktor abschaltet, schaltet der Destruktor wieder an.

| Guard | schaltet ab | ohne ihn |
|---|---|---|
| `SceneRedrawGuard` | Viewport-Updates | 300 Einzelzeichnungen, langsamer als der Import selbst |
| `HoldSuspendGuard` | Undo-Aufzeichnung | ein Undo-Record je Knoten; ein späteres Undo lässt eine halb zurückgerollte Szene stehen |
| `AnimateGuard` | — schaltet **an**: `AnimateOn` | **`SetValue` legt keine Keys an.** Der Import meldet Erfolg, die Zeitleiste bleibt leer |

Ein Importlauf hat ein Dutzend Abbruchstellen. Ein vergessenes
`EnableSceneRedraw` hinterlässt ein totes Max — bei einem `return`, bei
einer Ausnahme, bei jedem `continue` in der falschen Schleife. Genau
dafür gibt es Destruktoren.

Alle drei sind `= delete` für Kopie und Verschiebung. Ein kopierter
Guard würde zweimal freigeben.

## 4. Kein `swprintf_s` in feste Puffer

Meldungen dieses Projekts enthalten Namen aus der Datei: Knoten, Clips,
Materialien, Pfade. Deren Länge steht nicht im Voraus fest.

`swprintf_s` erkennt einen Überlauf, ruft den
Invalid-Parameter-Handler — und **der beendet den Prozess**. Bei XFBIN
Import ist genau das passiert: eine Warnung mit 39 Namen darin, 656
Zeichen lang, Puffer 420 Zeichen. Max stürzte mit
`STATUS_INVALID_PARAMETER` ab, ohne dass irgendwo ein Nullzeiger im
Spiel war, und die Suche ging in die völlig falsche Richtung.

Deshalb `Msg` (ein `std::wostringstream` mit `<<`). Er wächst mit, und
die Frage nach der richtigen Puffergröße stellt sich nicht mehr.

`Msg` konvertiert implizit nach `std::wstring` — das ist eine bewusste
Ausnahme von „keine impliziten Konvertierungen". Ohne sie hatten zwei
Aufrufstellen bei XFBIN das abschließende `.str()` vergessen, und der
Fehler kam erst beim Bauen gegen ein Max-SDK heraus.

## 5. Jeder Zeiger aus dem SDK wird geprüft

`CreateInstance` liefert `nullptr`, wenn eine Klasse fehlt.
`GetCOREInterface` liefert `nullptr`, wenn Max noch nicht so weit ist.
`GetInterface(I_SKINIMPORTDATA)` liefert `nullptr`, wenn das SDK die
Schnittstelle nicht meldet. Alle drei passieren im Feld, nicht nur in
der Theorie.

Ein fehlgeschlagener Teilschritt ist außerdem meist **kein Grund zum
Abbruch**: ein Modell ohne Skin ist brauchbar, ein abgebrochener Import
nicht. Deshalb `Warn()` statt `return false`, außer wenn ohne den
Schritt gar nichts mehr geht.

## 6. Gleitkomma: keine Kontraktion, eigene Zahlenausgabe

`/fp:precise` beziehungsweise `-ffp-contract=off`.

Der Compiler darf `a*b+c` zu einem FMA zusammenziehen. Das ist
schneller *und* genauer — es fällt nur eine Zwischenrundung weg.
Trotzdem unerwünscht: Dequantisierung (`bias + roh*scale`) und
Matrizenmultiplikation bestehen aus genau solchen Summen, und ob
kontrahiert wird, hängt vom Toolset ab. Bei BAF Toolkit gemessen: 18
von 21.166 Dumpzeilen ändern sich dadurch. 18 Zeilen, an denen nichts
falsch ist, und deren Suche einen Abend kostet.

Aus demselben Grund rechnet `FormatFloat()` die Dezimalausgabe selbst
statt über `printf` oder `<charconv>`: die Tie-Regel beim Runden steht
so im Quelltext und ist in C++ und Python dieselbe, statt in zwei
Laufzeitbibliotheken zu stehen.

## 7. C++17, nicht 20

Nicht aus Vorsicht, sondern weil C++17 gegen die Header der älteren
Max-SDKs übersetzt und C++20 nicht. Zwölf Jahrgänge sind die
Anforderung; der neueste Standard, der durch alle zwölf geht, gewinnt.

`<charconv>` wird ausdrücklich nicht gebraucht (siehe oben) — damit
entfällt auch die Weiche für Toolsets vor VS 2019 16.4.

## 8. Keine Fremdbibliotheken

Eigener DEFLATE, eigener JSON-Parser. Die Alternative wäre, zlib und
nlohmann gegen zwölf Max-Jahrgänge und zwei Toolsets zu bauen — mehr
Arbeit als beide Module zusammen, und bei jedem neuen Jahrgang von
vorn.

Der Preis dafür ist Beweislast: der Inflate ist gegen Pythons `zlib`
verifiziert, byteweise, alle Blöcke. Ohne diesen Nachweis wäre die
Entscheidung nicht vertretbar.

## 9. `const`, `override`, `= delete` konsequent

- `override` an jeder überschriebenen Methode. Es ist der einzige
  Schutz gegen eine geänderte SDK-Signatur — und die ändern sich (siehe
  `NonLocalizedClassName`).
- `const` an Referenzparametern und Methoden, wo möglich. cppcheck
  meldet Verstöße; der Bestand ist bei 0 Funden.
- `= delete` für Kopie und Verschiebung bei allem, was eine Ressource
  oder einen Szenenzeiger hält.

## 10. Aggregat statt langer Parameterliste

`ImportOptions` und `ImportStats` sind schlichte Strukturen mit
Vorgaben. Eine Option zu ergänzen heißt: ein Feld hinzufügen, keine
einzige Aufrufstelle anfassen. Und niemand vertauscht zwei
`bool`-Argumente in einem Aufruf mit acht davon.

## 11. Namen ohne Abkürzungen, Kommentare mit Grund

Kommentare beschreiben **warum**, nicht was. `// Schleife über alle
Knoten` steht nirgends; `// SetNodeTM arbeitet in Weltkoordinaten,
deshalb muss der Elternteil schon stehen` steht da, wo es zählt.

Wo eine Entscheidung anders aussieht, als sie sollte — das nicht
transponierte Matrix44, das `new` statt eines statischen Objekts bei
`SceneImport`, die Konjugation an genau einer Stelle — steht der Grund
im Quelltext. Diese Stellen sind die, an denen ein späterer Leser sonst
„korrigiert".

## 12. Prüfen, was ein Compiler nicht prüft

Vier Skripte unter `tools/`, alle ohne Max lauffähig:

| Skript | fängt |
|---|---|
| `PRUEFE_PLUGIN.py` | Übersetzen **und Linken** gegen den SDK-Stub, **beide Zweige jeder `MAX_RELEASE`-Weiche** |
| `PRUEFE_API.py` | eine Funktion, die in zwei von drei Listen steht (enum / FUNCTION_MAP / Deskriptor) — in MAXScript schlicht nicht da, im Quelltext unsichtbar |
| `PRUEFE_INCLUDES.py` | ein SDK-Symbol ohne seinen Header — baut überall außer bei Max 2016 |
| `PRUEFE_SDK.py` | bekannte SDK-Fallen (statisches `SceneImport`-Objekt, `AnimateOn` vergessen) |
| `PRUEFE_PAKET.py` | `PackageContents.xml` gegen die Autodesk-Spezifikation und gegen den tatsächlichen Paketinhalt, in **beide** Richtungen |
| `PRUEFE_SCRIPTS.py` | jeden `Eg3dCpp.x`-Aufruf in der `.mcr` gegen den C++-Deskriptor: Name, Klammern, Argumentzahl |
| `PRUEFE_LOCALE.py` | dieselbe Ausgabe unter C-Locale und einer Komma-Locale |

Dazu `cppcheck --enable=all` (0 Funde) und die Python-Gegenprobe des
Parsers (6.025.631 Zeilen, 0 Abweichungen).

Der gemeinsame Nenner: **ein normaler Build sieht immer nur einen
Zweig.** Der andere kann monatelang kaputt sein. Diese Skripte fahren
beide.

## 13. Eine Attrappe wird nachgeschlagen, nie geraten

Der SDK-Stub in `tools/sdkstub/` hat in seiner ersten Fassung acht
Fehler durchgewunken, weil die Signaturen darin erfunden statt
nachgeschlagen waren — `end` statt `p_end`, `AppendNoteTrack` statt
`AddNoteTrack`, `SetOpacity` an der falschen Klasse. Der erste echte
Build gegen Max 2016 brachte alle acht auf einmal.

Ein Prüfwerkzeug, das eine Annahme enthält, prüft die Annahme nicht —
es zementiert sie. Und weil es grün meldet, sieht es aus wie ein
Beweis. Das ist schlimmer als gar keine Prüfung: ohne sie hätte man
vorsichtig gebaut.

Die Regel gilt für jede Attrappe, jeden Mock, jeden Testdouble: der
Wert liegt darin, dass das Schreiben zum Nachschlagen zwingt. Wer sich
die Signatur ausdenkt, hat die Arbeit gemacht und den Nutzen nicht.

Zur Absicherung gehört die Gegenprobe: den Fehler absichtlich wieder
einbauen und prüfen, ob das Werkzeug anschlägt. Ein Prüfskript, das nie
etwas gemeldet hat, ist unbewiesen.

## 14. Eine Liste an zwei Stellen geht genau einmal gut

`INSTALLIERE.bat` hatte die `.mcr`-Dateinamen fest verdrahtet,
`PackageContents.xml` ebenfalls. Zwei Listen, die niemand aneinander
erinnert. In 0.1.0 stand in der einen ein Name, den es nie gab — die
Installation brach ab, nachdem alle zwölf Plugins fertig gebaut waren.

Beide Stellen benutzen jetzt einen Platzhalter (`*.mcr`; die
Spezifikation erlaubt ihn im Dateinamen, nicht im Pfad), und
`PRUEFE_PAKET.py` gleicht gegen den tatsächlichen Ordnerinhalt ab —
in beide Richtungen. Eine XML-Zeile ohne Datei ergibt einen Eintrag im
`max.log`, den niemand liest; eine Datei ohne XML-Zeile wird nie
geladen. Beides fällt sonst erst beim Benutzer auf.

Die allgemeine Form: wo dieselbe Information an zwei Orten steht, gehört
entweder ein Platzhalter hin oder eine Prüfung, die sie vergleicht. Ein
Kommentar „bitte synchron halten" ist keine von beiden.

## 15. Was erst zur Laufzeit ausgewertet wird, braucht eine eigene Prüfung

Die `.mcr` ist MAXScript. Kein Compiler sieht sie an, und ausgewertet
wird sie erst in dem Moment, in dem jemand auf den Knopf drückt. Ein
Tippfehler in einem selten benutzten Knopf fällt beim Benutzer auf oder
gar nicht.

In 0.1.0 stand dort `Eg3dCpp.kind` ohne Klammern. MAXScript liefert dann
das Funktionsobjekt statt seines Ergebnisses — Fehlermeldung beim
Öffnen einer Datei: `Unable to convert: kind() to type: String`,
Zeile 128. Zwölf gebaute Plugins, ein fehlendes Klammerpaar.

`PRUEFE_SCRIPTS.py` liest jetzt den Deskriptor aus `eg3dimport.cpp` und
prüft jeden Aufruf dagegen: existiert die Methode, hat sie Klammern wenn
sie parameterlos ist, stimmt die Argumentzahl. Bei der Gegenprobe findet
es genau Zeile 128 wieder.

Das gilt für jede Schicht, die der Compiler nicht sieht: Skripte,
Konfigurationsdateien, Ressourcen, Datenbankschemata. Entweder es gibt
eine Prüfung dafür, oder der Benutzer ist die Prüfung.

## 16. Ein Fehlschlag muss diagnostizierbar sein

Der Importer hat jetzt ein Protokoll (`Eg3dCpp.log()`,
`Eg3dCpp.saveLog()`, `setDebug`), und zwar mit zwei Eigenschaften, die
nicht selbstverständlich sind:

- **Es wird protokolliert, BEVOR ein Schritt läuft, nicht danach.**
  Stürzt der Import mitten in einem Schritt ab, ist die letzte Zeile
  genau die Information, auf die es ankommt. Ein Protokoll, das nur
  fertige Schritte meldet, schweigt über den einen, der nicht fertig
  wurde.
- **Die Protokollknöpfe werden auch bei einem fehlgeschlagenen Import
  freigeschaltet.** Genau dann braucht man sie.

Warnungen und Protokoll sind getrennt: eine Warnung ist ein Befund
("zwei Bind-Matrizen nicht invertierbar"), das Protokoll ist der
Verlauf ("Knoten fertig (139), baue Meshes ..."). Beides in einen Topf
zu werfen macht aus einem lesbaren Befund eine Textwand.

## 17. Das Locale ist eine versteckte Eingabe

`strtod`, `sscanf`, `atof`, `iostreams` ohne `imbue` — sie alle lesen im
Locale des **Prozesses**. Auf einem deutschen Windows ist der
Dezimaltrenner ein Komma, und dann liest `strtod("0.5")` die Null,
bleibt am Punkt stehen und **meldet Erfolg**: Rückgabe 0.0, `end` zeigt
hinter die Ziffer. Die übliche Fehlerprüfung (`end == anfang`) schlägt
nicht an.

Im Feld sah das so aus: der Dump aus dem Plugin zeigte in 58 von 192
`matvec`-Zeilen Nullen, wo dasselbe Werkzeug unter Linux echte Farben
las. Betroffen war ausschließlich, was einen Dezimalpunkt hat.
Ganzzahlen kamen durch, Vertexdaten sowieso — die werden binär gelesen.
Ein Fehler, der sich in genau den Zeilen versteckt, die niemand
nachrechnet.

Der Fix ist `_strtod_l` / `strtod_l` mit ausdrücklichem C-Locale — nicht
ein selbstgeschriebener Zahlenparser. Die Bibliotheksfassung rundet
korrekt zum nächsten `double`, eine Mantisse-mal-Zehnerpotenz-Rechnung
nicht. Bei einem Projekt, dessen Abnahme auf byteweiser Übereinstimmung
mit Python beruht, wäre das ein Eigentor.

**Der lehrreichere Teil ist, warum es nie auffiel.** Ein C-Programm
startet immer im C-Locale, egal was in der Umgebung steht. `LC_ALL=de_DE`
zu setzen ändert nichts, solange niemand `setlocale()` ruft. 3ds Max ruft
es, `eg3ddump.exe` nicht. Das Werkzeug lief also *strukturell* in einer
anderen Umgebung als das Plugin — und die Gegenprobe gegen Python, 6
Millionen Zeilen stark, konnte diesen Fehler grundsätzlich nicht finden.

Eine Prüfung ist nur so gut wie die Umgebung, in der sie läuft. Wenn das
Prüfwerkzeug in einer anderen Umgebung läuft als das Produkt, gibt es
eine ganze Fehlerklasse, die es per Konstruktion nicht sieht. `eg3ddump`
hat deshalb jetzt `--locale`, und `PRUEFE_LOCALE.py` fährt den Vergleich.

## 18. Eine Dateiendung gehört einem nicht

`.model` gehört seit Max 2016 dem mitgelieferten ATF CATIA V4 Importer.
Wählt der Benutzer im Import-Dialog "All formats", entscheidet Max
anhand der Endung — und der eingebaute gewinnt. Die Meldung lautet dann
`039_skin8.model is either corrupt or not a supported version`, was
aussieht, als sei die Datei kaputt.

Vor dem Anmelden einer Endung gehört die Frage, wer sie schon hat. Und
unabhängig davon braucht ein Plugin einen Zugang, der nicht an der
Endung hängt — hier das Utilities-Panel und das Makro.

## 19. Eine Zahl im Protokoll, die niemand liest, ist keine Prüfung

Der erste `.x`-Import meldete im Ergebnis:

    Knoten 131, Meshes 1, Vertices 12318, ..., geskinnt 0, Materialien 0

`geskinnt 0` stand da, korrekt gemeldet, und fiel trotzdem nicht auf —
weil das Modell im Viewport vollständig aussieht. Ein Mesh ohne Skin
merkt man erst, wenn man die Zeitleiste bewegt. Der Grund war banal:
für `.x` gab es überhaupt keinen Skin-Schritt. Der Parser las
`boneIndices` und `weights`, die Max-Schicht benutzte sie nie.

Zahlen, die einen Fehlerfall beschreiben, gehören nicht ins Protokoll,
sondern in eine Warnung — oder besser in eine Prüfung. `geskinnt 0` bei
vorhandenen Gewichten ist kein Statuswert, das ist ein Befund.

## 20. Ein Objekt anzulegen ist nicht dasselbe wie es zu benutzen

`CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID)` liefert ein
gültiges Bone-Objekt — mit Länge, Breite und Höhe 0. Das ist im
Viewport unsichtbar, also sah der Import nach Point-Helpern aus, obwohl
echte Bones angelegt wurden.

Die Abmessungen stehen im Parameterblock `boneobj_params`
(`boneobj_width`, `_height`, `_taper`, `_length` aus `plugapi.h`), und
die Länge ist keine freie Zahl: die Doku sagt, die Bone-Geometrie werde
"lengthwise from the pivot point to the bone's child object"
gezeichnet. Die Länge ist der Abstand zum Kind — was heißt, dass sie
erst gesetzt werden kann, wenn die Hierarchie steht, und bevor die
Animation die Knoten von Frame 0 wegbewegt. Drei Schritte in genau
dieser Reihenfolge.

Die allgemeine Form: bei jedem SDK-Objekt ist nach `CreateInstance` die
Frage fällig, welche Parameter noch 0 sind und ob 0 dort ein
brauchbarer Wert ist.

## 21. Weltkoordinaten setzen heißt: Reihenfolge zählt

`INode::SetNodeTM` setzt die **Welt**matrix. Wird danach der Elternteil
bewegt, wandert das Kind mit — relativ zu seiner neuen Elternlage. Wer
ein Kind setzt, bevor sein Elternteil steht, verschiebt es zweimal.

In den `.x`-Dateien stehen die Bones alphabetisch, nicht nach
Hierarchie: `Bip001 Head` ist Index 1, sein Elternteil `Bip001 Neck` ist
Index 26. Der Kopf wurde also gesetzt, lange bevor der Hals stand — und
danach von Hals, Wirbelsäule und Becken je noch einmal mitgenommen.

Im Viewport sah das aus wie ein Fehler in der Bind-Pose oder in der
Quaternion-Konvention: Arme abgespreizt, Einzelteile quer im Raum,
Knoten mit Koordinaten wie `X = -1312046`. Es war keins von beidem.

Über den ganzen Bestand gemessen: **162 Stellen, an denen ein Kind vor
seinem Elternteil steht.** Nach der topologischen Sortierung 0. Die
Zahl "vorher" gehört zur Prüfung dazu — ohne sie wäre "0 nachher"
wertlos, weil man nicht wüsste, ob überhaupt etwas gemessen wurde.

Die Reihenfolge ist auch der Grund, warum `ShapeBones()` zwischen
Hierarchieaufbau und Animation läuft: die Bonelänge ist der Abstand zum
Kind bei Frame 0, und danach stehen dort die Keys.

**Und sie gilt in der Animation genauso.** Beim ersten Anlauf war nur
die Bind-Pose topologisch sortiert, die Animationsschleife nicht.
Ergebnis: saubere Ruhestellung, zerrissenes Modell sobald man die
Zeitleiste bewegte — was aussieht wie ein Fehler in der
Quaternion-Konvention und keiner ist. `SetNodeTM` rechnet gegen die
Elternlage **zum Zeitpunkt t**; hat der Elternteil bei t noch keinen
Key, steht er auf seiner Frame-0-Lage.

Einen Fehler an einer Stelle zu beheben und die zweite Stelle stehen zu
lassen, ist die häufigste Art, ihn zweimal zu suchen. Nach jeder
Korrektur gehört die Frage dazu, wo dieselbe Konstruktion sonst noch
steht — hier: `grep SetNodeTM`.

## 23. Lokale Werte sind keine Weltwerte

Die `.model`-Kanäle enthalten **lokale** Position, Rotation und
Skalierung — dieselbe Konvention wie die Ruhelage in den Knotendaten.
Version 0.1.4 schrieb sie direkt in die Knotenmatrix, also als
Weltwerte. Für einen Knoten ohne Elternteil sieht das richtig aus, für
alle anderen nicht.

Zwei weitere Fehler steckten in derselben Schleife:

- **Die drei Kanäle wurden nacheinander in dieselbe Matrix
  geschrieben.** Erst die Translation setzen, dann die Rotation
  darüberlegen, löscht jeweils den vorigen Teil. Aus Position, Rotation
  und Skalierung entsteht *eine* Matrix, also müssen sie zusammen
  ausgewertet werden — mit vereinigten Keyzeiten, weil Position 200 Keys
  haben kann und Rotation drei.
- **Fehlende Kanäle wurden aus der aktuellen Knotenmatrix ergänzt.**
  Ein Knoten mit nur einem Rotationskanal blieb dann dort stehen, wo ihn
  der vorige Clip abgestellt hatte. Die Ruhelage ist der richtige
  Rückfallwert.

Dazu ein Zeiger, den der Stub nicht sehen konnte: `slot.location` wurde
dereferenziert, ohne auf `nullptr` geprüft zu werden — bei den anderen
beiden Kanälen stand die Prüfung da. Attrappen stürzen nicht ab, also
fängt kein Stub-Build so etwas. Dafür ist cppcheck da, und dafür ist die
Symmetrie im Code ein Hinweis: drei gleichartige Blöcke, einer ohne
Prüfung.

## 22. Ausreißer machen Mittelwerte unbrauchbar

Die Bone-Dicke hing zuerst an `SceneExtent()`, der Ausdehnung aller
Knoten. Ein einziger Partikel-Emitter weit außerhalb — und alle Bones
werden zu Balken. Jetzt entscheidet der **Median** der Kindabstände;
der interessiert sich nicht für Ausreißer.

Dieselbe Überlegung steht schon im Parser: `SolveFrames()` rankt die
Framezahl-Hypothesen nach der Medianabweichung der Quaternionlängen,
nicht nach dem Anteil exakt getroffener Werte. Der Anteil-Ansatz kippt
bei Dateien mit vielen stillstehenden Bones.

## 24. Wo eine Schicht nachweislich funktioniert, gehört der Code hin

Note Tracks lagen zuerst in C++: `Interface::GetRootNode()` plus
`Animatable::AddNoteTrack`. Das übersetzte, lief, legte einen Track an —
und im Ergebnis war keiner zu sehen.

Der XFBIN-Importer macht es in MAXScript, mit `addNoteTrack rootNode`
und `addNewNoteKey`. Der Grund steht als Kommentar in dessen Quelltext:
`rootNode` ist die **Szenenwurzel** und eine MAXScript-Konstante, keine
Variable — und Exporter suchen den Track genau dort. Der Autor hatte sie
einmal für eine undefinierte Variable gehalten und den Track an den
Wurzel-Bone gehängt, wo ihn niemand findet.

Ob `Interface::GetRootNode()` denselben Knoten liefert, den MAXScript
unter `rootNode` versteht, ist eine Annahme. Sie mag stimmen; geprüft
hat sie niemand. Der Importer, der nachweislich funktionierende Note
Tracks schreibt, macht es in MAXScript — also hier auch. C++ liefert nur
die Clipgrenzen (`clipRanges()`).

Das ist keine Regel gegen C++, sondern gegen ungeprüfte Äquivalenz-
annahmen zwischen zwei Schichten. Wenn eine davon belegt funktioniert,
ist sie der Ort — bis jemand die andere tatsächlich prüft.

## 25. Ein mitlaufender Zähler ist keine Zuordnung

`BuildModelSkin` fuhr dieselbe Schleife noch einmal ab wie
`BuildModelMeshes` und zählte einen Cursor mit, um zum n-ten Mesh das
n-te Submesh zu finden. Das hält genau so lange, wie beide Schleifen
dieselben Elemente überspringen — und `BuildModelMeshes` überspringt:
leere Submeshes, und solche mit Indices außerhalb des Vertexbereichs.

Danach bekommt jedes Mesh die Gewichte eines anderen, ohne dass
irgendwo ein Fehler gemeldet wird.

Jedes gebaute Mesh trägt jetzt seine Herkunft mit sich (`BuiltMesh`:
Knoten, Instanz, Submesh). Das kostet zwei `int` und macht die
Zuordnung unabhängig davon, was übersprungen wurde. Die allgemeine
Form: wo zwei Schleifen über dieselben Daten laufen müssen und eine
davon filtert, gehört die Verbindung in die Daten — nicht in einen
Index, der in beiden Schleifen gleich zählen soll.

## 26. Die Grenzen des Ziels gehören in die Entscheidung

Der Compact Material Editor hat **höchstens 24 Slots** (je nach
Einstellung 6, 15 oder 24). `039_skin8.model` hat 50 Materialien — sie
passen dort grundsätzlich nicht alle hinein, und keine noch so saubere
Implementierung ändert das.

Also kommen nur die tatsächlich benutzten hinein (11 statt 50), und
wenn auch die nicht passen, sagt eine Meldung im Listener, wie viele
übrig sind und wo sie stehen. Der Editor ist kein Materialverwalter;
alle Materialien hängen an den Meshes und sind über *Get Material →
Scene* erreichbar.

Vor dem Bauen die Frage, was das Ziel überhaupt aufnehmen kann. Sonst
entsteht eine Funktion, die in der Hälfte der Fälle stillschweigend
Daten verliert.

## 27. Der beste bekannte Stand ist ein Rückfallpunkt, kein Zwischenschritt

Nach 0.1.8 ("Animationen klar und sauber, nur das ganze Modell um 90°
gedreht") kamen vier Versuche, die Drehung zu beheben: Bind-Pose aus
`inv(IBM)`, dann eine gemessene Korrekturmatrix, dann dieselbe je
Palette, dann eine Wurzeldrehung. **Jeder einzelne war schlechter als
0.1.8.**

Der Fehler war nicht in den Versuchen — die Messungen waren richtig, die
Palettendiagnose hat sogar aufgedeckt, dass die Datei zwei verschieden
abgelegte Charaktere enthält. Der Fehler war, dass jeder Versuch auf dem
vorigen aufbaute, statt auf dem letzten guten Stand. Nach vier Schritten
war nicht mehr unterscheidbar, welche Änderung was verursacht hatte.

Zwei Konsequenzen im Code:

- **Jeder Eingriff, der über den bewährten Stand hinausgeht, ist eine
  eigene Option** mit eigenem Schalter und Standard *aus*:
  `bindPoseFromInvBind`, `bindScaleOnly`, `matchBindScale`,
  `upAxisFix`. Sie lassen sich einzeln zu- und abschalten, statt sich
  zu überlagern.
- **Was sich als falsch erwiesen hat, bleibt als Diagnose drin.** Die
  Palettenmessung korrigiert nichts mehr, meldet aber im Protokoll, ob
  eine Palette überhaupt eine einheitliche Differenz hat (Palette 0:
  91 von 91, Palette 2: 0 von 80). Diese Zahl war die einzige echte
  Erkenntnis aus den vier Versuchen.

Die Regel dahinter: wenn eine Änderung das Ergebnis verschlechtert,
gehört sie zurückgenommen und nicht durch die nächste ergänzt. Der
letzte Stand, von dem jemand gesagt hat "das war gut", ist der
Ausgangspunkt für den nächsten Versuch — nicht der aktuelle.

**Und "zurückgebaut" wird geprüft, nicht behauptet.** Beim ersten
Versuch, 0.1.8 wiederherzustellen, hatte ich aus dem Gedächtnis
rekonstruiert und zwei Dinge falsch:

- Eine Maßstabsangleichung eingebaut, die es in 0.1.8 **gar nicht
  gab** — `bindScale_` kommt dort im ganzen Quelltext nicht vor.
- Die Elternkette aus eigenen Matrizen aufgebaut statt aus der Szene
  (`parent->GetNodeTM(0)`). Das ist nicht dasselbe: in der Szene steht
  der Elternteil schon auf seiner **Bind**-Lage, wenn er eine
  Bindmatrix hatte, und ein Kind ohne eigene erbt sie dadurch. Aus
  meta[5] aufgebaut geht genau diese Vererbung verloren.

Beides fiel erst auf, als der alte Stand danebenlag und Funktion für
Funktion verglichen wurde. Ein Rückbau ohne diesen Vergleich ist eine
Vermutung — und ein neuer Fehler, der wie der alte aussieht.

## 28. Übersetzen ist nicht Linken

`PRUEFE_PLUGIN.py` hat ein Jahr lang nur `-c` gemacht — übersetzt, nicht
gelinkt. Damit fängt es eine ganze Fehlerklasse nicht: **ein fehlender
Funktionskörper fällt beim Übersetzen nicht auf**, weil die Deklaration
im Header dem Compiler genügt. Erst der Linker meldet ihn.

Konkret: `MeshScale()` war deklariert und aufgerufen, aber nie
definiert. Der Stub-Lauf meldete für alle vier Jahrgänge "sauber", und
alle zwölf echten Builds scheiterten an

```
error LNK2019: nicht aufgelöstes externes Symbol
"private: float SceneBuilder::MeshScale(void) const"
```

Der Linkschritt kostet Sekunden. Aber er braucht **`-Wl,--no-undefined`**
— ohne das erlaubt ein Shared Object undefinierte Symbole, der Link geht
durch, und die Prüfung ist wieder wertlos. Beim ersten Anlauf stand die
Option nicht da, und die Gegenprobe meldete weiterhin "sauber".

Das ist Regel 13 zum zweiten Mal: **ein Prüfschritt ohne Gegenprobe ist
unbewiesen.** Diesmal war es kein erfundener Wert im Stub, sondern eine
fehlende Linker-Option — dasselbe Ergebnis, ein grünes Werkzeug, das
nichts misst.

## 29. Ein eigener Name, der schon vergeben ist, wirkt an ganz anderer Stelle

Die `.mcr` hatte eine Hilfsfunktion `fn openFile path` — genauso heißt
eine **eingebaute** MAXScript-Funktion. Damit verdeckt die eigene die
echte, und jeder spätere Aufruf landet bei der falschen.

Der Fehler äußerte sich weit entfernt von seiner Ursache: der
Szenendump rief `openFile out mode:"a"` auf, um die Logdatei zum
Anhängen zu öffnen, bekam aber den Modell-Lader — und der meldete
*"weder EG3D noch JUMPX"*. Die Meldung kam also aus dem Import,
angezeigt beim Speichern des Logs, ausgelöst durch einen Namen, der
drei Bildschirmseiten weiter oben vergeben wurde.

`PRUEFE_SCRIPTS.py` prüft jetzt gegen eine Liste eingebauter Namen
(`openFile`, `createFile`, `close`, `select`, `delete`, `append`, …).
Gegenprobe gemacht: mit wieder eingebautem `fn openFile` meldet es die
Zeile.

Das ist derselbe Fehlertyp wie bei `end` statt `p_end` im SDK-Stub:
ein Bezeichner, den man für frei hält. In einer Sprache ohne
Namensräume — MAXScript, MEL, alte Makrosprachen — ist die Prüfliste
das einzige Mittel.

## 31. "Wird gelesen" ist nicht "wird richtig gelesen"

Der Parser meldete lange **51 von 51 gelesen** — und dabei waren:

- `068.x` und sechs weitere **ohne Mesh** (der Parser meldete "keine
  brauchbare Positionsadresse" und machte weiter),
- 11 Monster-Dateien mit **Quaternionen der Länge 0.02** statt 1.0
  (float32 gelesen, wo Axis-Angle steht),
- `monster_youlong.x` mit **Clips bis Frame 156 bei 53 Frames Daten**.

Alle drei Klassen standen in der Statistik als Erfolg. Erst als DH
fragte, ob wirklich *alle* Dateien korrekt importiert werden, habe ich
gemessen statt gezählt.

`PRUEFE_BESTAND.py` prüft deshalb je Datei den Inhalt:

| | |
|---|---|
| Mesh | Geometrie vorhanden, wenn welche deklariert ist |
| Vertices | nicht alle im selben Punkt, keine NaN |
| Normalen | Länge 1 (95 % der Stichprobe) |
| Quaternionen | Länge 1 im Mittel |
| UVs | im Bereich −2…3 |
| Clips | innerhalb der Zeitleiste |
| Gewichte | Summe 1 |

**Gegenprobe gemacht** — das ist der Teil, ohne den ein Prüfwerkzeug
wertlos ist: derselbe Lauf gegen den *alten* Parser meldet **28 von 102
mit Befund**, gegen den neuen **0**.

Zwei Fehler steckten anfangs in der Prüfung selbst, nicht im Parser: sie
kannte nur die `.x`-Zählzeile und hielt eine 1 Byte große kaputte Datei
sowie eine reine Effektdatei ohne Geometrie für Fehler. Auch ein
Prüfwerkzeug braucht Prüfung.

## 32. Eine Attrappe mit dem falschen TYP ist schlimmer als eine fehlende

`LININTERP_POSITION_CLASS_ID` steht im SDK als **einzelne Zahl**
(PartA), der Aufrufer bildet `Class_ID(WERT, 0)`. Im Stub hatte ich es
als fertiges `Class_ID`-Objekt definiert.

Folge: der Stub-Lauf meldete für alle vier Jahrgänge „sauber", und alle
zwölf echten Builds scheiterten an

```
error C2664: Konvertierung von Argument 2 von "int" in "Class_ID"
nicht möglich
```

Eine fehlende Deklaration fällt sofort auf. Eine mit dem falschen Typ
macht das Prüfwerkzeug **grün, obwohl der Code nicht baut** — sie ist
schlimmer als gar keine, weil sie Sicherheit vortäuscht.

Das ist Regel 13 zum zweiten Mal (`end` statt `p_end` war der erste
Fall). Die Konsequenz bleibt dieselbe: **jede Attrappe wird
nachgeschlagen, nie geraten** — und danach die Gegenprobe, ob der Stub
den Fehler auch wirklich meldet. Hier gemacht: mit wieder entferntem
`Class_ID(...)` meldet er dieselbe Zeile wie der echte Build.
