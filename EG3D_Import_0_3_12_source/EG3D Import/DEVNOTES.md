# DEVNOTES — Formatnotizen EG3D / JUMPX

Alles hier ist an den 53 Retail-Dateien aus `41.zip` nachgemessen, nicht
aus dem Addon abgeschrieben. Wo Messung und Addon-README auseinandergehen,
steht beides da, mit der Zahl der Vorkommen.

Testmenge: 48 gültige `.model`, 5 `.x`, dazu `020_skin3.model` — eine
1-Byte-Attrappe ohne Magic. Beide Dumper überspringen sie, das ist so
gewollt und nicht als Fehler zu deuten.

---

## 1. Der Off-by-12 — die einzige wirklich wichtige Zeile

```
kBlockBase = 12
```

Kopf ist `EG3D` + `u32 version` (immer 4) + `u32 mainSize`, danach kommt
der Binärblock. **Alle Offsets im JSON zählen ab Byte 12**, nicht ab
Dateianfang.

Wer das um 12 daneben liest, bekommt nicht etwa Müll, sondern etwas,
das *fast* funktioniert. Genau daran erkennt man den Fehler wieder:

- Löcher in Meshes und Brücken-Dreiecke quer durchs Modell
- Quaternionlängen um 0,14 statt 1,0
- Skin-Gewichte, die sich nicht zu 1 summieren
- ein „Müllkopf" von ein paar Bytes vor jedem Indexpuffer

Das sind keine fünf Probleme. Das ist eines.

`VerifyLayout()` in `eg3d_model.cpp` prüft deshalb nicht nur, ob jeder
Puffer in die Datei passt, sondern auch, ob die Puffer den Binärblock
lückenlos überdecken. Eine Lücke von genau 12 Byte am Anfang wäre die
Warnung, auf die es ankommt.

---

## 2. `.model` — Metasektionen

| # | Inhalt |
|---|---|
| 0 | Header |
| 1 | Scene-Root |
| 2 | Materialien |
| 3 | Geometrie |
| 4 | Paletten + inverse Bindmatrizen |
| 5 | Nodes |
| 6 | Texturen |
| 7 | Clips |

**meta[5] ist nicht die Bind-Pose.** Es ist Frame 0 des autoPlay-Clips.
Wer es als Bind-Pose nimmt, bekommt ein Modell, das in der Ruhestellung
schon halb in einer Animation steht. Die Bind-Pose steht ausschließlich
in den inversen Bindmatrizen der Palette (meta[4]).

### Attribut-Deskriptoren

Deskriptoren sind **6 oder 8 Felder** lang; 8 heißt „mit scale/bias".
Belegung über die ganze Testmenge:

| Index | Bedeutung | type | comps | Anmerkung |
|---|---|---|---|---|
| 0 | POSITION | 2 | 3 | quantisiert |
| 1 | NORMAL   | 3 | 3 | **Stride 4** — zwei int16, siehe unten |
| 2 | TANGENT  | 4 | 4 | |
| 3 | COLOR    | 1 | 4 | normalisiert |
| 4 | BONES    | 0 | 4 | |
| 5 | WEIGHTS  | 4 | 4 | |
| 6 | UV0      | 2 | 2 | quantisiert |
| 7 | UV1      | 2 | 2 | quantisiert, **14 Vorkommen** |

`desc[2]` ist überall 5 — außer bei Bone-Indices, da 2 (252 Treffer).

### Normalen sind Höhe + Azimut, keine Vektoren

Attribut 1 hat drei Komponenten im Deskriptor, belegt aber nur vier
Byte. Es sind zwei int16:

```
z    = c0 / 16384
phi  = c1 / 32768 * pi
r    = sqrt(max(0, 1 - z*z))
n    = ( -r*sin(phi), -z, -r*cos(phi) )
```

Die drei Vorzeichen sind nicht geraten, sie fallen aus dem Vergleich mit
den Tangenten. Wer sie vertauscht, bekommt ein Modell, das nur bei
frontaler Beleuchtung richtig aussieht.

### Der Monster-Bestand: drei Unterschiede zur Charaktervariante

Von 51 `.x`-Dateien scheiterten 21 — alle aus `monster.zip`. Das
Blender-Addon (2.7.0) liest sie; die Unterschiede, aus `eg3d_x.py`:

