# EG3D Import — 300 Heroes (300英雄) für 3ds Max 2016–2027

C++-Importer für beide Modellformate des Spiels, nach dem Vorbild von
[[xfbin-max-importer]] und [[baf-max-importer]] aufgebaut.

| Format | Engine | Erkennung |
|---|---|---|
| `.model` | neuer (`EG3D` / `ezgame`) | Magic `EG3D` |
| `.x` | älter (`JUMPX V5.01`) | Magic `JUMPX` |

Erkannt wird an der **Signatur**, nicht an der Endung — die `.x`-Dateien sind
trotz Endung keine DirectX-X-Files.

Grundlage ist das Blender-Addon `io_scene_eg3d` 1.14.0. Das Addon ist die
Quelle des Formatwissens; der Maßstab ist es nicht (siehe „Gegenprobe“).

---

## Stand: Stufe 0

Fertig und verifiziert ist der **Parser** samt eigenständigem Werkzeug. Die
Max-Schicht (`src/eg3dimport.cpp`) kommt in Stufe 1 — `BAUE_ALLE.bat` weiß das
und überspringt das Plugin, solange die Datei fehlt.

**Gegenprobe bestanden:** 53 Retail-Dateien (48 `.model`, 5 `.x`),
**6.025.631 Zeilen**, **468 MB**, **0 Abweichungen** — byteweise identisch mit
der Python-Referenz. Verglichen wird jeder einzelne Vertex (Position, Normale,
Tangente, Farbe, Bone-Indices, Gewichte, beide UV-Sätze), jedes Dreieck, jede
inverse Bind-Matrix, jeder Animations-Keyframe und jeder Bone-Frame beider
Formate.

Zusätzlich geprüft: **1 Thread und 16 Threads liefern bitgleiche Ausgaben.**

---

## Bauen

```
BAUE_ALLE.bat            alles: Werkzeug + alle installierten SDKs
BAUE_ALLE.bat 2027       nur ein Jahrgang
```

Sucht Visual Studio über `vswhere`, baut zuerst `eg3ddump.exe` (ohne SDK) und
danach je Jahrgang eine `.dlu`. `STOP_ON_ERROR=0` setzen, um trotz Fehler
weiterzubauen.

## Gegenprobe

```
VERGLEICHE.bat C:\300heroes\models           schnell, nur Kopfzahlen
VERGLEICHE.bat C:\300heroes\models --all     alles, ~470 MB je Seite
```

Oder von Hand:

```
output\tools\eg3ddump.exe <ordner> --all -o cpp.txt
python tools\pydump.py    <ordner> --all -o py.txt
fc /L cpp.txt py.txt
```

## Installieren

```
INSTALLIERE.bat          nach %APPDATA%\Autodesk\ApplicationPlugins\Eg3dImport
DEINSTALLIERE.bat
```

Danach in Max: **Datei → Importieren → 300 Heroes Model (\*.model, \*.x)**.

---

## Warum eine zweite Implementierung statt Testfälle

Ein Testfall prüft, was sich jemand ausgedacht hat. Zwei unabhängig
geschriebene Parser, die über 53 Retail-Dateien zeichengleiche Ausgaben
liefern, schließen die ganze Fehlerklasse aus, die „sieht im Viewport richtig
aus“ überlebt.

`tools/pydump.py` hängt deshalb **nicht** am Blender-Addon. Wäre das Addon
gleichzeitig Quelle und Maßstab, käme ein Denkfehler darin auf beiden Seiten
gleich heraus und bliebe unsichtbar.

Zwei echte Funde schon beim allerersten Vergleich:

* Die JUMPX-Signatur lief ohne GBK-Umwandlung in den Dump — rohe Bytes, kein
  gültiges UTF-8. Wäre im Viewport nie aufgefallen.
* `raw_decode` zählt **Zeichen**, der C++-Parser zählte **Bytes**. Bei
  chinesischen Materialnamen laufen die Zahlen um bis zu 20 auseinander.

---

## Threads

Autodesk sagt es in der Entwicklerdoku unter „Thread Safety“ ohne Umschweife:
3ds Max ist nicht threadsicher, und Referenzsystem sowie Knotenauswertung sind
ausdrücklich einfädig. Daraus folgt eine harte Trennung:

| Schicht | Threads |
|---|---|
| Parser (`eg3d_*`) | ja — `ParallelFor` über Submeshes und Bones |
| Max-Schicht (`eg3dimport.cpp`, Stufe 1) | **nein**, ausschließlich Hauptthread |

Das ist nicht nur Vorsicht, sondern die Aufteilung, die überhaupt etwas
bringt: teuer sind das Entpacken, das Dekodieren der Vertexattribute und das
Abtasten der Bonespuren — alles reine Rechenarbeit auf Rohdaten.

`ParallelFor` schreibt ausschließlich in eigene Ausgabeplätze, ohne Reduktion.
Deshalb ist das Ergebnis bitgleich mit dem einfädigen Lauf — Voraussetzung
dafür, dass die Gegenprobe überhaupt etwas aussagt. `--threads 1` schaltet ab.

---

## Keine Fremdbibliotheken

Zwei Dinge, die man normalerweise einbindet, stehen hier im Quelltext:

**zlib** (`eg3d_inflate.cpp`, RFC 1950/1951). Eine Abhängigkeit müsste für
zwölf Max-Jahrgänge und zwei Toolsets vorliegen. Der Entpacker ist dagegen
abgeschlossen und lässt sich gegen die Python-`zlib` prüfen: über alle fünf
`.x`-Dateien sind beide Blöcke byteweise identisch, und der Adler-32 stimmt.

