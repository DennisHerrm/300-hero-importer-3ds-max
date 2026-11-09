#!/usr/bin/env python3
"""Prueft einen ganzen Dateibestand INHALTLICH, nicht nur auf Lesbarkeit.

    python3 tools/PRUEFE_BESTAND.py <ordner> [--dump pfad/zu/eg3ddump]

WARUM ES DAS GIBT

"Wird gelesen" und "wird richtig gelesen" sind zwei verschiedene Dinge, und
der Unterschied ist in diesem Projekt mehrfach teuer geworden:

  * 068.x wurde erfolgreich gelesen und kam OHNE MESH an - der Parser
    meldete "keine brauchbare Positionsadresse" und machte weiter.
  * 11 Monster-Dateien wurden gelesen, ihre Quaternionen hatten aber die
    Laenge 0.02 statt 1.0 - float32 gelesen, wo Axis-Angle steht.
  * monster_youlong.x wurde gelesen, hatte aber Clips bis Frame 156 bei
    53 Frames Daten.

Alle drei haetten in einer Statistik "51 von 51 gelesen" gestanden.

Geprueft wird deshalb je Datei:

    Mesh        mindestens ein Mesh mit Vertices und Dreiecken
    Vertices    nicht alle im selben Punkt, keine NaN
    Normalen    Laenge 1 (Toleranz 5 %)
    Quaternion  Laenge 1 (Toleranz 2 %)
    UV          im Bereich -2..3 (Kacheln erlaubt, Muell nicht)
    Clips       innerhalb der Zeitleiste
    Gewichte    summieren auf 1 (Toleranz 2 %)

Rueckgabe 0 = alles sauber, 1 = mindestens ein Befund.
"""

import math
import os
import re
import subprocess
import sys


def zahlen(text):
    try:
        return [float(x) for x in text]
    except ValueError:
        return None


def laenge(werte):
    return math.sqrt(sum(x * x for x in werte))


