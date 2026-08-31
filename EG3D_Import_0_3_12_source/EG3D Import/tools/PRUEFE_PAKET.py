#!/usr/bin/env python3
"""Prueft PackageContents.xml gegen die Spezifikation UND gegen die Dateien.

    python3 tools/PRUEFE_PAKET.py

Warum es das gibt: die Installation von 0.1.0 brach ab, weil
INSTALLIERE.bat eine Datei kopieren wollte, die es nie gab
(Eg3dImportAlt.mcr - ein Rest aus dem BAF-Vorbild). Alle zwoelf Plugins
waren fertig gebaut; gescheitert ist eine Namensliste, die an zwei
Stellen gepflegt werden musste.

Geprueft wird deshalb beides:

  1. Die Pflichtangaben der "Autodesk Application Plug-in Package Format
     Specification" fuer 3ds Max. Fehlt eine, laedt Max das Paket
     kommentarlos nicht - der haeufigste Fall ist <CompanyDetails>.

  2. Ob jede in der XML genannte Datei existiert und ob jede vorhandene
     Datei in der XML steht. Beide Richtungen: eine XML-Zeile ohne Datei
     ergibt einen Eintrag im max.log, den niemand liest; eine Datei ohne
     XML-Zeile wird schlicht nie geladen.

Rueckgabe 0 = stimmig, 1 = Fund.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
PAKET = os.path.join(WURZEL, 'package', 'Eg3dImport')
XML = os.path.join(PAKET, 'PackageContents.xml')

# Jahrgaenge, fuer die je ein eigener Components-Block noetig ist.
JAHRGAENGE = [str(j) for j in range(2016, 2028)]

# Aus der Spezifikation. Andere Werte laedt Max nicht.
GUELTIGE_DESCRIPTIONS = {
    'plugins parts',
    'assemblies parts',
    'ui schemes parts',
    'default setting paths parts',
    'pre-start-up scripts parts',
    'macroscripts parts',
    'post-start-up scripts parts',
    'light icon paths parts',
    'dark icon paths parts',
    'scene converter folders parts',
    'osl folders parts',
    'amg folders parts',
    'hotkey parts',
}


def pruefe_kopf(root, funde):
    """Pflichtattribute des <ApplicationPackage>-Knotens."""
    # AutodeskProduct und ProductType sind auf feste Werte
    # festgelegt - ein Tippfehler dort laedt das Paket nicht.
    if root.get('AutodeskProduct') != '3ds Max':
        funde.append('AutodeskProduct muss "3ds Max" sein, ist: %r'
                     % root.get('AutodeskProduct'))
    if root.get('ProductType') != 'Application':
        funde.append('ProductType muss "Application" sein, ist: %r'
                     % root.get('ProductType'))

    version = root.get('AppVersion')
    if not version:
        funde.append('AppVersion fehlt (Pflicht, Form MMM.mmm.bbb)')
    elif not re.fullmatch(r'\d+\.\d+\.\d+', version):
        funde.append('AppVersion hat nicht die Form MMM.mmm.bbb: %r' % version)

    # UpgradeCode ist die DAUERHAFTE Kennung: Max laedt je
    # UpgradeCode nur ein Paket, und zwar das mit der hoeheren
    # AppVersion. Er darf sich nie aendern.
    if not root.get('UpgradeCode'):
        funde.append('UpgradeCode fehlt (bei 3ds Max Pflicht)')

    # Der haeufigste stille Ausfall.
    if root.find('CompanyDetails') is None:
        funde.append('<CompanyDetails> fehlt - ohne den Knoten laedt das '
                     'Plugin NICHT, ohne verwertbare Meldung')

    return version


def pruefe_runtime(elem, wo, funde):
    req = elem.find('RuntimeRequirements')
    if req is None:
        funde.append('%s: <RuntimeRequirements> fehlt (in <Components> Pflicht)'
                     % wo)
        return None, None
    if req.get('OS') != 'Win64':
        funde.append('%s: OS muss "Win64" sein, ist: %r' % (wo, req.get('OS')))
    if req.get('Platform') != '3ds Max':
        funde.append('%s: Platform muss "3ds Max" sein, ist: %r'
                     % (wo, req.get('Platform')))
    # SeriesMax ist seit dem 2022er SDK PFLICHT: "plugins will no
    # longer be loaded if the attribute is not found."
    if not req.get('SeriesMax'):
        funde.append('%s: SeriesMax fehlt - seit dem 2022er SDK laedt Max '
                     'das Plugin dann gar nicht' % wo)
    return req.get('SeriesMin'), req.get('SeriesMax')


def main():
    if not os.path.isfile(XML):
        print('Nicht gefunden: %s' % XML)
        return 1

    try:
        root = ET.parse(XML).getroot()
    except ET.ParseError as fehler:
        print('PackageContents.xml ist kein gueltiges XML: %s' % fehler)
        return 1

    funde = []
    appversion = pruefe_kopf(root, funde)

    plugin_jahrgaenge = []
    genannte_dateien = []   # (Pfad relativ zum Paket, mit Platzhalter?)

    for komponente in root.findall('Components'):
        beschreibung = komponente.get('Description')
        if beschreibung is None:
            funde.append('<Components> ohne Description-Attribut (Pflicht)')
            continue
        if beschreibung not in GUELTIGE_DESCRIPTIONS:
            funde.append('unbekannte Description %r - Max kennt nur die Werte '
                         'aus der Spezifikation' % beschreibung)

        smin, smax = pruefe_runtime(komponente, beschreibung, funde)

        eintraege = komponente.findall('ComponentEntry')
        if not eintraege:
            funde.append('%s: kein <ComponentEntry> (mindestens einer noetig)'
                         % beschreibung)

        for eintrag in eintraege:
            modul = eintrag.get('ModuleName')
            if not modul:
                funde.append('%s: ComponentEntry ohne ModuleName' % beschreibung)
                continue

            # Version im Eintrag sollte zur AppVersion passen -
            # sonst faellt beim Versionssprung eine Stelle hinten
            # runter.
            eintrag_version = eintrag.get('Version')
            if appversion and eintrag_version and eintrag_version != appversion:
                funde.append('%s: ComponentEntry Version %r weicht von '
                             'AppVersion %r ab'
                             % (beschreibung, eintrag_version, appversion))

            genannte_dateien.append((modul, beschreibung))

            if beschreibung == 'plugins parts':
                # Ein Block je Jahrgang, SeriesMin == SeriesMax.
                # Die Spezifikation ist deutlich: "3ds Max does not
                # test for binary compatibility." Ein Block ueber
                # mehrere Jahre wuerde EINE DLU in alle laden - und
                # eine gegen 2016 gebaute DLU in 2027 stuerzt ab,
                # statt eine Meldung zu geben.
                if smin != smax:
                    funde.append('plugins parts: SeriesMin %r != SeriesMax %r. '
                                 'Je Jahrgang ein eigener Block, sonst laedt '
                                 'Max eine DLU in einen Jahrgang, gegen den '
                                 'sie nicht gebaut ist.' % (smin, smax))
                if smin:
                    plugin_jahrgaenge.append(smin)

    fehlende = [j for j in JAHRGAENGE if j not in plugin_jahrgaenge]
    if fehlende:
        funde.append('kein plugins-parts-Block fuer: %s' % ', '.join(fehlende))

    doppelte = {j for j in plugin_jahrgaenge
                if plugin_jahrgaenge.count(j) > 1}
    if doppelte:
        funde.append('mehr als ein plugins-parts-Block fuer: %s'
                     % ', '.join(sorted(doppelte)))

    # ---- XML gegen den Ordnerinhalt, beide Richtungen ----
    #
    # Nur wenn das Paket schon gebaut ist. Vor dem ersten
    # INSTALLIERE.bat gibt es Contents/ nicht, und das ist kein
    # Fund.
    contents = os.path.join(PAKET, 'Contents')
    if os.path.isdir(contents):
        getroffen = set()

        for modul, beschreibung in genannte_dateien:
            pfad = modul.replace('/', os.sep).lstrip('.' + os.sep)
            voll = os.path.join(PAKET, pfad)

            if '*' in os.path.basename(voll):
                ordner = os.path.dirname(voll)
                muster = os.path.basename(voll).replace('.', r'\.')
                muster = muster.replace('*', '.*')
                if not os.path.isdir(ordner):
                    funde.append('%s: Ordner fehlt: %s' % (beschreibung, ordner))
                    continue
                treffer = [n for n in os.listdir(ordner)
                           if re.fullmatch(muster, n, re.IGNORECASE)]
                if not treffer:
                    funde.append('%s: Platzhalter %r trifft keine Datei'
                                 % (beschreibung, modul))
                for name in treffer:
                    getroffen.add(os.path.join(ordner, name))
            else:
                if not os.path.isfile(voll):
                    funde.append('%s: Datei fehlt: %s' % (beschreibung, modul))
                else:
                    getroffen.add(voll)

        # Andere Richtung: was liegt da, ohne genannt zu sein?
        for wurzel, _, dateien in os.walk(contents):
            for name in dateien:
                voll = os.path.join(wurzel, name)
                if voll in getroffen:
                    continue
                if name.lower().endswith(('.dlu', '.mcr', '.ms', '.dll')):
                    rel = os.path.relpath(voll, PAKET).replace(os.sep, '/')
                    funde.append('liegt im Paket, steht aber in keinem '
                                 'ComponentEntry: ./%s' % rel)

    # ---- Versionsnummern im Quelltext ----
    header = os.path.join(WURZEL, 'src', 'eg3dimport.h')
    if appversion and os.path.isfile(header):
        text = open(header, encoding='utf-8', errors='replace').read()
        treffer = re.search(r'EG3DIMPORT_VERSION_STR\s+_T\("([^"]+)"\)', text)
        if treffer and treffer.group(1) != appversion:
            funde.append('EG3DIMPORT_VERSION_STR ist %r, AppVersion %r'
                         % (treffer.group(1), appversion))

    if funde:
        for eintrag in funde:
            print('  %s' % eintrag)
        print()
        print('%d Fund(e).' % len(funde))
        return 1

    print('PackageContents.xml: Pflichtangaben vollstaendig, je ein '
          'plugins-parts-Block fuer 2016-2027.')
    if os.path.isdir(contents):
        print('Paketinhalt und XML stimmen ueberein (beide Richtungen).')
    else:
        print('(Contents/ noch nicht gebaut - Dateiabgleich uebersprungen.)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
