#!/usr/bin/env python3
"""Prueft, dass der Parser unabhaengig vom Locale dasselbe liefert.

    python3 tools/PRUEFE_LOCALE.py [ordner-mit-modellen]

DER FEHLER, GEGEN DEN DAS HIER STEHT

`strtod` liest im Locale des PROZESSES. Auf einem deutschen Windows ist
der Dezimaltrenner ein Komma - `strtod("0.5")` liest dort die 0, bleibt
am Punkt stehen und MELDET ERFOLG. Rueckgabe 0.0, `end` zeigt hinter die
Ziffer, also schlaegt auch die uebliche Fehlerpruefung nicht an.

Im Feld sah das so aus: der Dump aus dem Plugin zeigte in 58 von 192
`matvec`-Zeilen Nullen, wo dasselbe Werkzeug unter Linux echte Farben
las. Betroffen war ausschliesslich, was einen Dezimalpunkt hat.
Ganzzahlen kamen durch, Vertexdaten sowieso - die werden binaer gelesen.
Ein Fehler, der sich in genau den Zeilen versteckt, die niemand
nachrechnet.

WARUM ES NIE AUFFIEL

Ein C-Programm startet IMMER im C-Locale, egal was in der Umgebung
steht. `LC_ALL=de_DE` zu setzen aendert nichts, solange niemand
`setlocale()` ruft. 3ds Max ruft es (die CRT-Initialisierung tut es),
`eg3ddump.exe` nicht.

Das Werkzeug lief also immer im C-Locale und das Plugin im deutschen -
die Gegenprobe gegen Python konnte den Fehler grundsaetzlich nicht
finden. Deshalb hat `eg3ddump` jetzt `--locale`, und deshalb gibt es
dieses Skript.

Rueckgabe 0 = locale-unabhaengig, 1 = Fund.
"""

import argparse
import os
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)

# Kandidaten fuer das Werkzeug, in der Reihenfolge der
# Wahrscheinlichkeit.
KANDIDATEN = [
    os.path.join(WURZEL, 'output', 'tools', 'eg3ddump.exe'),
    os.path.join(WURZEL, 'output', 'tools', 'eg3ddump'),
    os.path.join(WURZEL, 'build_tools', 'eg3ddump'),
]

# Locales mit Komma als Dezimaltrenner. Welche davon vorhanden ist,
# haengt vom System ab - geprueft wird jede, die sich setzen laesst.
KOMMA_LOCALES = [
    'de_DE.UTF-8', 'de_DE.utf8', 'de_DE',
    'German_Germany.1252', 'German',
    'fr_FR.UTF-8', 'French_France.1252',
    'ru_RU.UTF-8', 'pt_BR.UTF-8',
]


def finde_werkzeug():
    for pfad in KANDIDATEN:
        if os.path.isfile(pfad):
            return pfad
    return None


def dump(werkzeug, datei, ziel, locale=None):
    befehl = [werkzeug, '--all']
    if locale:
        befehl += ['--locale', locale]
    befehl += [datei, '-o', ziel]
    lauf = subprocess.run(befehl, capture_output=True, text=True)
    return lauf.returncode, (lauf.stdout + lauf.stderr)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ordner', nargs='?',
                        help='Ordner mit .model/.x (sonst Suche im Projekt)')
    args = parser.parse_args(argv[1:])

    werkzeug = finde_werkzeug()
    if werkzeug is None:
        print('eg3ddump nicht gefunden - bitte zuerst BAUE_ALLE.bat.')
        print('Gesucht in:')
        for pfad in KANDIDATEN:
            print('  %s' % pfad)
        return 0

    quelle = args.ordner
    if not quelle or not os.path.isdir(quelle):
        print('Kein Ordner mit Testdateien angegeben.')
        print('Aufruf: python3 tools/PRUEFE_LOCALE.py <ordner-mit-modellen>')
        return 0

    dateien = [os.path.join(quelle, n) for n in sorted(os.listdir(quelle))
               if n.lower().endswith(('.model', '.x'))]
    if not dateien:
        print('Keine .model/.x in %s' % quelle)
        return 0

    # Welche Komma-Locale hat dieses System?
    verfuegbar = []
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, 'probe.txt')
        for name in KOMMA_LOCALES:
            rc, _ = dump(werkzeug, dateien[0], probe, name)
            if rc == 0:
                verfuegbar.append(name)

    if not verfuegbar:
        print('Keine Komma-Locale auf diesem System verfuegbar - '
              'Pruefung nicht moeglich.')
        print('Unter Linux: locale-gen de_DE.UTF-8')
        print('Unter Windows sollte "German_Germany.1252" gehen.')
        return 0

    locale = verfuegbar[0]
    print('Werkzeug: %s' % werkzeug)
    print('Locale:   %s (Komma als Dezimaltrenner)' % locale)
    print('Dateien:  %d' % len(dateien))
    print()

    abweichend = []
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, 'c.txt')
        b = os.path.join(tmp, 'komma.txt')

        for datei in dateien:
            rc1, meldung1 = dump(werkzeug, datei, a)
            rc2, meldung2 = dump(werkzeug, datei, b, locale)

            if rc1 != 0 or rc2 != 0:
                abweichend.append('%s: Werkzeug meldet Fehler\n    %s%s'
                                  % (os.path.basename(datei),
                                     meldung1.strip(), meldung2.strip()))
                continue

            with open(a, 'rb') as f1, open(b, 'rb') as f2:
                if f1.read() != f2.read():
                    abweichend.append(
                        '%s: Ausgabe unterscheidet sich zwischen C-Locale '
                        'und %s' % (os.path.basename(datei), locale))

    if abweichend:
        for eintrag in abweichend:
            print('  %s' % eintrag)
        print()
        print('%d Datei(en) locale-abhaengig. Irgendwo im Parser steht eine '
              'Funktion, die das Prozess-Locale benutzt - strtod, sscanf, '
              'atof, iostreams ohne imbue.' % len(abweichend))
        return 1

    print('Alle %d Datei(en): Ausgabe identisch unter C-Locale und %s.'
          % (len(dateien), locale))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