**1. Die Blockadresse steht bei +148, nicht bei +144.** Die ganze
Tracktabelle ist um vier Bytes verschoben. *Behoben:* der Offset wird
gesucht statt angenommen — genommen wird der erste, der für **jeden**
Bone eine gültige Adresse ergibt.

**2. Es gibt ZWEI Trackadressen je Bone** — Position, und 12 Bytes
später im Deskriptor die Rotation. Ihre Differenz gibt die Framezahl
direkt:

```
A2 - A1 = frames * 12   ->  float32-Positionen
A2 - A1 = frames *  4   ->  Positionen 10:10:10 in eine Box gepackt
```

Das ersetzt das Raten der Framezahl aus den Blockgrößen, das bei diesen
Dateien gar nicht funktionieren kann, weil die Blöcke dort nicht
lückenlos liegen.

**3. Die komprimierte Variante.** Positionen 10:10:10 (4 Bytes je Key),
Rotationen als Axis-Angle (8 Bytes je Key: 8-Bit-Winkel in Grad plus
drei 12-Bit-Achsenkomponenten mit Vorzeichen). Optional folgt eine
float32x3-Skalierungsspur, macht 24 Bytes je Frame —
`tx_030_shenmujiaren_01.x` ist so gebaut.

**4. Die Rotationsvariante muss GEMESSEN werden.** Aus den Blockgrößen
lässt sie sich nicht schließen: die Blöcke liegen bei diesen Dateien
nicht lückenlos, `rotBytes` ergibt sich dann aus dem Abstand zum
nächsten Block statt aus der tatsächlichen Spur. Bei 11 der 21 Dateien
war die Ableitung falsch — Quaternionen der Länge **0.02 statt 1.00**,
also float32 gelesen, wo Axis-Angle steht.

Ein Quaternion hat Länge 1. Also wird eine Stichprobe in **beiden**
Deutungen gelesen und die genommen, deren Länge stimmt. Genau das macht
`_unit_quaternion_score` im Addon.

**5. Ältere Mesh-Anordnung (23 TOC-Einträge statt 25).** Bei +36 steht
keine Positionsadresse, dafür eine bei +40. Die Positionen sind dann auf
**einen uint32 je Vertex** quantisiert — 10:10:10 als Anteil in einer
Box, deren Ecken im Deskriptor bei +96 (hi) und +108 (lo) stehen. Die
Normalen liegen als **int8x3 in drei Bytes** statt als float32x3 in
zwölf.

`068.x` und `083_b.x` sind so gebaut. Ohne diesen Zweig meldet der
Parser „keine brauchbare Positionsadresse", liest die Datei erfolgreich
und liefert sie **ohne Mesh** aus — der Fehler, der beim Import als
leere Szene ankommt.

Nachgemessen an `068.x`: 3021 Vertices, 4174 Dreiecke, Box
X −23.7…38.9, Y −42.6…20.1, Z −0.2…91.0, und **3021 von 3021 Normalen
mit Länge 1.0**.

**6. Clips dürfen über die Zeitleiste hinausreichen — das ist ein
Fehler in der Datei, kein Dekodierfehler.** `monster_youlong.x` hat 53
Frames und einen `dead`-Clip bis 156. Der Parser warnte nur; der
Importer legte dann Keys für Frames an, zu denen es keine Daten gibt,
und die Animation kam zerrissen an.

Das Addon hält es ausdrücklich fest: *"That is an authoring error in the
file, not a decoding problem, so clamp instead of refusing."*

Also klemmen statt ablehnen. Betrifft 12 der 53 Dateien.

**Falsche Fährte, die ich zuerst verfolgt habe:** aus „Clips reichen bis
156, Datei hat 53 Frames" schloss ich, die *Framezahl* sei falsch, und
bestimmte sie aus den Clips neu. Das war die falsche Richtung — die
Framezahl stimmt, die Clipgrenzen nicht. Der Kommentar im Addon hat es
geklärt.

**Stand: alle sechs Punkte übernommen.**

| | vorher | jetzt |
|---|---|---|
| `.x` gelesen | 30 / 51 | **51 / 51** |
| `.model` gelesen | 51 / 51 | 51 / 51 |
| Quaternionen plausibel | — | **51 / 51** |
| bisher lesbare Dateien | — | **30 / 30 bit-identisch** |

