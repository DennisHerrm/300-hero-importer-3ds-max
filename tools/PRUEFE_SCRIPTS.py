#!/usr/bin/env python3
"""Prueft scripts/*.mcr gegen den Interface-Deskriptor in C++.

    python3 tools/PRUEFE_SCRIPTS.py

Warum es das gibt: in 0.1.0 stand in der .mcr

    lblKind.text = "Format: " + Eg3dCpp.kind

ohne Klammern. MAXScript liefert dann das FUNKTIONSOBJEKT statt seines
Ergebnisses, und der Fehler kommt erst beim Anklicken:

    -- Unable to convert: kind() to type: String
    -- Line number: 128

Kein Compiler sieht das - MAXScript wird erst zur Laufzeit ausgewertet,
und zwar erst an der Stelle, an der der Benutzer draufdrueckt. Ein
Tippfehler in einem selten benutzten Knopf faellt also beim Benutzer auf
oder gar nicht.

Geprueft wird:

  1. Jeder Eg3dCpp.<name> existiert im Deskriptor (eg3dimport.cpp).
  2. Parameterlose Methoden werden mit () aufgerufen.
  3. Die Zahl der Argumente stimmt mit dem Deskriptor ueberein.

Rueckgabe 0 = stimmig, 1 = Fund.
"""

import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
QUELLE = os.path.join(WURZEL, 'src', 'eg3dimport.cpp')
SKRIPTE = os.path.join(WURZEL, 'scripts')

# Der Name, unter dem das Interface in MAXScript erscheint. Steht
# im Deskriptorkopf als zweites Argument.
INTERFACE = 'Eg3dCpp'

# ----------------------------------------------------------------
#  Eingebaute MAXScript-Funktionen, die man nicht ueberschreiben darf
#
#  Eine eigene "fn openFile ..." verdeckt die eingebaute, und JEDER
#  spaetere Aufruf der echten landet bei der eigenen. Der Fehler
#  aeussert sich weit entfernt von seiner Ursache: der Szenendump
#  wollte eine Textdatei zum Anhaengen oeffnen und bekam den
#  Modell-Lader, der sich mit "weder EG3D noch JUMPX" meldete.
#
#  Die Liste ist nicht vollstaendig - sie enthaelt die Namen, die
#  man in einem Importer versehentlich vergibt.
# ----------------------------------------------------------------
BUILTINS = {
    'openFile', 'createFile', 'close', 'format', 'print',
    'getOpenFileName', 'getSaveFileName', 'getFiles', 'deleteFile',
    'copy', 'move', 'rotate', 'scale', 'select', 'delete',
    'addNoteTrack', 'deleteNoteTrack', 'getNoteTrack', 'numNoteTracks',
    'addNewNoteKey', 'notetrack', 'messageBox', 'filterString',
    'appendIfUnique', 'append', 'findItem', 'execute', 'fileIn',
}


def lies(pfad):
    with open(pfad, 'r', encoding='utf-8', errors='replace') as handle:
        return handle.read()


def deskriptor():
    """{maxscript-name: Argumentzahl} aus eg3dimport.cpp."""
    text = lies(QUELLE)
    treffer = re.search(r'the%s\w*\s*\((.*?)\n\);' % 'Eg3dImportInterface',
                        text, re.S)
    if not treffer:
        return None
    muster = re.compile(
        r'fn_\w+\s*,\s*_T\(\s*"([^"]*)"\s*\)\s*,\s*\d+\s*,\s*TYPE_\w+\s*,'
        r'\s*\d+\s*,\s*(\d+)')
    return {m.group(1): int(m.group(2))
            for m in muster.finditer(treffer.group(1))}


