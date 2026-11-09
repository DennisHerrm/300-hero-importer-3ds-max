# PLAN — Eg3dImport für 3ds Max

Vorgehen wie bei XFBIN Import und BAF Toolkit: erst der Parser mit
Gegenprobe, dann das SDK. Der Grund ist banal — ein Formatfehler fällt im
Werkzeug in Sekunden auf, im Plugin erst nach zwölf SDK-Builds und einem
Max-Neustart.

---

## Stufe 0 — Parser, Werkzeug, Gegenprobe · **fertig**

Nichts in `src/` außer `eg3dimport*.cpp` bindet je einen Max-Header ein.
Das ist die ganze Idee.

- [x] `eg3d_common` — Vektoren, Quat, Matrix44 (Zeilenvektor),
      Gauss-Jordan-Invertierung, geprüfter `Cursor`, GBK→UTF-8,
      `FormatFloat` mit eigener Tie-Regel
- [x] `eg3d_inflate` — eigener DEFLATE, Adler-32, meldet `consumed`
      für den zweiten Strom
- [x] `eg3d_json` — eigener Parser, Knotenpool, `raw_decode`-Semantik,
      `null` ≠ `0`, Tiefenlimit 64
- [x] `eg3d_parallel` — `ParallelFor`, deterministisch, Ausnahmen über
      die Threadgrenze gerettet
- [x] `eg3d_model` — kompletter `.model`-Parser inklusive
      `VerifyLayout()`
- [x] `eg3d_x` — kompletter `.x`-Parser inklusive `SolveFrames()` und
      `SanitiseTracks()`
- [x] `eg3d_dump` — Dumpausgabe, läuft später auch aus Max heraus
- [x] `tools/eg3ddump_main.cpp`, `tools/pydump.py`

**Abnahme bestanden:**

| Prüfung | Ergebnis |
|---|---|
| Inflate gegen Pythons zlib | 10/10 Blöcke byteweise gleich |
| `eg3ddump --all` über alle 53 Dateien | Exit 0, kein Parse-Fehler |
| C++ gegen Python, `--all`, alle Dateien | **6.025.631 Zeilen, 0 Abweichungen** |
| dekodierte Kanäle | 124.001 von 124.001 (`decoded=0` kommt nicht mehr vor) |
| `PRUEFE_INCLUDES.py` | sauber |
| `PRUEFE_SDK.py` | sauber |

Die drei bekannten Auffälligkeiten (`263_skin5.model` singuläre
Bindmatrizen, `156_skin10.x` NaN und nicht normierte Quaternionen,
`045_skin5_huicheng.x` ohne Act-Tabelle) reproduzieren sich in beiden
Werkzeugen identisch. Das ist das Ziel: nicht „keine Warnungen", sondern
„dieselben Warnungen".

---

## Stufe 1 — Max-Schicht · **geschrieben, nicht am echten SDK erprobt**

- [x] `src/eg3dimport.h` / `.cpp` — `FPStaticInterface` mit 15
      Funktionen, `SceneBuilder`, drei RAII-Guards, `Msg`
- [x] `src/eg3dimport_dll.cpp` — `DllMain`, ClassDescs für Utility und
      `SceneImport`, `Lib*`, eingebettete Oberfläche als RCDATA
- [x] `scripts/Eg3dImport.mcr` — Dialog mit Optionen
- [x] `src/eg3dimport.rc`, `.def` um `LibInitialize`/`LibShutdown`
      ergänzt
- [x] Knoten, Meshes (UV0/UV1/Vertexfarben), Skin über
      `ISkinImportData`, Materialien, Animation, Note Track,
      Sichtbarkeitsspuren — für **beide** Formate

**Was geprüft ist:**

| Prüfung | Ergebnis |
|---|---|
| `PRUEFE_PLUGIN.py` gegen den SDK-Stub | 2016 / 2021 / 2022 / 2027 sauber |
| `PRUEFE_API.py` (enum / MAP / Deskriptor) | 15 / 15 / 15, stimmig |
| `PRUEFE_INCLUDES.py` | jedes SDK-Symbol hat seinen Header |
| `PRUEFE_SDK.py` | keine bekannte Falle |
| `PRUEFE_STIL.py` (cppcheck) | keine Funde |

**Was NICHT geprüft ist:** alles Laufzeitverhalten. Der Stub sagt, dass
es übersetzt und die Signaturen stimmen — nicht, dass Max die Aufrufe in
der erwarteten Reihenfolge macht oder das Ergebnis richtig aussieht. Der
erste echte Build und ein Import gegen eine Retail-Datei stehen aus.

## Stufe 1b — erster echter Build

- [ ] `BAUE_ALLE.bat` mit installiertem SDK, alle vorhandenen Jahrgänge
- [ ] `INSTALLIERE.bat`, Max starten, Plugin Manager prüfen
- [ ] File → Import: taucht "300 Heroes Modell" in der Liste auf?
- [ ] Eine `.model` und eine `.x` importieren, Ergebnis ansehen

Fallen, die bei den Vorgängerprojekten Zeit gekostet haben und hier von
`PRUEFE_SDK.py` abgefangen werden:

- `ClassDesc2::Create()` muss ein `new` liefern. Ein statisches Objekt
  geht bei `UtilityObj` gut und bei `SceneImport` nicht.
- `NonLocalizedClassName()` erst ab `MAX_RELEASE >= 24000`
- `#include <iparamb2.h>` wird für 2016 gebraucht
- `/Zp8`, `/MD`, x64, `.def`-Datei — alle vier, nicht drei davon

Danach: `BAUE_ALLE.bat` einmal komplett, damit die zwölf Jahrgänge
durchgehen, **bevor** Geometrie dazukommt.

## Stufe 2 — nachbessern, was der erste Import zeigt

Erst wenn ein echter Import gelaufen ist, lohnt sich das hier:

- [ ] `ignoreBoneScale` am Skin-Modifier setzen (Parameterblock-Index
      0x0E, wie bei XFBIN) — braucht das echte SDK zum Prüfen
- [ ] Materialien nach Typ trennen (character / avmesh / trail /
      ezParticle) statt einheitlich StdMat
- [ ] `_MaskTex`, `_DissolveTex`, `_SpecTex` — bisher unzugeordnet,
      weil es in einem StdMat keine ehrliche Entsprechung gibt
- [ ] `material._Color`-Kanäle auf den Materialparameter statt als
      Benutzereigenschaft am Knoten
- [ ] Gruppen ohne Node-Referenz (ezParticle-Vorlagen) überspringen —
      Option ist da, greift aber noch nicht

## Stufe 3 — Paket

- [ ] `package/Eg3dImport/PackageContents.xml` — je ein
      Components-Block 2016–2027, **eigener UpgradeCode**,
      `<CompanyDetails>` ist Pflichtfeld
- [ ] `PRUEFE_PAKET.py`
- [ ] `INSTALLIERE.bat` / `DEINSTALLIERE.bat` gegen eine echte
      Max-Installation

---

## Was in dieser Reihenfolge nicht vertauschbar ist

Stufe 4 vor Stufe 2 wäre verlockend, weil die `.x`-Animation der
interessantere Teil ist. Sie ist aber auch der Teil, dessen Fehler sich
in der Bind-Pose wegkürzen — sichtbar wird die konjugierte Quaternion
erst an einem Modell, das schon steht. Ohne Stufe 2 sucht man den Fehler
in der falschen Datei.