### Keyzeiten sind gerundete Millisekunden, gemeint sind Frames

Die Deltas sind 33, 66, 100 — nicht 33.333, 66.667, 100. Direkt in
Ticks umgerechnet ergibt 33 ms bei 30 fps **0.99 Frames statt 1.0**, und
der Fehler summiert sich: nach hundert Keys ist ein ganzer Frame
Versatz aufgelaufen. Sichtbar wird das als Keys zwischen den Frames.

Deshalb rechnet `MsToTicks()` erst in Frames um, rundet dort, und
multipliziert dann mit `GetTicksPerFrame()`. Die Millisekunden sind nur
die Schreibweise; gemeint sind Frames.

### Props werden über die Skalierung versteckt, nicht über ein Flag

`336_skin1.model`: die beiden Schwert-Bones `Bone001` und `Bone002`
stehen in **allen 31 Clips** auf Skalierung 0.001. Damit fällt die Waffe
zusammen und alles, was daran hängt — in Max ist sie zu klein zum
Anklicken.

Nachgezählt an der Datei:

| Bone | Keys mit Skalierung ≤ 0.01 |
|---|---|
| `Bip001 R Finger1Nub` | 795 |
| `Bip001 R Finger0Nub` | 765 |
| `Bip001 L Toe0Nub` | 720 |
| `Bone001` | 30 |
| `Bone002` | 30 |

Die Nub-Bones sind Endstücke ohne Geometrie — dort ist es harmlos. Bei
den Waffen-Bones ist es die Versteckmechanik des Spiels.

`ignoreZeroScale` zwingt solche Keys auf 1. Das **weicht bewusst von der
Datei ab**, deshalb Standard aus und eine Meldung im Protokoll, wie
viele Werte angehoben wurden. Dieselbe Option gibt es im Blender-Addon
ab 2.8.0.

### Kanäle haben NICHT dieselben Keyzeiten - Sampling muss interpolieren

`157_skin11.model`, Clip 0:

| | |
|---|---|
| Kanäle | 390 |
| Keys je Kanal | 1, 5, 7, 8, 11, 12, 14, 15, … |
| vereinigte Zeitpunkte | 244 |
| Kanäle mit eigenem Key je Zeitpunkt | Median **9 von 390** |

`SampleChannel` lieferte den **letzten Key ≤ t** — konstante
Interpolation. Solange nur an den eigenen Keyzeiten eines Kanals
gesampelt wurde, fiel das nicht auf: dort trifft man immer genau einen
Key.

Seit der Animationspfad an den **vereinigten** Keyzeiten sampelt, ist es
der Hauptfehler: an jedem Zeitpunkt bekommen im Schnitt 381 von 390
Kanälen den alten Wert eingefroren und springen erst beim nächsten
eigenen Key weiter. Treppenstufen — genau das sichtbare Stottern.

Bei `.x` tritt es nicht auf, weil dort jeder Bone in jedem Frame einen
Wert hat.

Jetzt wird linear gemischt, Rotationen über den kürzeren Weg
(Vorzeichen angleichen) und wieder normiert.

**Falle beim Einbauen:** die drei Kanäle brauchen je einen eigenen
Zwischenspeicher. Mit einem gemeinsamen überschreibt der zweite Aufruf
das Ergebnis des ersten — der zurückgegebene Zeiger zeigt dorthin, und
gelesen wird er erst danach.

### Die Elternmatrix darf nicht aus der Szene kommen

`.x` lief sauber, `.model` zitterte — und der Unterschied war eine
Zeile:

```cpp
.x     : SetNodeWorld(nodes_[i], world);        // direkt aus den Daten
.model : tm = tm * parent->GetNodeTM(t);        // aus der SZENE
```

An einem Zeitpunkt, an dem der Elternteil keinen Key hat, liefert
`GetNodeTM(t)` einen von Max **interpolierten** Wert. Dieser Fehler
schaukelt sich über die Hierarchie auf: bei einem Fingerknochen, sieben
Ebenen unter dem Becken, bleibt davon nichts als Zittern.

