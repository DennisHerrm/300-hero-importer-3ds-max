#!/usr/bin/env python3
"""Uebersetzt die Max-Schicht gegen den SDK-Stub - ohne 3ds Max SDK.

    python3 tools/PRUEFE_PLUGIN.py [--jahrgang 2016 2022 ...] [--behalte]

Warum das hier steht: src/eg3dimport*.cpp binden Max-Header ein und
lassen sich deshalb sonst nur auf einem Rechner mit installiertem SDK
uebersetzen. Jeder Tippfehler, jede falsche Signatur, jedes vergessene
override faellt dann erst nach einem SDK-Build auf - bei zwoelf
Jahrgaengen also potenziell zwoelfmal nacheinander.

Der wichtigere Teil sind die MAX_RELEASE-Weichen. Zwischen 2016 und
2027 hat Autodesk mehrfach Signaturen geaendert, und der Quelltext hat
dafuer #if-Weichen. EIN NORMALER BUILD SIEHT IMMER NUR EINEN ZWEIG. Der
andere kann seit Monaten kaputt sein, ohne dass es auffaellt. Dieses
Skript setzt MAX_RELEASE selbst und faehrt beide.

Geprueft wird Uebersetzen UND Linken. Das Linken ist kein Zierrat: ein
fehlender Funktionskoerper faellt beim Uebersetzen nicht auf, weil die
Deklaration im Header dem Compiler genuegt. Erst der Linker meldet ihn -
und wenn der Stub nur uebersetzt, ist er gruen, waehrend alle zwoelf
echten Jahrgaenge an LNK2019 scheitern. Genau das ist bei MeshScale()
passiert.

Was NICHT geprueft wird: Laufzeitverhalten, Aufrufreihenfolge durch
Max, Klassen-ID-Kollisionen. Ein gruener Lauf ersetzt keinen echten
Build - er sortiert nur die Fehler aus, fuer die keiner noetig war.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
SRC = os.path.join(WURZEL, 'src')
STUB = os.path.join(HIER, 'sdkstub')

# Die Grenzfaelle, nicht alle zwoelf: an diesen vier Werten haengen die
# Weichen im Quelltext.
#
#   2016 = 18000  aeltester unterstuetzter Jahrgang
#   2021 = 23000  letzter vor NonLocalizedClassName
#   2022 = 24000  erster mit NonLocalizedClassName und der neuen
#                 ExecuteMAXScriptScript-Signatur
#   2027 = 29000  neuester
JAHRGAENGE = {
    2016: 18000,
    2021: 23000,
    2022: 24000,
    2027: 29000,
}

PLUGIN_QUELLEN = ['eg3dimport.cpp', 'eg3dimport_dll.cpp']

PARSER_QUELLEN = [
    'eg3d_common.cpp', 'eg3d_parallel.cpp', 'eg3d_inflate.cpp',
    'eg3d_json.cpp', 'eg3d_model.cpp', 'eg3d_x.cpp', 'eg3d_dump.cpp',
]


def finde_compiler():
    for name in ('g++', 'clang++'):
        pfad = shutil.which(name)
        if pfad:
            return pfad
    return None


def uebersetze(compiler, jahrgang, release, arbeitsordner, behalte):
    ausgabe = os.path.join(arbeitsordner, 'max%d' % jahrgang)
    os.makedirs(ausgabe, exist_ok=True)

    befehl = [
        compiler,
        '-c',
        '-std=c++17',
        '-O1',
        '-Wall', '-Wextra',
        # Der Stub bildet nur nach, was gebraucht wird; ungenutzte
        # Parameter in den Attrappen sind kein Befund.
        '-Wno-unused-parameter',
        '-ffp-contract=off',
        '-fPIC',
        '-DMAX_RELEASE=%d' % release,
        '-DNOMINMAX',
        '-DWIN32_LEAN_AND_MEAN',
        '-D_UNICODE', '-DUNICODE',
        '-I' + STUB,
        '-I' + SRC,
    ]

    fehler = []
    for quelle in PLUGIN_QUELLEN + PARSER_QUELLEN:
        voll = os.path.join(SRC, quelle)
        if not os.path.exists(voll):
            fehler.append('%s fehlt' % quelle)
            continue

        objekt = os.path.join(ausgabe, quelle.replace('.cpp', '.o'))
        lauf = subprocess.run(befehl + [voll, '-o', objekt],
                              capture_output=True, text=True)
        if lauf.returncode != 0:
            fehler.append('%s:\n%s' % (quelle, lauf.stdout + lauf.stderr))
        elif (lauf.stdout + lauf.stderr).strip():
            fehler.append('%s (Warnungen):\n%s'
                          % (quelle, lauf.stdout + lauf.stderr))

    # ------------------------------------------------------------
    #  LINKEN, nicht nur uebersetzen.
    #
    #  Ein fehlender Funktionskoerper faellt beim Uebersetzen NICHT
    #  auf - die Deklaration im Header genuegt dem Compiler. Erst
    #  der Linker meldet ihn:
    #
    #    error LNK2019: nicht aufgeloestes externes Symbol
    #    "private: float SceneBuilder::MeshScale(void) const"
    #
    #  Genau das ist passiert: MeshScale() war deklariert und
    #  aufgerufen, aber nie definiert. Der Stub-Lauf war gruen,
    #  alle zwoelf Jahrgaenge scheiterten.
    #
    #  Zu einer gemeinsam gelinkten Bibliothek zusammenzufassen
    #  kostet Sekunden und schliesst die Luecke. Ein Hauptprogramm
    #  braucht es dafuer nicht - eine statische Bibliothek zwingt
    #  den Linker noch nicht, aber ein Shared Object schon.
    # ------------------------------------------------------------
    if not fehler:
        objekte = [os.path.join(ausgabe, q.replace('.cpp', '.o'))
                   for q in PLUGIN_QUELLEN + PARSER_QUELLEN]
        objekte = [o for o in objekte if os.path.exists(o)]
        ziel = os.path.join(ausgabe, 'plugin.so')
        # -Wl,--no-undefined ist der GANZE PUNKT: ohne das
        # erlaubt ein Shared Object undefinierte Symbole und der
        # Link geht durch, obwohl die Funktion fehlt. Erst mit
        # dieser Option verhaelt sich der Linker wie beim Bauen
        # einer DLL unter Windows, wo jedes Symbol aufgeloest sein
        # muss.
        #
        # Beim ersten Anlauf stand sie nicht da - und die
        # Gegenprobe (MeshScale-Definition entfernen) meldete
        # weiterhin "sauber". Ein Pruefschritt ohne Gegenprobe ist
        # unbewiesen, siehe CODESTIL.md.
        lauf = subprocess.run([compiler, '-shared', '-Wl,--no-undefined',
                               '-o', ziel] + objekte,
                              capture_output=True, text=True)
        if lauf.returncode != 0:
            fehler.append('Linken:\n%s' % (lauf.stdout + lauf.stderr))

    if not behalte:
        shutil.rmtree(ausgabe, ignore_errors=True)

    return fehler


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jahrgang', type=int, nargs='*',
                        help='nur diese Jahrgaenge pruefen')
    parser.add_argument('--behalte', action='store_true',
                        help='Objektdateien nicht loeschen')
    args = parser.parse_args(argv[1:])

    if not os.path.exists(os.path.join(SRC, 'eg3dimport.cpp')):
        print('src/eg3dimport.cpp fehlt - nichts zu pruefen.')
        return 0

    compiler = finde_compiler()
    if compiler is None:
        print('FEHLER: weder g++ noch clang++ im PATH.')
        return 2

    gewaehlt = args.jahrgang if args.jahrgang else sorted(JAHRGAENGE)

    print('Compiler: %s' % compiler)
    print('Stub:     %s' % STUB)
    print()

    gesamt = 0
    arbeitsordner = tempfile.mkdtemp(prefix='eg3d_stub_')
    try:
        for jahrgang in gewaehlt:
            release = JAHRGAENGE.get(jahrgang)
            if release is None:
                print('  Max %d: kein MAX_RELEASE hinterlegt, uebersprungen'
                      % jahrgang)
                continue

            fehler = uebersetze(compiler, jahrgang, release, arbeitsordner,
                                args.behalte)
            if fehler:
                gesamt += len(fehler)
                print('  Max %d (MAX_RELEASE %d): %d Befund(e)'
                      % (jahrgang, release, len(fehler)))
                for eintrag in fehler:
                    for zeile in eintrag.rstrip().split('\n'):
                        print('    %s' % zeile)
            else:
                print('  Max %d (MAX_RELEASE %d): sauber'
                      % (jahrgang, release))
    finally:
        if args.behalte:
            print('\nObjektdateien in %s' % arbeitsordner)
        else:
            shutil.rmtree(arbeitsordner, ignore_errors=True)

    print()
    if gesamt:
        print('%d Befund(e). Der Stub ist keine Garantie, aber ein Befund '
              'hier ist im echten Build auch einer.' % gesamt)
        return 1

    print('Alle geprueften Jahrgaenge uebersetzen sauber gegen den Stub.')
    print('Das ersetzt keinen SDK-Build - siehe tools/sdkstub/README.md.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
