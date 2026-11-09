# EG3D Import — 300 Heroes (300英雄) für 3ds Max 2016–2027

C++-Importer für beide Modellformate des Spiels, nach dem Vorbild von
[[xfbin-max-importer]] und [[baf-max-importer]] aufgebaut.

| Format | Engine | Erkennung |
|---|---|---|
| `.model` | neuer (`EG3D` / `ezgame`) | Magic `EG3D` |
| `.x` | älter (`JUMPX V5.01`) | Magic `JUMPX` |

Erkannt wird an der **Signatur**, nicht an der Endung — die `.x`-Dateien sind
trotz Endung keine DirectX-X-Files.

Grundlage ist das Blender-Addon `io_scene_eg3d` (heute 2.9.0, Repo
`io_scene_eg3d-1.14.0-300-Hero-importer-`). Das Addon ist die Quelle des
Formatwissens; der Maßstab ist es nicht (siehe „Gegenprobe“).

---

## Stand 0.4.0

Parser, Max-Schicht und Paket für Max 2016–2027. Geprüft in Max 2027 an
102 Retail-Dateien (48 `.model`, 54 `.x`, davon 49 aus dem Monster-Bestand):
alle importieren ohne Fehler.

**Neu in 0.4.0**

* **Bind-Pose und Animation in einem Raum.** Die inversen Bindmatrizen sind
  relativ zum *Mesh-Knoten* gespeichert, und jeder Mesh-Knoten trägt die
  Drehung Z-up → Y-up, oft auch einen eigenen Maßstab (039_skin8: 0,59,
  234_skin10_huicheng: 21,35, 289_96: 6,54). Bis 0.3.12 stand die Figur in
  der Bind-Pose aufrecht und lag in der Animation auf dem Rücken, bei
  skalierten Mesh-Knoten mit Größensprung. Jetzt: Bone = `inv(IBM) · MeshWelt`,
  das Mesh-Objekt steht auf `MeshWelt`, und für die ganze Szene gilt genau eine
  Achsendrehung Engine → Max. Nachgerechnet: Bind-Pose aufs 10⁻⁶ genau,
  Animation gleich `v · IBM · Welt` wie in der Engine.
* **Die Bind-Pose bleibt erhalten.** Jeder Knoten bekommt sie als Max-Skin-Pose;
  zurückholen mit *Animation → Assume Skin Pose*. Mit Ruheframes vor der
  Animation (*Rest frames before animation*) steht sie als echter Key bei
  Frame 0. Bis 0.3.12 zeigte Frame 0 dort in 96 von 97 Dateien schon den
  ersten Clipframe: der Tausch auf lineare Controller hatte die Bind-Pose
  gelöscht. Geprüft an allen 97 gehäuteten Dateien: gehäutetes Mesh bei
  Frame 0 = ungehäutetes Mesh.
* **Layer je Figur.** Das Plugin bestimmt die Figuren aus Paletten und
  Mesh-Knoten (`eg3d_model` an jedem Objekt). 039_skin8.model ergibt
  „039_skin8 Bones/Mesh“ und „144_skin10 Bones/Mesh“; gehäutete Meshes
  bekommen keine eigenen Layer mehr.
* **Keys auf dem Frameraster.** Kanäle mit Keys zwischen den Frames
  (16, 25, 41 ms …) überschrieben sich beim Runden gegenseitig; Gelenke standen
  bis zu 9° falsch. Jetzt wird jeder Kanal genau an den Frames abgetastet.
* **Monster-Animationen vollständig.** Acht `.x`-Dateien (u. a.
  monster_youlong, jiutoulong) kamen mit einem Drittel der Frames an, fast
  jeder Clip schrumpfte auf einen Frame.
* Gepackte int8-Normalen werden normiert, gepackte Positionen in double
  gerechnet, abgeschnittene chinesische Namen ohne „?“.
* Figuren schauen nach −Y (Max-Konvention, wie die `.x`-Figuren).

**Gegenprobe gegen das Blender-Addon:** `tools/vergleiche_blender.py`
vergleicht den Volldump von `eg3ddump.exe` numerisch mit den Arrays des
Blender-Parsers – jeder Vertex, jedes Dreieck, jede Bindmatrix, jeder Knoten,
jeder Animationskey. Stand 0.4.0: 101 von 102 Dateien deckungsgleich; die
eine Abweichung ist „kein Material“ (Max `-1`, Blender `0xFFFFFFFF`).

```
python tools\vergleiche_blender.py output\tools\eg3ddump.exe <addon-ordner> <modellordner> -o bericht.txt
```

Die ältere Python-Referenz `tools/pydump.py` kennt die Monster-Varianten der
`.x`-Dateien nicht und ist nur noch für die Charakterdateien aussagekräftig.

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

Für Anwender: `Eg3dImport-<version>-Setup.exe` oder die ZIP mit `Install.bat`
von der Release-Seite (Ziel `%ProgramData%\Autodesk\ApplicationPlugins`).

Für Entwickler, direkt aus `output\`:

```
INSTALLIERE.bat          nach %APPDATA%\Autodesk\ApplicationPlugins\Eg3dImport
DEINSTALLIERE.bat
```

Danach in Max: **Datei → Importieren → 300 Heroes (EG3D)**.

## Release bauen

```
BAUE_ALLE.bat                    alle zwölf Jahrgänge
installer\BAUE_RELEASE.bat       dist\: Setup.exe, ZIP, SHA256SUMS.txt
```

Die Version steht an genau zwei Stellen: `src/eg3d_version.h` und
`package/Eg3dImport/PackageContents.xml`. `tools/PRUEFE_PAKET.py` vergleicht
beide, `BAUE_RELEASE.bat` prüft die Dateiversion jeder `.dlu`.

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