Deshalb rechnet der `.model`-Zweig die Weltmatrizen jetzt genauso wie
der `.x`-Zweig — selbst, aus den Kanaldaten. Dazu werden **alle
Keyzeiten des Clips vereinigt**, außen über die Zeit gelaufen und innen
über die Knoten (Eltern vor Kindern). Dann ist die Elternmatrix immer
die zum selben Zeitpunkt, aus denselben Daten.

Das macht auch das Retargeting gegenstandslos — es gibt keinen Ort
mehr, an dem eine Korrekturmatrix greifen könnte.

### Max legt TCB-Controller an, und die überschwingen

Beim Setzen von Keys über `SetNodeTM` entstehen standardmäßig
TCB-Controller. Die Autodesk-Doku dazu:

> *"**High Continuity values create curved overshoot on both sides of
> the key.** Low Continuity values create a linear animation curve."*

Continuity steht standardmäßig auf 25 — also Überschwingen zwischen
jedem Keypaar. Bei **gebackenen** Spieldaten, wo ohnehin jeder Frame
einen Key hat, ist das falsch: die Kurve schwingt zwischen Werten, die
schon stimmen. Sichtbar als Zittern und Nachschwingen.

Das Blender-Addon für dieselben Daten setzt aus demselben Grund
`LINEAR` als Standard: *"No overshooting between the animations,
recommended for baked game data."*

Der Importer weist deshalb vor dem ersten Key lineare Controller zu
(`LININTERP_POSITION/ROTATION/SCALE_CLASS_ID`). **Vorher** — ein
Controllerwechsel nach dem Setzen wirft die Keys weg.

### Keyabstand ist ein Frame - nicht 33 ms bei 30 fps

Die Keys stehen 33 ms auseinander. Bei 30 fps sind das **0.99 Frames**,
und Runden verschluckt jeden hundertsten Key:

```
Key 50 -> 49.50 -> Frame 50
Key 51 -> 50.49 -> Frame 50   (derselbe!)
Key 52 -> 51.48 -> Frame 51
```

Zwei Keys auf einem Frame heißt: einer geht verloren, die Bewegung
ruckt. Sichtbar als „alle Animationen stottern außer Run" — `bat_run`
ist mit 795 ms nur 24 Keys lang und bleibt unter der ersten Kollision
bei Key 51. Im Sequenzmodus trifft es jeden Clip, der nicht ganz am
Anfang liegt.

Der Importer bestimmt deshalb den **häufigsten** Keyabstand (nicht den
kleinsten — einzelne Kanäle haben Zwischenkeys, und ein Ausreißer würde
die ganze Zeitleiste stauchen) und rechnet in diesem Raster: ein
Keyabstand ist genau ein Frame. Nachgerechnet über 300 Keys: alte
Rechnung 3 Doppelframes, neue 0.

Steht im Protokoll:

```
Keyraster: 33 ms je Frame (390 von 2 Abstaenden) - ein Keyabstand ist ein Frame
```

### Clips haben drei Felder, nicht zwei

`[name, channels, flag]`. Das dritte Feld ist ein bool und in allen 349
Clips der Testmenge `false`. Es wird gelesen und weitergereicht, aber
nichts damit gemacht — solange niemand ein `true` findet, wäre jede
Deutung erfunden.

### Kanal-`kind` ist nicht immer eine Zahl

Neben 0/1/2 (Position/Rotation/Skalierung) stehen dort Strings. Sie
zerfallen in zwei Gruppen, und beide sind dekodiert:

| kind | Vorkommen | vType | comps | Kodierung |
|---|---|---|---|---|
| `active` | 1093 | 6 | 1 | uint8, 0/1 |
| `rendererenable` | 4 | 6 | 1 | uint8, 0/1 |
| `enable` | 2 | 6 | 1 | uint8, 0/1 |
| `material._Color` | 69 | 0 | 4 | float32 RGBA |
| `material._TintColor` | 29 | 0 | 4 | float32 RGBA |
| `material._TilingOffset` | 1 | 0 | 4 | float32 |

**vType 6 ist ein Byte je Komponente, nicht vier.** Das war die letzte
offene Stelle im `.model`-Format. Über den ganzen Bestand nachgemessen:
1245 Keys in 1099 Kanälen, ausschließlich `0x00` (1120×) und `0x01`
(125×), nie ein anderer Wert. Das sind Sichtbarkeitsschalter.