def ohne_kommentare(text):
    """MAXScript-Kommentare entfernen: -- bis Zeilenende, /* */."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    zeilen = []
    for zeile in text.split('\n'):
        stelle = zeile.find('--')
        # Anfuehrungszeichen zaehlen, damit ein "--" IN einem
        # String nicht als Kommentar gilt.
        while stelle >= 0 and zeile[:stelle].count('"') % 2 == 1:
            stelle = zeile.find('--', stelle + 2)
        zeilen.append(zeile if stelle < 0 else zeile[:stelle])
    return '\n'.join(zeilen)


def zaehle_argumente(rest):
    """Argumente hinter einem klammerlosen Aufruf grob zaehlen.

    MAXScript trennt Argumente durch Leerzeichen; geklammerte
    Ausdruecke und Zeichenketten zaehlen als eines. Das reicht,
    um 'zwei erwartet, eines da' zu erkennen."""
    anzahl = 0
    i = 0
    while i < len(rest):
        zeichen = rest[i]
        if zeichen in ' \t':
            i += 1
            continue
        if zeichen in ')\n':
            break
        anzahl += 1
        if zeichen == '"':
            i = rest.find('"', i + 1)
            if i < 0:
                break
            i += 1
        elif zeichen == '(':
            tiefe = 1
            i += 1
            while i < len(rest) and tiefe > 0:
                if rest[i] == '(':
                    tiefe += 1
                elif rest[i] == ')':
                    tiefe -= 1
                i += 1
        else:
            while i < len(rest) and rest[i] not in ' \t)\n':
                i += 1
    return anzahl


def main():
    if not os.path.isfile(QUELLE):
        print('src/eg3dimport.cpp fehlt - nichts zu pruefen.')
        return 0

    bekannt = deskriptor()
    if bekannt is None:
        print('Interface-Deskriptor in eg3dimport.cpp nicht gefunden.')
        return 1

    if not os.path.isdir(SKRIPTE):
        print('scripts/ fehlt - nichts zu pruefen.')
        return 0

    funde = []
    geprueft = 0

    for name in sorted(os.listdir(SKRIPTE)):
        if not name.lower().endswith(('.mcr', '.ms')):
            continue
        geprueft += 1

        roh = lies(os.path.join(SKRIPTE, name))
        text = ohne_kommentare(roh)

        # ----------------------------------------------------------
        #  'return' in einem Event-Handler
        #
        #  "on x pressed do (...)" ist KEINE Funktion - ein return
        #  darin bricht den Handler nicht ab, sondern verhaelt sich
        #  unerwartet. Genau daran ist der Log-Knopf gescheitert:
        #  der Fehlerzweig lief weiter, und angezeigt wurde eine
        #  Meldung von vorhin.
        #
        #  Gesucht wird ein 'return' zwischen einem "on ... do" und
        #  dem naechsten "fn " oder "on " - grob, aber es findet
        #  genau diesen Fall.
        # ----------------------------------------------------------
        # Eigene Funktion mit dem Namen einer eingebauten?
        for treffer in re.finditer(r'^\s*fn\s+(\w+)', text, re.M):
            eigen = treffer.group(1)
            if eigen in BUILTINS:
                nummer = text[:treffer.start()].count('\n') + 1
                funde.append(
                    '%s:%d: "fn %s" ueberschreibt eine eingebaute '
                    'MAXScript-Funktion - jeder Aufruf der echten landet '
                    'dann bei dieser' % (name, nummer, eigen))

        zeilen = text.split('\n')
        in_handler = False
        for nummer, zeile in enumerate(zeilen, 1):
            if re.match(r'\s*fn\s+\w+', zeile):
                in_handler = False
            elif re.match(r'\s*on\s+\S+.*\bdo\b', zeile):
                in_handler = True
            elif in_handler and re.search(r'\breturn\b', zeile):
                funde.append(
                    '%s:%d: return in einem Event-Handler - "on ... do" ist '
                    'keine Funktion, das return bricht ihn nicht ab'
                    % (name, nummer))

        for treffer in re.finditer(r'\b%s\.(\w+)' % INTERFACE, text):
            methode = treffer.group(1)
            zeile = text[:treffer.start()].count('\n') + 1

            if methode not in bekannt:
                funde.append('%s:%d: %s.%s gibt es im Deskriptor nicht'
                             % (name, zeile, INTERFACE, methode))
                continue

            erwartet = bekannt[methode]
            rest = text[treffer.end():]
            hat_klammer = rest.lstrip(' \t').startswith('(')

            if erwartet == 0 and not hat_klammer:
                # DER Fehler aus 0.1.0.
                funde.append(
                    '%s:%d: %s.%s ohne Klammern - MAXScript liefert dann '
                    'das Funktionsobjekt statt des Ergebnisses '
                    '("Unable to convert: %s() to type: ...")'
                    % (name, zeile, INTERFACE, methode, methode))
                continue

            if erwartet > 0 and not hat_klammer:
                gefunden = zaehle_argumente(rest)
                if gefunden != erwartet:
                    funde.append('%s:%d: %s.%s erwartet %d Argument(e), '
                                 'gefunden %d'
                                 % (name, zeile, INTERFACE, methode,
                                    erwartet, gefunden))

    if funde:
        for eintrag in funde:
            print('  %s' % eintrag)
        print()
        print('%d Fund(e).' % len(funde))
        return 1

    print('%d Skript(e) geprueft: jeder %s-Aufruf existiert und hat die '
          'richtige Form.' % (geprueft, INTERFACE))
    return 0


if __name__ == '__main__':
    sys.exit(main())
