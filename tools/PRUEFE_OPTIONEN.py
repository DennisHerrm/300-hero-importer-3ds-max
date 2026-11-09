#!/usr/bin/env python3
"""Prueft, dass jede Importoption auch wirklich benutzt wird.

    python3 tools/PRUEFE_OPTIONEN.py

WARUM ES DAS GIBT

Optionen werden hier per Textersatz in den Quelltext eingefuegt. Trifft
der Ankertext nicht, landet der Block nicht im Code - und es sieht so
aus, als sei die Option eingebaut: sie steht in ImportOptions, hat einen
Setter, einen Deskriptoreintrag und eine Checkbox. Nur die Wirkung
fehlt.

Das Tueckische ist das Fehlerbild: es sieht nach einem RECHENPROBLEM
aus. Man prueft die Matrix, misst nach, dreht das Vorzeichen um -
waehrend der Code gar nicht laeuft.

Geprueft wird: hat jedes Feld in ImportOptions mindestens eine
Verwendung als options_.<name> ausserhalb seines Setters?

Rueckgabe 0 = alles verdrahtet, 1 = Fund.
"""

import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
HEADER = os.path.join(WURZEL, 'src', 'eg3dimport.h')
QUELLE = os.path.join(WURZEL, 'src', 'eg3dimport.cpp')

# Felder, die absichtlich nicht in C++ ausgewertet werden.
#
#   noteTrack             - die Note Tracks entstehen in MAXScript
#                           (addNoteTrack); C++ liefert nur die
#                           Clipgrenzen ueber clipRanges.
#   skipParticleTemplates - nicht instanzierte Gruppen werden ohnehin
#                           uebersprungen, weil kein Knoten auf sie
#                           zeigt.
#   animRootFix           - Versuch aus 0.2.6, bewusst ohne Code
#   animBindScale         - Versuch aus 0.2.13, bewusst ohne Code
#
# Die letzten beiden stehen hier, weil ihre Wirkung das Ergebnis
# verschlechtert hat. Sie sind absichtlich folgenlos - wer sie
# wiederbelebt, streicht sie aus dieser Liste und weiss dann, dass
# er etwas aendert, das schon einmal schiefging.
OHNE_WIRKUNG = {'noteTrack', 'skipParticleTemplates',
                'animRootFix', 'animBindScale'}


def main():
    if not os.path.isfile(HEADER):
        print('src/eg3dimport.h fehlt - nichts zu pruefen.')
        return 0

    with open(HEADER, encoding='utf-8', errors='replace') as handle:
        header = handle.read()

    treffer = re.search(r'struct ImportOptions \{(.*?)\n\};', header, re.S)
    if not treffer:
        print('struct ImportOptions nicht gefunden.')
        return 1

    felder = re.findall(r'^\s*(?:bool|int|float)\s+(\w+)\s*=',
                        treffer.group(1), re.M)
    if not felder:
        print('keine Felder in ImportOptions gefunden.')
        return 1

    with open(QUELLE, encoding='utf-8', errors='replace') as handle:
        quelle = handle.read()

    funde = []
    for feld in felder:
        if feld in OHNE_WIRKUNG:
            continue
        # Verwendungen ausserhalb der Setter zaehlen. Ein Setter sieht
        # so aus:  options_.<feld> = (on != 0);
        alle = len(re.findall(r'options_\.%s\b' % feld, quelle))
        setter = len(re.findall(r'options_\.%s\s*=' % feld, quelle))
        if alle - setter <= 0:
            funde.append('%s: in ImportOptions deklariert, aber nirgends '
                         'ausgewertet - die Option tut nichts' % feld)

    if funde:
        for eintrag in funde:
            print('  %s' % eintrag)
        print()
        print('%d Fund(e).' % len(funde))
        return 1

    print('Alle %d wirksamen Importoptionen sind verdrahtet '
          '(%d bewusst folgenlos).'
          % (len(felder) - len(OHNE_WIRKUNG & set(felder)),
             len(OHNE_WIRKUNG & set(felder))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