Typischer Verlauf in `039_skin8_huicheng.model`, Clip `idle`: Knoten 220
schaltet bei 6533 ms von 0 auf 1, ein Dutzend anderer Knoten schalten
bei 6966 ms von 1 auf 0 — und 6966 ms ist genau das Clipende (211 Keys
× 33 ms). Ein- und Ausblenden von Waffen, Effekten, Umhängen.

Die vType-0-Gruppe war nie exotisch: dieselbe float32-Kodierung wie
Position und Rotation. Sie wurde nur deshalb nicht gelesen, weil die
Fallunterscheidung am `kind` hing statt am `valueType` — ein Kanal mit
Stringnamen fiel durch. **Die Wertbreite hängt am `valueType`, nie am
`kind`.** Werte liegen im Bereich 0..1 und sind meistens Alpha-Fades
(`[0.5, 0.5, 0.5, 0.0]` → `[0.5, 0.5, 0.5, 1.0]`).

Was ein Kanal *bedeutet*, entscheidet erst die Max-Schicht. Der Parser
behält `kindName`, damit dort ein Sichtbarkeits-Controller oder ein
Materialparameter daraus werden kann.

Ein Kanaldeskriptor kann 8 Felder haben statt der üblichen 7.

### Zeiten sind uint16-Deltas, keine floats

`tType=5` heißt **zwei Byte je Key**, aufsummiert Millisekunden, erster
Eintrag 0. Typische Deltas sind 33, 66, 100 — Vielfache von 1/30 s.

Wer den Zeitpuffer als float32 liest, bekommt Denormale (`1.37e-23`) und
läuft außerdem in den Wertepuffer hinein, weil er doppelt so viel Platz
annimmt wie tatsächlich belegt ist. Zwischen Zeit- und Wertepuffer liegt
ein Padding auf 4-Byte-Ausrichtung: bei 211 Keys sind das 422 Byte Zeit
plus 2 Byte Füllung.

Zeitpuffer werden **geteilt**. In `039_skin8_huicheng.model` hängen fünf
`active`-Kanäle an demselben `tOff`, nur die Wertoffsets laufen einzeln
weiter. Ein Parser, der Puffer nach Kanälen aufteilt statt sie nach
Offset zu adressieren, verrechnet sich hier.

### Nodes

9 oder 10 Felder. `nd[4]` ist überall `null`. `nd[8]` sind
Partikel-Extras, `nd[9]` ist `{castShadow, receiveShadow}`, gelegentlich
mit Material-Override.

**Nicht jede Gruppe wird instanziert.** `039_skin8.model` hat 27
Gruppen mit 27 Submeshes, aber nur 11 Instanzen — die übrigen 16
Gruppen sind ezParticle-Vorlagen, auf die kein Knoten zeigt. Dass beim
Import 11 statt 27 Meshes entstehen, ist also richtig und kein
Datenverlust.

**Die Palette hängt am Knoten (Feld 7), nicht an der Gruppe.** Eine
Gruppe kann mehrfach instanziert sein und dabei je Instanz eine andere
Palette bekommen. Gruppen, auf die kein Node zeigt, sind
ezParticle-Vorlagen und ergeben keine Geometrie.

### Der Bindfaktor gilt JE PALETTE, nicht global

`039_skin8.model` enthält zwei Charaktere, und ihre Bindmatrizen tragen
verschiedene Maßstäbe:

| Palette | Bones | Bindfaktor | Mesh-Skalierung 1/f |
|---|---|---|---|
| 0 (Bip001) | 91 | 0.592375 | 1.6881 |
| 1 | 1 | 0.814752 | 1.2274 |
| 2 (Bip002) | 80 | **1.000000** | 1.0000 |
| 3–6 | je 1 | 1.000000 | 1.0000 |

Ein globaler Median trifft damit genau einen von beiden. Der erste
Charakter kam richtig, der zweite um Faktor 1.688 zu klein.

Der Importer misst den Faktor deshalb je Palette und wendet auf jedes
Mesh den Faktor **seiner** Palette an. Starre Meshes (ohne Palette)
bleiben unskaliert — sie hängen an ihrem Knoten und brauchen keine
Umrechnung.