def pruefe(pfad, dump):
    """Gibt eine Liste von Befunden zurueck - leer heisst sauber."""
    ziel = '/tmp/_bestand.txt'
    # Leere oder abgeschnittene Dateien sind kein Parserfehler.
    # 020_skin3.model im Testbestand ist 1 Byte gross.
    if os.path.getsize(pfad) < 64:
        return []

    lauf = subprocess.run([dump, pfad, '-o', ziel, '--all'],
                          capture_output=True, text=True)
    if lauf.returncode != 0:
        erste = (lauf.stdout or lauf.stderr or '').strip().split('\n')[0]
        return ['nicht lesbar: %s' % erste[:80]]

    with open(ziel, encoding='utf-8', errors='replace') as handle:
        text = handle.read()

    befunde = []

    # ---- Meshes ----
    #
    # .x meldet "meshes=", .model "groups=/submeshes=" - beide Formate
    # muessen erkannt werden, sonst meldet die Pruefung "Zaehlzeile
    # fehlt" fuer eine voellig gesunde Datei.
    #
    # Und: eine Datei OHNE Geometrie ist nicht zwangslaeufig kaputt.
    # 149_bat_specialidle_skin13.model hat 17 Knoten, einen Clip und
    # zwei ezParticle-Materialien, aber keine Gruppen - eine reine
    # Effekt- und Animationsdatei. Gemeldet wird das nur, wenn die
    # Datei Geometrie ANKUENDIGT und dann keine liefert.
    treffer = re.search(r'meshes=(\d+).*?verts=(\d+) tris=(\d+)', text)
    if treffer:
        meshes, verts, tris = (int(treffer.group(i)) for i in (1, 2, 3))
        if meshes > 0 and (verts == 0 or tris == 0):
            befunde.append('Mesh angekuendigt, aber ohne Geometrie')
        elif meshes == 0:
            # Nur bemaengeln, wenn die Datei Geometrie deklariert:
            # ngeo > 0 bei .x. Sonst ist es eine Effektdatei.
            ngeo = re.search(r'\bngeo (\d+)', text)
            if ngeo and int(ngeo.group(1)) > 0:
                befunde.append('kein Mesh, obwohl ngeo=%s' % ngeo.group(1))
    else:
        treffer = re.search(r'groups=(\d+) submeshes=(\d+).*?verts=(\d+) tris=(\d+)',
                            text)
        if treffer:
            gruppen, submeshes, verts, tris = (int(treffer.group(i))
                                               for i in (1, 2, 3, 4))
            if submeshes > 0 and (verts == 0 or tris == 0):
                befunde.append('Submeshes angekuendigt, aber ohne Geometrie')
        # Weder das eine noch das andere: dann steht in der Datei
        # nichts ueber Geometrie, und es gibt nichts zu bemaengeln.

    # ---- Vertexpositionen ----
    ps = re.findall(r'\bP (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+)', text)
    punkte = [z for z in (zahlen(p) for p in ps[:2000]) if z]
    if punkte:
        if any(not math.isfinite(v) for p in punkte for v in p):
            befunde.append('Vertices enthalten NaN oder unendlich')
        else:
            spanne = max(max(p[i] for p in punkte) - min(p[i] for p in punkte)
                         for i in range(3))
            if spanne < 1e-4:
                befunde.append('alle Vertices im selben Punkt')

    # ---- Normalen ----
    ns = re.findall(r'\bN (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+)', text)
    normalen = [z for z in (zahlen(n) for n in ns[:2000]) if z]
    if normalen:
        gut = sum(1 for n in normalen if abs(laenge(n) - 1.0) < 0.05)
        anteil = gut / len(normalen)
        if anteil < 0.95:
            befunde.append('nur %.0f %% der Normalen haben Laenge 1' % (100 * anteil))

    # ---- Quaternionen ----
    qs = re.findall(r'\bQ (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+)',
                    text)
    quats = [z for z in (zahlen(q) for q in qs[:2000]) if z]
    if quats:
        mittel = sum(laenge(q) for q in quats) / len(quats)
        if abs(mittel - 1.0) > 0.02:
            befunde.append('Quaternionen im Mittel %.4f lang statt 1.0' % mittel)

    # ---- UVs ----
    uvs = re.findall(r'UV0 (-?[\d.eE+-]+) (-?[\d.eE+-]+)', text)
    punkte_uv = [z for z in (zahlen(u) for u in uvs[:2000]) if z]
    if punkte_uv:
        aus = sum(1 for u in punkte_uv if not (-2 <= u[0] <= 3 and -2 <= u[1] <= 3))
        if aus > len(punkte_uv) * 0.02:
            befunde.append('%d von %d UVs weit ausserhalb 0..1'
                           % (aus, len(punkte_uv)))

    # ---- Clips gegen die Zeitleiste ----
    rahmen = re.search(r'frames=(\d+)', text)
    if rahmen:
        frames = int(rahmen.group(1))
        for name, ende in re.findall(r'name=(\S+).*?end=(\d+)', text):
            if frames and int(ende) >= frames:
                befunde.append("Clip '%s' endet bei %s, Datei hat %d Frames"
                               % (name, ende, frames))
                break

    # ---- Bones gegen Mesh ----
    #
    # Stehen die Bones in einem winzigen Wuerfel, waehrend das Mesh
    # hundert Einheiten gross ist, wurden die Positionen falsch
    # gelesen. Das ist kein Absturz und keine unplausible Zahl - es
    # faellt nur im Viewport auf, wo die Bones zu klein zum Anklicken
    # sind.
    #
    # Statische Objekte mit einem einzigen Bone sind ausgenommen: ein
    # Bone hat naturgemaess keine Spanne.
    knochen = re.search(r'bones=(\d+)', text)
    if knochen and int(knochen.group(1)) >= 2 and punkte:
        keys = [z for z in (zahlen(k) for k in re.findall(
            r'^xkey \d+ 0 P (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+)',
            text, re.M)) if z]
        if keys:
            weit_mesh = max(max(p[i] for p in punkte) - min(p[i] for p in punkte)
                            for i in range(3))
            weit_bone = max(max(k[i] for k in keys) - min(k[i] for k in keys)
                            for i in range(3))
            if weit_mesh > 1e-6 and weit_bone / weit_mesh < 0.05:
                befunde.append('Bones spannen nur %.2f, das Mesh %.2f'
                               % (weit_bone, weit_mesh))

    # ---- Skingewichte ----
    ws = re.findall(r'\bW (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+) (-?[\d.eE+-]+)',
                    text)
    gewichte = [z for z in (zahlen(w) for w in ws[:2000]) if z]
    if gewichte:
        schlecht = sum(1 for w in gewichte if abs(sum(w) - 1.0) > 0.02)
        if schlecht > len(gewichte) * 0.02:
            befunde.append('%d von %d Vertices mit Gewichtssumme != 1'
                           % (schlecht, len(gewichte)))

    return befunde


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    ordner = sys.argv[1]
    dump = 'output/tools/eg3ddump'
    if '--dump' in sys.argv:
        dump = sys.argv[sys.argv.index('--dump') + 1]
    if not os.path.isfile(dump):
        print('eg3ddump nicht gefunden: %s' % dump)
        return 1

    dateien = []
    for wurzel, _, namen in os.walk(ordner):
        for name in namen:
            if name.lower().endswith(('.x', '.model')):
                dateien.append(os.path.join(wurzel, name))
    dateien.sort()

    if not dateien:
        print('keine .x oder .model in %s' % ordner)
        return 1

    schlecht = 0
    for pfad in dateien:
        befunde = pruefe(pfad, dump)
        if befunde:
            schlecht += 1
            print('  %s' % os.path.basename(pfad))
            for eintrag in befunde:
                print('      %s' % eintrag)

    print()
    print('%d Datei(en) geprueft, %d ohne Befund, %d mit Befund.'
          % (len(dateien), len(dateien) - schlecht, schlecht))
    return 1 if schlecht else 0


if __name__ == '__main__':
    sys.exit(main())
