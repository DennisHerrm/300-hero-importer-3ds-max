#!/usr/bin/env python3
"""Statische Analyse des ganzen Projekts mit cppcheck.

    python3 tools/PRUEFE_STIL.py [--roh]

Braucht cppcheck im PATH (Ubuntu: apt install cppcheck, Windows:
https://cppcheck.sourceforge.io). Fehlt es, meldet das Skript das und
gibt 0 zurueck - eine fehlende Werkzeugkette soll keinen Build
aufhalten.

ZU DEN UNTERDRUECKUNGEN

cppcheck analysiert die Plugin-Dateien gegen tools/sdkstub, weil das
echte Max SDK nicht ueberall liegt. Die Attrappen geben Konstanten
zurueck - `CreateInstance` immer nullptr, `GetTempPathW` immer 0. Damit
ist jede Nullpruefung dahinter aus cppchecks Sicht "immer wahr".

Das ist ein Artefakt des Stubs und kein Befund: im echten SDK liefern
diese Funktionen echte Werte, und die Pruefungen sind dort genau
richtig. Deshalb wird `knownConditionTrueFalse` fuer src/eg3dimport*
unterdrueckt - NUR dort, nicht im Parser.

Mit --roh laeuft cppcheck ohne diese Unterdrueckung, falls jemand
nachsehen will, was sie verdeckt.
"""

import argparse
import os
import shutil
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)

UNTERDRUECKT = [
    # Systemheader hat cppcheck nicht, das ist erwartet.
    'missingIncludeSystem',
    'missingInclude',
    # Funktionen, die nur das Plugin aufruft, sieht cppcheck beim
    # Pruefen des Werkzeugs als ungenutzt.
    'unusedFunction',
]

# Nur fuer die Plugin-Dateien, mit dem Grund im Docstring oben.
STUB_ARTEFAKTE = [
    'knownConditionTrueFalse:%s/src/eg3dimport.cpp' % WURZEL,
    'knownConditionTrueFalse:%s/src/eg3dimport_dll.cpp' % WURZEL,
]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--roh', action='store_true',
                        help='ohne die Stub-Unterdrueckung laufen')
    args = parser.parse_args(argv[1:])

    if shutil.which('cppcheck') is None:
        print('cppcheck liegt nicht im PATH - Stilpruefung uebersprungen.')
        print('  Ubuntu:  apt install cppcheck')
        print('  Windows: https://cppcheck.sourceforge.io')
        return 0

    befehl = [
        'cppcheck',
        '--enable=all',
        '--std=c++17',
        '--inline-suppr',
        '--quiet',
        '-I' + os.path.join(WURZEL, 'src'),
        '-I' + os.path.join(HIER, 'sdkstub'),
    ]
    for eintrag in UNTERDRUECKT:
        befehl.append('--suppress=' + eintrag)
    if not args.roh:
        for eintrag in STUB_ARTEFAKTE:
            befehl.append('--suppress=' + eintrag)

    befehl.append(os.path.join(WURZEL, 'src'))
    befehl.append(os.path.join(HIER, 'eg3ddump_main.cpp'))

    lauf = subprocess.run(befehl, capture_output=True, text=True)
    ausgabe = (lauf.stdout + lauf.stderr).strip()

    # Nur echte Befundzeilen zaehlen: cppcheck haengt Notizen an
    # (die Herleitung) und Informationszeilen (nicht getroffene
    # Unterdrueckungen, Checkerliste). Beide sind keine Funde, und
    # sie mitzuzaehlen macht die Zahl unbrauchbar.
    zeilen = [z for z in ausgabe.split('\n')
              if z.strip()
              and ': note:' not in z
              and not z.startswith('nofile:')
              and ': information:' not in z
              and (': error:' in z or ': warning:' in z
                   or ': style:' in z or ': performance:' in z
                   or ': portability:' in z)]

    if zeilen:
        for zeile in zeilen:
            print(zeile)
        print()
        print('%d Fund(e).' % len(zeilen))
        return 1

    print('cppcheck: keine Funde.')
    if not args.roh:
        print('(Stub-Artefakte in src/eg3dimport* unterdrueckt - '
              'mit --roh sichtbar.)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