Die Zahlen stehen im Protokoll:

```
  Palette 0: Bindfaktor 0.592375 (91 Bones)
  Palette 2: Bindfaktor 1 (80 Bones)
```

### Die Bindmatrix enthält die MESH-Transformation

Das ist die Auflösung für alles, was ab 0.1.9 gesucht wurde. Die
Unity-Dokumentation zu `Mesh.bindposes` sagt es wörtlich:

```
bindPoses[0] = bones[0].worldToLocalMatrix * transform.localToWorldMatrix
```

Die Bindmatrix ist also **nicht** `inv(boneWorld)`. Sie enthält
zusätzlich die Weltmatrix des Objekts, an dem der Skin hängt. In
Zeilenvektor-Schreibweise:

```
IBM       = meshWorld · inv(boneWorld)
boneWorld = inv(IBM) · meshWorld
```

Nachgerechnet an `039_skin8.model`, `inv(IBM) · meshWorld` gegen die
Ruhelage aus meta[5]:

| Palette | meshWorld-Skalierung | Abweichung | Bones |
|---|---|---|---|
| 0 | 0.59237 | **0.00006** | 91 |
| 1 | 0.81475 | **0.00000** | 1 |
| 2 | 1.00000 | 6.81 | 80 |

Für Palette 0 und 1 fällt die Bind-Pose damit **mit der Ruhelage
zusammen** — es gibt gar keine zwei Räume. Der Faktor 1.688118 war
schlicht `1 / 0.59237`, die Skalierung des Mesh-Knotens `039_skin8`,
und die gesuchte 90°-Drehung steckte in derselben Matrix.

Das erklärt rückblickend jede Messung:

- Warum die Korrektur **je Palette** konstant war (eine Palette gehört
  zu einem Mesh) und zwischen Paletten verschieden (andere Mesh-Knoten).
- Warum sie **von rechts** konstant war und von links nicht.
- Warum genau 91 von 176 Bones ihr folgten — das ist Palette 0.

Palette 2 (der zweite Charakter, Bip002) bleibt abweichend. Dort ist
die Bind-Pose wirklich eine andere Haltung als meta[5].

### meta[4] und meta[5] sind zwei verschiedene Posen

Nachgemessen an `039_skin8.model`, über alle 176 Bindmatrizen. Für
jeden Bone die Differenz zwischen `inv(IBM)` und der aus meta[5]
akkumulierten Weltmatrix:

| | |
|---|---|
| **Skalierung** | 0.592374 bis 0.592376, Median 0.592376 (einzelne bei 1.0) |
| **Kehrwert** | 1.688118 |
| **Knotendaten-Skalierung** | durchgängig 1.0 |
| **Rotationsanteil** | **nicht konstant** — wechselt je Körperteil das Vorzeichen |

Der Maßstabsfaktor ist also durchgehend und kein Zufall. Der
Rotationsanteil ist es nicht: Becken und Wirbelsäule bekommen
`[0 -1.688 0; 1.688 0 0]`, Ober- und Unterschenkel das Gegenteil.

Diese Messung war **von der falschen Seite gerechnet** und hat zu dem
Fehlschluss geführt, es gäbe keine globale Korrektur. Die Korrektur
wirkt von **rechts**:

```
bindWorld = nodeWorld * C        →    C = inv(nodeWorld) * bindWorld
```

So gerechnet ist sie über alle 176 Bindmatrizen dieselbe:

```
[ 1.68812   0         0      ]
[ 0         0         1.68812]
[ 0        -1.68812   0      ]
T = 0
```

Skalierung 1.68812 mal einer Drehung um **-90° um X** — eine
Y-up-nach-Z-up-Umrechnung. Die Skalierung allein war nur ein Drittel
der Antwort; das war der Grund, warum das Modell nach dem
Skalierungs-Fix immer noch flach lag.

**Und sie gilt JE PALETTE, nicht global.** Über alle Bones gemessen
folgen nur 91 von 176 — und 91 ist genau die Bonezahl der ersten
Palette. Je Palette gemessen:

| Palette | Bones | folgen | Korrektur |
|---|---|---|---|
| 0 | 91 | **91** | 1.6881 · (−90° um X) |
| 2 | 80 | **0** | keine erkennbare Struktur |
| 1, 3–6 | je 1 | trivial | zu wenig zum Messen |