**JSON** (`eg3d_json.cpp`). Die `.model`-Metadaten sind JSON — bis zu 548 KB.
Gebraucht werden Arrays, Objekte, Zahlen, Zeichenketten und `null`; mehr steht
nicht drin.

Bei JSON zählt vor allem eines: **`null` ist nicht `0`.** Ein Knoten mit
`translationOffset = null` hat keine Translation; einer mit `0` hat eine, die
am Anfang des Binärblocks liegt. Wer `null` zu `0` macht, liest zwölf fremde
Bytes als Position. Dafür gibt es `JsonRef::TryInt`.

---

## Die eine Sache, die beim `.model` zählt

Jeder Offset im JSON ist **relativ zum Binärblock**, und der beginnt bei
Dateibyte 12:

```
absolut = 12 + offsetAusJson
```

Als absolute Dateioffsets gelesen sind das genau 12 Byte daneben, und 12 Byte
sind je nach Attribut 1,5 bis 6 Elemente. Das Mesh wird dann aus fremden
Vertices gebaut. Die Symptome — Löcher, Bridge-Triangles, `|q| ≈ 0,14` beim
Skelett, Gewichte ≠ 1, ein scheinbarer 6-`u16`-Müllkopf vor jedem Indexpuffer —
sahen jahrelang wie sechs verschiedene Probleme aus. Es war eines.

`--layout` schreibt die Puffertabelle: mit der richtigen Basis ist **jede
Lücke 0**. Haben alle Lücken denselben Wert ≠ 0, ist die Basis falsch, und
`layoutsum ... allSame=1` sagt es direkt.

---

## Drei Fallen beim `.x`

1. **Jeder Puffer hat seine eigene Adresse** im Geo-Deskriptor. Offsets aus
   den Größen hochzurechnen geht nur, solange keine optionalen Puffer
   vorkommen — `156_skin10.x` hat bei 14 von 21 Meshes einen zusätzlichen
   Puffer und bei sieben nicht.
2. **Die Blockgröße je Bone ist nicht konstant**: 28 Byte/Frame (Pos + Rot),
   32 (+ Sichtbarkeitsflag), 40 (+ Skalierung) oder 44 (beides).
   `063_skin7.x` passt strukturell zu 835 Frames à 32 *und* 668 à 40 — nur das
   zweite ist echt. Entschieden wird über die Rotationsspur: der falsche
   Kandidat kommt auf 16,5 % Einheitsquaternionen, der richtige auf 100 %.
3. **Das Quaternion ist konjugiert gespeichert.** Bei der Bind-Pose kürzt sich
   das weg, ist also in jedem Test unsichtbar, der nur Frame 0 anschaut. Ohne
   Konjugation streckt sich die schlimmste Kante von `020_skin2.x` während der
   Animation auf das 25,5-fache. Konjugiert wird an genau einer Stelle:
   `XBoneWorld()`.

**Frame 0 ist die Bind-Pose** — es gibt keine separate Bind-Matrix.

---

## Was der Parser aus den Daten meldet

Alle bekannten Eigenheiten der Testdateien kommen als Warnung heraus, und zwar
genau die, die das Addon dokumentiert:

* `263_skin5.model` — 2 nicht invertierbare Bind-Matrizen
* `156_skin10.x` — 1 Bone mit NaN über 37 Frames, 7 Bones mit nicht normierten
  Rotationen über 217 Frames
* `045_skin5_huicheng.x` — keine Clips deklariert, aber 110 von 120 Bones
  bewegen sich → synthetischer Clip `full_timeline`

Dass diese drei unabhängig voneinander wieder auftauchen, ist der beste
Einzelhinweis darauf, dass die Formatabbildung stimmt.

---

## Dateien

```
EG3D Import/
├─ CMakeLists.txt          Werkzeug + Plugin
├─ BAUE_ALLE.bat           Max 2016-2027 in einem Lauf
├─ VERGLEICHE.bat          Gegenprobe in einem Aufruf
├─ INSTALLIERE.bat / DEINSTALLIERE.bat
├─ eg3dimport.def
├─ PLAN.md                 Stufenplan
│                          (die geprüfte Optionskombination für .model
│                           steht als Kommentar bei ImportOptions::upAxisFix)
├─ DEVNOTES.md             Formatnotizen (nachgemessen, nicht abgeschrieben)
├─ CODESTIL.md             die C++-Regeln des Projekts, jeweils begründet
├─ package/Eg3dImport/PackageContents.xml
├─ src/
│  ├─ eg3d_common.*        Vektoren, Cursor, FormatFloat, GBK
│  ├─ eg3d_parallel.*      ParallelFor (nur Parser-Schicht!)
│  ├─ eg3d_inflate.*       zlib/DEFLATE
│  ├─ eg3d_json.*          JSON
│  ├─ eg3d_model.*         .model (EG3D)
│  ├─ eg3d_x.*             .x (JUMPX)
│  └─ eg3d_dump.*          Dumpausgabe
└─ tools/
   ├─ eg3ddump_main.cpp    Werkzeug ohne Max
   ├─ pydump.py            Python-Referenz
   ├─ PRUEFE_INCLUDES.py   Symbol -> Header
   ├─ PRUEFE_SDK.py        SDK-Fallen (greift ab Stufe 1)
   └─ PRUEFE_API.py        Interface-Deskriptor (greift ab Stufe 1)
```

Siehe `PLAN.md` für den Stufenplan und `DEVNOTES.md` für die Formatnotizen.