`039_skin8.model` enthält **zwei Charaktere** (Bip001 und Bip002).
Beim ersten ist die Differenz zwischen Bindraum und Knotenraum eine
saubere Y-up-nach-Z-up-Umrechnung mit Maßstab. Beim zweiten nicht — dort
sind es wirklich zwei verschiedene Körperhaltungen, und keine Matrix der
Welt gleicht das aus.

Der Importer misst deshalb je Palette und übernimmt eine Korrektur nur,
wenn mindestens 90 % ihrer Bones ihr folgen und die Palette mindestens
vier hat. Eine Palette mit einem einzigen Bone taugt nicht zum Messen:
dort ist der Median dieser eine Wert, und ob er eine Konvention
beschreibt oder eine Einzellage, lässt sich nicht unterscheiden.

**Wichtig bei der Anwendung:** die Elternkette wird im *Knotenraum*
aufgebaut und `C` erst ganz am Ende auf die Weltmatrix multipliziert.
Andersherum käme die Korrektur je Hierarchieebene erneut hinein und das
Modell zerfiele mit jeder Ebene weiter.

### Bindmatrizen: nicht transponieren

Zeilenvektor-Konvention, dieselbe wie in Max. Das Blender-Addon
transponiert — aber nur, weil Blender Spaltenvektoren führt. In Max wäre
das Transponieren der Fehler.

`263_skin5.model` hat **zwei singuläre Bindmatrizen** in Palette 5.
Deshalb invertiert `MatInvert()` per Gauss-Jordan mit Pivotsuche und
meldet Singularität, statt die adjunkte Formel zu benutzen und stillos
NaN zu liefern.

### Materialtypen

| Typ | Vorkommen |
|---|---|
| ezParticle | 991 |
| character | 147 |
| avmesh | 70 |
| trail | 11 |
| Default | 1 |

`textures{_MainTex,_MaskTex,_DissolveTex,_SpecTex}` sind int-Indices,
`vectors{_TilingOffset,_TintColor,…}` sind je vier floats.

### JSON

Bis 548 KB. **Fünf Dateien haben Bytes hinter dem JSON.** Der Parser
muss deshalb `raw_decode`-Semantik haben: nach dem schließenden Zeichen
aufhören und den Rest ignorieren, statt „trailing garbage" zu melden.

---

## 3. `.x` — JUMPX V5.01

TOC ab `0x58`, danach vier `u32`: roh1, roh2, gepackt1, gepackt2. Dann
zwei zlib-Ströme hintereinander → Block 1 (Deskriptoren), Block 2
(Nutzdaten). Zeigerbasis ist 1.000.000.000, Zeiger sind **unaligned**.

| Tabelle | Stride | wichtige Felder |
|---|---|---|
| Bone | 172 | +8 Name, +144 Blockadresse |
| Geo | 124 | +8 Name, +16 Material, +28/+32 vc/tc, Adressen +36/+44/+52/+68/+76/+92 |
| Act | 90 | |
| Mtl | 48 | |
| Tex | 8 | |

### Die Framezahl steht nirgends

Bytes je Frame und Bone sind einer aus {28, 32, 40, 44}. Welcher, ergibt
sich nur aus der Blockgröße — und die ist mehrdeutig: `063_skin7.x` geht
als 835 Frames à 32 Byte *und* als 668 à 40 auf.

`SolveFrames()` entscheidet über die **Medianabweichung der
Quaternionlängen von 1**, nicht über den Anteil exakt getroffener Werte.
Der Anteil-Ansatz kippt bei Dateien mit vielen stillstehenden Bones, weil
dort in beiden Deutungen dieselben konstanten Werte stehen.

### Es gibt KEINE separate Bind-Pose

Frame 0 ist die Pose, gegen die die Skin-Gewichte gebunden sind — und
gleichzeitig der erste Frame des ersten Clips. In `063_skin7.x` beginnt
`bat_idle` bei Frame 0, und schon zwischen Frame 0 und 1 bewegen sich
die Bones.

Eine T-Pose oder eine sonstige neutrale Referenzstellung steht nirgends
in der Datei. Wer eine ruhige Pose bei Frame 0 haben will, muss die
Animation nach hinten schieben — das macht die Option `restFrames`
(Vorgabe 5). Frame 0 trägt dann die Ruhelage, die Clips fangen dahinter
an. Das ist eine Zutat des Importers, keine Formatinformation.

Die Clips liegen ohnehin nicht lückenlos: `bat_idle` endet bei 45,
`bat_specialidle` beginnt bei 50. Was in den Frames 46–49 steht, ist
nicht deklariert.

### Quaternionen sind konjugiert gespeichert

Und zwar so, dass es sich **in der Bind-Pose wegkürzt**. Frame 0 ist die
Bind-Pose; wer nur die prüft, merkt nichts. Erst in der Animation
zerreißt das Modell. Die Konjugation passiert an genau einer Stelle,
`XBoneWorld()` in `eg3d_x.cpp` — wenn sie an zwei Stellen steht, ist eine
davon falsch.

Der vierte Wert pro Frame ist ein Sichtbarkeitsschalter, die Namenstabelle
trägt die Hierarchie.

### Bekannte Auffälligkeiten in der Testmenge

- `045_skin5_huicheng.x` hat keine Act-Einträge → synthetischer Clip
  `full_timeline`, 110 von 120 Bones bewegen sich darin
- `156_skin10.x`: 7 Bones mit nicht normierten Quaternionen über 217
  Frames, 1 Bone mit NaN über 37 Frames. `SanitiseTracks()` ersetzt NaN
  durch den nächsten gültigen Frame und normiert nach — beides mit
  Warnung, nie stillschweigend.

---

## 4. Warum eigener Inflate und eigener JSON-Parser

Weil die Alternative wäre, zlib und nlohmann gegen **zwölf
Max-Jahrgänge und zwei Toolsets** zu bauen. Das ist mehr Arbeit als die
beiden Module zusammen, und es geht bei jedem neuen Max-Jahrgang von
vorn los.

Der Inflate ist gegen Pythons `zlib` verifiziert: alle fünf `.x`,
beide Blöcke, **10 von 10 FNV-Hashes byteweise gleich**. Beispiel
`156_skin10.x`: 52.418 Byte → `66c3576540ae4d52`, 8.614.048 Byte →
`b5593a4d1075bc7c`.

Der JSON-Parser unterscheidet `null` von `0` (`TryInt()` gibt beides
verschieden zurück) — das ist bei `nd[4]` und bei fehlenden
Materialfeldern der Unterschied zwischen „nicht gesetzt" und „Index 0".

---

## 5. Gleitkomma

`/fp:precise` beziehungsweise `-ffp-contract=off`, und `FormatFloat()`
rechnet die Dezimalausgabe selbst statt über `printf` oder `<charconv>`.

Grund: der Compiler darf `a*b+c` zu einem FMA zusammenziehen. Das ist
schneller *und* genauer, es fällt nur eine Zwischenrundung weg. Trotzdem
unerwünscht — die Dequantisierung (`bias + roh*scale`) und die
Matrizenmultiplikation bestehen aus genau solchen Summen, und ob
kontrahiert wird, hängt vom Toolset ab. Bei BAF Toolkit gemessen:
18 von 21.166 Dumpzeilen ändern sich dadurch. Das sind 18 Zeilen, an
denen nichts falsch ist und deren Suche einen Abend kostet.

Dieselbe Überlegung bei der Tie-Regel beim Runden: die steht im
Quelltext, damit sie in C++ und in Python dieselbe ist, statt in zwei
Laufzeitbibliotheken zu stehen.

---

## 6. Threading

`ParallelFor` gibt es nur in der Parser-Schicht. **Das Max-SDK ist nicht
threadsicher** — kein `INode`, kein `Mesh`, kein `Interface`-Aufruf
außerhalb des Hauptthreads.

Die Aufteilung ist deterministisch: gleiche Eingabe, gleiche Ausgabe,
unabhängig von der Threadzahl. Sonst wäre die Python-Gegenprobe wertlos.
`--threads 1` und `--threads 16` müssen dieselbe Datei ergeben; das ist
Teil der Abnahme.

Ausnahmen werden über die Threadgrenze gerettet und im Aufrufer neu
geworfen, statt `std::terminate` auszulösen.
