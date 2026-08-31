#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Referenzdump fuer EG3D Import.

    python pydump.py <datei|ordner> [--all] [--verts] [--tris] [--anim]
                                    [--bind] [--layout] [-o datei.txt]

Schreibt Zeile fuer Zeile dasselbe wie eg3ddump.exe. Der Vergleich
beider Ausgaben ist die eigentliche Pruefung des C++-Parsers:

    eg3ddump.exe <ordner> --all -o cpp.txt
    python pydump.py <ordner> --all -o py.txt
    fc /L cpp.txt py.txt          (bzw. diff unter Linux)

WARUM EINE ZWEITE IMPLEMENTIERUNG UND NICHT EIN TESTFALL

Weil ein Testfall nur prueft, was jemand sich ausgedacht hat. Zwei
unabhaengig geschriebene Parser, die ueber 53 Retail-Dateien
zeichengleiche Ausgaben liefern, schliessen die ganze Klasse von
Fehlern aus, die "sieht im Viewport richtig aus" ueberlebt - allen
voran die falsche Pufferbasis, die jahrelang wie sechs
verschiedene Probleme aussah.

BEWUSST OHNE das Blender-Addon als Abhaengigkeit: das Addon ist die
Quelle des Formatwissens, nicht der Massstab. Waere es beides,
wuerde ein Denkfehler im Addon auf beiden Seiten gleich
herauskommen und unsichtbar bleiben.
"""

import json
import math
import os
import struct
import sys
import zlib

BLOCK_BASE = 12
ADDR_BASE = 1000000000
BONE_STRIDE = 172
GEO_STRIDE = 124
ACT_STRIDE = 90
MTL_STRIDE = 48
TEX_STRIDE = 8
BYTES_PER_FRAME = (28, 32, 40, 44)
SCALE_MIN_BPF = 40

# Werttypen aus dem Kanaldeskriptor (Feld 6, zweiter Eintrag).
VALUE_TYPE_FLOAT = 0    # vier Byte je Komponente
VALUE_TYPE_BOOL = 6     # ein Byte je Komponente, nur 0 oder 1

ATTR_LABEL = {0: 'POSITION', 1: 'NORMAL', 2: 'TANGENT', 3: 'COLOR',
              4: 'BONE_INDICES', 5: 'BONE_WEIGHTS', 6: 'UV0', 7: 'UV1'}


# ---------------------------------------------------------------------------
#  Zahlenausgabe - muss Zeichen fuer Zeichen FormatFloat() in
#  eg3d_common.cpp entsprechen.
#
#  Bewusst NICHT '%.6f': weder printf noch std::to_chars legen fest,
#  wohin ein exakter Rundungs-Gleichstand faellt. Die UCRT von
#  Microsoft darf dort anders runden als die glibc, gegen die dieses
#  Skript laeuft - und der Vergleich wuerde eine Abweichung melden,
#  an der nichts falsch ist.
#
#  Also selbst rechnen: skalieren (gewoehnliche IEEE-754-
#  Multiplikation, auf jeder Plattform bitgleich), Betrag, +0.5,
#  abrunden. Die Tie-Regel steht damit in beiden Sprachen im
#  Quelltext.
# ---------------------------------------------------------------------------
def f(value, decimals=6):
    value = float(value)
    if math.isnan(value):
        return 'nan'
    if math.isinf(value):
        return '-inf' if value < 0.0 else 'inf'
    scale = 10 ** decimals
    magnitude = math.floor(abs(value * float(scale)) + 0.5)
    if magnitude >= 9223372036854775808.0:
        return '-huge' if value < 0.0 else 'huge'
    units = int(magnitude)
    whole, fraction = divmod(units, scale)
    sign = '-' if math.copysign(1.0, value) < 0.0 else ''
    if decimals == 0:
        return '%s%d' % (sign, whole)
    return '%s%d.%0*d' % (sign, whole, decimals, fraction)


def f32(value):
    """Auf float32 zurechtstutzen. Der C++-Parser haelt alle
    Geometriewerte als float; ohne diesen Schritt weicht die
    Referenz in den letzten Stellen ab."""
    return struct.unpack('<f', struct.pack('<f', value))[0]


def v3(v):
    return '%s %s %s' % (f(v[0]), f(v[1]), f(v[2]))


def v2(v):
    return '%s %s' % (f(v[0]), f(v[1]))


def v4(v):
    return '%s %s %s %s' % (f(v[0]), f(v[1]), f(v[2]), f(v[3]))


def ilist(values):
    return ','.join(str(v) for v in values) if values else '-'


def half_to_float(half):
    return struct.unpack('<e', struct.pack('<H', half))[0]


# ---------------------------------------------------------------------------
#  GBK -> UTF-8
#
#  Spiegelt den Nicht-Windows-Zweig von GbkToUtf8() in
#  eg3d_common.cpp: eine umkehrbare Abbildung in die Privatzone,
#  KEINE echte Zeichentabelle. Unter Windows nimmt der C++-Code die
#  Codepage 936, dann laufen die Ausgaben auseinander - deshalb
#  vergleicht man auf EINER Plattform, so wie bei BAF Toolkit auch.
# ---------------------------------------------------------------------------
def gbk_marker(raw):
    if all(b < 0x80 for b in raw):
        return raw.decode('ascii')
    out = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c < 0x80:
            out.append(chr(c))
            i += 1
            continue
        if i + 1 < len(raw) and 0x81 <= c <= 0xFE:
            d = raw[i + 1]
            if 0x40 <= d <= 0xFE and d != 0x7F:
                out.append(chr(0xE000 + (c - 0x81) * 191 +
                               ((d - 0x41) if d >= 0x7F else (d - 0x40))))
                i += 2
                continue
        out.append(chr(0xE000 + 0xB000 + c))
        i += 1
    return ''.join(out)


def cstr(buf, off, limit=256):
    if not (0 <= off < len(buf)):
        return ''
    end = buf.find(b'\x00', off, min(len(buf), off + limit))
    if end < 0:
        end = min(len(buf), off + limit)
    return gbk_marker(buf[off:end])


class Sections(object):
    def __init__(self, everything=False):
        self.container = True
        self.meta = True
        self.geometry = True
        self.skeleton = True
        self.clips = True
        self.warnings = True
        self.vertices = everything
        self.triangles = everything
        self.bindpose = everything
        self.animation = everything
        self.layout = everything


# ===========================================================================
#  .model
# ===========================================================================

def attr_stride(type_code, comps):
    size = {0: 1, 1: 1, 3: 1, 2: 2, 4: 2, 5: 2}.get(type_code, 0)
    if size == 0:
        raise ValueError('unbekannter Typcode %d' % type_code)
    if type_code in (0, 3) and comps < 4:
        return 4
    return size * comps


def comp_size(type_code):
    return {0: 1, 1: 1, 3: 1, 2: 2, 4: 2, 5: 2}[type_code]


def read_component(data, off, type_code):
    if type_code in (0, 1):
        return float(data[off])
    if type_code == 3:
        return float(struct.unpack_from('<b', data, off)[0])
    if type_code == 2:
        return float(struct.unpack_from('<H', data, off)[0])
    if type_code in (4, 5):
        return float(half_to_float(struct.unpack_from('<H', data, off)[0]))
    return 0.0


def normalize_denominator(type_code):
    return {0: 255.0, 1: 255.0, 3: 127.0, 2: 65535.0}.get(type_code, 0.0)


def decode_normal(four):
    c0, c1 = struct.unpack_from('<hh', four, 0)
    z = c0 / 16384.0
    phi = c1 / 32768.0 * math.pi
    r = math.sqrt(max(0.0, 1.0 - z * z))
    nx, ny, nz = -r * math.sin(phi), -z, -r * math.cos(phi)
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    inv = 1.0 / max(length, 1e-9)
    return (f32(nx * inv), f32(ny * inv), f32(nz * inv))


def mat_invertible(m):
    """Gauss-Jordan mit Spaltenpivot, wie MatInvert() in
    eg3d_common.cpp. Gebraucht wird nur die Ja/Nein-Antwort."""
    a = [[float(m[r][c]) for c in range(4)] + [1.0 if r == c else 0.0 for c in range(4)]
         for r in range(4)]
    for col in range(4):
        pivot = col
        for r in range(col + 1, 4):
            if abs(a[r][col]) > abs(a[pivot][col]):
                pivot = r
        if abs(a[pivot][col]) < 1e-12:
            return False
        a[col], a[pivot] = a[pivot], a[col]
        inv = 1.0 / a[col][col]
        for c in range(8):
            a[col][c] *= inv
        for r in range(4):
            if r == col:
                continue
            factor = a[r][col]
            if factor == 0.0:
                continue
            for c in range(8):
                a[r][c] -= factor * a[col][c]
    return True


class ModelDump(object):
    def __init__(self, path, data):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.data = data
        self.warnings = []
        self.layout = []
        self.parse()

    # -- Container ------------------------------------------------------
    def parse(self):
        d = self.data
        self.version, self.main_size = struct.unpack_from('<II', d, 4)
        self.json_start = BLOCK_BASE + self.main_size
        if self.json_start >= len(d):
            raise ValueError('mainSize legt den JSON-Teil hinter das Dateiende')
        # raw_decode zaehlt ZEICHEN, der C++-Parser zaehlt BYTES.
        # Bei chinesischen Materialnamen laufen die beiden Zahlen
        # auseinander (bis zu 20 in den Testdaten). Also
        # zurueckrechnen - gemessen wird der Bytebedarf des
        # verbrauchten Teils.
        raw_tail = d[self.json_start:]
        tail = raw_tail.decode('utf-8', errors='replace')
        self.meta, chars = json.JSONDecoder().raw_decode(tail)
        self.json_chars = len(tail[:chars].encode('utf-8'))
        self.json_trailing = len(raw_tail) - self.json_chars

        header = self.meta[0] if isinstance(self.meta[0], dict) else {}
        self.generator = header.get('generator', '')
        self.gen_version = header.get('version', '')
        self.left_hand = bool(header.get('leftHandCoord', False))

        self.parse_materials()
        self.parse_geometry()
        self.parse_palettes()
        self.parse_nodes()
        self.parse_clips()
        # stable sort nach start, wie std::stable_sort im C++-Teil
        self.layout.sort(key=lambda b: b[1])

    def parse_materials(self):
        self.materials = []
        for m in self.meta[2]:
            props = m[2] if len(m) > 2 and isinstance(m[2], dict) else {}
            textures = props.get('textures') or {}
            vectors = props.get('vectors') or {}
            self.materials.append({
                'name': m[0] if len(m) > 0 and isinstance(m[0], str) else 'material',
                'type': m[1] if len(m) > 1 and isinstance(m[1], str) else '',
                'blend': int(props.get('blendMode', 0) or 0),
                'double': bool(props.get('doubleSide', False)),
                'depthWrite': bool(props.get('depthWrite', True)),
                'depthTest': bool(props.get('depthTest', True)),
                'textures': {k: int(v) for k, v in textures.items()},
                'vectors': {k: [f32(x) for x in (list(v) + [0, 0, 0, 0])[:4]]
                            for k, v in vectors.items() if isinstance(v, list)},
            })
        self.textures = []
        for t in self.meta[6]:
            self.textures.append(t.get('uri', '') if isinstance(t, dict) else str(t))

    def parse_geometry(self):
        d = self.data
        self.groups = []
        for gi, group in enumerate(self.meta[3]):
            subs = []
            for si, desc in enumerate(group):
                sub = {'group': gi, 'index': si, 'material': int(desc[0]),
                       'verts': int(desc[1]), 'attrs': [], 'bad': 0,
                       'idxOffset': int(desc[3][0]), 'idxCount': int(desc[3][1])}
                sub['name'] = 'g%d_s%d' % (gi, si)
                vc = sub['verts']

                for adesc in desc[2]:
                    a = {'id': int(adesc[0]), 'type': int(adesc[1]),
                         'qual': int(adesc[2]), 'comps': int(adesc[3]),
                         'norm': int(adesc[4]), 'off': int(adesc[5]),
                         'quant': False}
                    a['stride'] = attr_stride(a['type'], a['comps'])
                    base = BLOCK_BASE + a['off']
                    a['start'] = base
                    a['end'] = base + a['stride'] * vc
                    if len(adesc) >= 8 and isinstance(adesc[6], int) \
                            and isinstance(adesc[7], int):
                        a['quant'] = True
                        so = BLOCK_BASE + adesc[6]
                        bo = BLOCK_BASE + adesc[7]
                        a['scale'] = list(struct.unpack_from('<%df' % a['comps'], d, so))
                        a['bias'] = list(struct.unpack_from('<%df' % a['comps'], d, bo))
                        a['end'] = bo + 4 * a['comps']
                    sub['attrs'].append(a)
                    self.layout.append(('%s.%s' % (sub['name'], ATTR_LABEL.get(a['id'], 'ATTR')),
                                        a['start'], a['end']))
                    self.decode_attr(sub, a)

                ioff = BLOCK_BASE + sub['idxOffset']
                usable = (sub['idxCount'] // 3) * 3
                sub['indices'] = list(struct.unpack_from('<%dH' % usable, d, ioff)) \
                    if usable else []
                self.layout.append(('%s.INDICES' % sub['name'], ioff,
                                    ioff + sub['idxCount'] * 2))
                sub['bad'] = sum(1 for i in sub['indices'] if i >= vc)
                if sub['idxCount'] % 3 != 0:
                    self.warnings.append('%s: Indexzahl %d ist kein Vielfaches von 3'
                                         % (sub['name'], sub['idxCount']))
                if sub['bad']:
                    self.warnings.append(
                        '%s: %d Indices >= Vertexzahl - das ist das klassische '
                        'Zeichen fuer eine falsche Pufferbasis'
                        % (sub['name'], sub['bad']))
                subs.append(sub)
            self.groups.append(subs)

    def decode_attr(self, sub, a):
        d = self.data
        vc = sub['verts']
        base = a['start']
        stride = a['stride']

        if a['id'] == 1:
            # Attribut 1 sind zwei int16 (Hoehe + Azimut), nicht drei
            # int8. Der erklaerte Typ beschreibt nur die Ablage.
            sub['normals'] = [decode_normal(d[base + stride * v:base + stride * v + 4])
                              for v in range(vc)]
            return

        denom = normalize_denominator(a['type']) if a['norm'] == 1 else 0.0
        cs = comp_size(a['type'])

        def value(v, c):
            raw = read_component(d, base + stride * v + cs * c, a['type'])
            if denom > 0.0:
                raw /= denom
            if a['quant']:
                raw = float(f32(a['bias'][c])) + raw * float(f32(a['scale'][c]))
            return f32(raw)

        aid = a['id']
        if aid == 0:
            sub['pos'] = [(value(v, 0), value(v, 1), value(v, 2)) for v in range(vc)]
        elif aid == 2:
            sub['tan'] = [(value(v, 0), value(v, 1), value(v, 2),
                           value(v, 3) if a['comps'] > 3 else 0.0) for v in range(vc)]
        elif aid == 3:
            sub['col'] = [(value(v, 0), value(v, 1), value(v, 2),
                           value(v, 3) if a['comps'] > 3 else 1.0) for v in range(vc)]
        elif aid == 4:
            # Rohwert: ein Palettenindex ist eine Nummer, keine
            # Messgroesse.
            sub['bone'] = [tuple(d[base + stride * v + c] for c in range(4))
                           for v in range(vc)]
        elif aid == 5:
            sub['weight'] = [tuple(value(v, c) if c < a['comps'] else 0.0
                                   for c in range(4)) for v in range(vc)]
        elif aid in (6, 7):
            key = 'uv0' if aid == 6 else 'uv1'
            sub[key] = [(value(v, 0), value(v, 1)) for v in range(vc)]

    def parse_palettes(self):
        d = self.data
        self.palettes = []
        for gi, entry in enumerate(self.meta[4]):
            bones = [int(b) for b in entry[0]]
            base = BLOCK_BASE + int(entry[1])
            mats = []
            singular = 0
            for b in range(len(bones)):
                o = base + 64 * b
                values = struct.unpack_from('<16f', d, o)
                m = [list(values[0:4]), list(values[4:8]),
                     list(values[8:12]), list(values[12:16])]
                mats.append(m)
                if not mat_invertible(m):
                    singular += 1
            if singular:
                self.warnings.append(
                    'Palette %d: %d nicht invertierbare Bind-Matrix/Matrizen '
                    '(kommt im Bestand vor, z.B. 263_skin5.model)' % (gi, singular))
            self.palettes.append({'bones': bones, 'bind': mats, 'start': base,
                                  'end': base + 64 * len(bones), 'singular': singular})
            self.layout.append(('palette%d.BINDMATRICES' % gi, base,
                                base + 64 * len(bones)))

    def parse_nodes(self):
        d = self.data
        self.nodes = []
        bad_quat = 0
        for i, nd in enumerate(self.meta[5]):
            node = {'name': nd[0] if isinstance(nd[0], str) else 'node_%d' % i,
                    'T': (0.0, 0.0, 0.0), 'R': (0.0, 0.0, 0.0, 1.0),
                    'S': (1.0, 1.0, 1.0), 'hasT': 0, 'hasR': 0, 'hasS': 0,
                    'qlen': 1.0, 'parent': -1, 'mesh': -1, 'pal': -1,
                    'children': []}
            if isinstance(nd[1], int):
                node['T'] = struct.unpack_from('<3f', d, BLOCK_BASE + nd[1])
                node['hasT'] = 1
            if isinstance(nd[2], int):
                q = struct.unpack_from('<4f', d, BLOCK_BASE + nd[2])
                node['R'] = q
                length = math.sqrt(sum(float(c) * float(c) for c in q))
                node['qlen'] = f32(length)
                if abs(length - 1.0) > 0.02:
                    bad_quat += 1
                node['hasR'] = 1
            if isinstance(nd[3], int):
                s = struct.unpack_from('<3f', d, BLOCK_BASE + nd[3])
                if all(0.01 < abs(c) < 100.0 for c in s):
                    node['S'] = s
                    node['hasS'] = 1
            if len(nd) > 5 and isinstance(nd[5], list):
                node['children'] = [int(c) for c in nd[5]]
            if len(nd) > 6 and isinstance(nd[6], int):
                node['mesh'] = int(nd[6])
            if len(nd) > 7 and isinstance(nd[7], int):
                node['pal'] = int(nd[7])
            self.nodes.append(node)

        for i, node in enumerate(self.nodes):
            for c in node['children']:
                if 0 <= c < len(self.nodes):
                    self.nodes[c]['parent'] = i

        # Die Palette steht am KNOTEN, nicht an der Gruppe.
        self.instances = []
        for i, node in enumerate(self.nodes):
            if node['mesh'] < 0 or node['mesh'] >= len(self.groups):
                continue
            self.instances.append({'group': node['mesh'], 'node': i,
                                   'pal': node['pal']})
        placed = set(inst['group'] for inst in self.instances)
        self.unplaced = [g for g in range(len(self.groups)) if g not in placed]

        if bad_quat:
            self.warnings.append(
                '%d Knoten mit |q| != 1 - deutet auf eine falsche Pufferbasis hin'
                % bad_quat)

    def parse_clips(self):
        d = self.data
        self.clips = []
        for ci, cd in enumerate(self.meta[7]):
            clip = {'name': cd[0] if isinstance(cd[0], str) else 'clip_%d' % ci,
                    'flag': bool(cd[2]) if len(cd) > 2 else False,
                    'channels': [], 'duration': 0.0}
            for chd in cd[1]:
                if not isinstance(chd, list) or len(chd) < 7:
                    continue
                kind_raw = chd[1]
                if isinstance(kind_raw, bool) or not isinstance(kind_raw, int):
                    kind, kind_name = 99, str(kind_raw)
                elif 0 <= kind_raw <= 2:
                    kind, kind_name = kind_raw, ''
                else:
                    kind, kind_name = 99, str(kind_raw)

                ch = {'node': int(chd[0]), 'kind': kind, 'kindName': kind_name,
                      'f2': int(chd[2]) if isinstance(chd[2], int) else 0,
                      'keys': int(chd[3]), 'comps': int(chd[4]),
                      'tOff': int(chd[5][0]), 'tType': int(chd[5][1]),
                      'vOff': int(chd[6][0]), 'vType': int(chd[6][1]),
                      'decoded': 0, 'isBool': 0, 'times': [], 'values': []}
                if ch['keys'] < 0 or ch['comps'] <= 0:
                    continue
                t_at = BLOCK_BASE + ch['tOff']
                v_at = BLOCK_BASE + ch['vOff']
                # Wertbreite haengt am vType, nicht am kind:
                # Typ 0 = vier Byte, Typ 6 = eines.
                width = 1 if ch['vType'] == VALUE_TYPE_BOOL else 4
                if t_at + ch['keys'] * 2 > len(d) or \
                        v_at + ch['keys'] * ch['comps'] * width > len(d):
                    self.warnings.append(
                        "Clip '%s' Knoten %d: Kurve liegt ausserhalb der Datei, "
                        'uebersprungen' % (clip['name'], ch['node']))
                    continue
                deltas = struct.unpack_from('<%dH' % ch['keys'], d, t_at) \
                    if ch['keys'] else ()
                running = 0.0
                times = []
                for delta in deltas:
                    running += delta
                    times.append(running)
                ch['times'] = times
                count = ch['keys'] * ch['comps']
                if ch['vType'] == VALUE_TYPE_BOOL:
                    ch['isBool'] = 1
                    raw = struct.unpack_from('<%dB' % count, d, v_at) \
                        if count else ()
                    for value in raw:
                        if value > 1:
                            self.warnings.append(
                                "Clip '%s' Knoten %d Kanal '%s': Schaltwert %d "
                                'statt 0 oder 1'
                                % (clip['name'], ch['node'], ch['kindName'], value))
                    ch['values'] = [1 if value else 0 for value in raw]
                    ch['decoded'] = 1
                elif ch['vType'] == VALUE_TYPE_FLOAT:
                    ch['values'] = list(struct.unpack_from(
                        '<%df' % count, d, v_at)) if count else []
                    ch['decoded'] = 1
                else:
                    self.warnings.append(
                        "Clip '%s' Knoten %d Kanal '%s': unbekannter Werttyp %d, "
                        'nicht dekodiert'
                        % (clip['name'], ch['node'],
                           ch['kindName'] or str(ch['kind']), ch['vType']))
                if times:
                    clip['duration'] = max(clip['duration'], times[-1])
                clip['channels'].append(ch)
            self.clips.append(clip)

    # -- Ausgabe --------------------------------------------------------
    def write(self, out, s):
        w = out.write
        w('FILE %s format=model\n' % self.name)
        vcount = sum(sm['verts'] for g in self.groups for sm in g)
        tcount = sum(len(sm['indices']) // 3 for g in self.groups for sm in g)
        subcount = sum(len(g) for g in self.groups)

        if s.container:
            w('container size=%d version=%d mainSize=%d jsonStart=%d '
              'jsonChars=%d trailing=%d\n'
              % (len(self.data), self.version, self.main_size, self.json_start,
                 self.json_chars, self.json_trailing))
            w('header generator=%s version=%s leftHand=%d\n'
              % (self.generator or '-', self.gen_version or '-',
                 1 if self.left_hand else 0))
            w('counts materials=%d textures=%d groups=%d submeshes=%d palettes=%d '
              'nodes=%d instances=%d clips=%d verts=%d tris=%d\n'
              % (len(self.materials), len(self.textures), len(self.groups),
                 subcount, len(self.palettes), len(self.nodes),
                 len(self.instances), len(self.clips), vcount, tcount))

        if s.meta:
            for i, m in enumerate(self.materials):
                w('material %d name=%s type=%s blend=%d double=%d depthWrite=%d '
                  'depthTest=%d\n'
                  % (i, m['name'] or '-', m['type'] or '-', m['blend'],
                     1 if m['double'] else 0, 1 if m['depthWrite'] else 0,
                     1 if m['depthTest'] else 0))
                for k in sorted(m['textures']):
                    w('mattex %d %s %d\n' % (i, k, m['textures'][k]))
                for k in sorted(m['vectors']):
                    w('matvec %d %s %s\n' % (i, k, v4(m['vectors'][k])))
            for i, t in enumerate(self.textures):
                w('texture %d %s\n' % (i, t or '-'))

        for g in self.groups:
            for sm in g:
                if s.geometry:
                    w('submesh %s material=%d verts=%d idxOffset=%d idxCount=%d '
                      'attrs=%d badIdx=%d\n'
                      % (sm['name'], sm['material'], sm['verts'], sm['idxOffset'],
                         sm['idxCount'], len(sm['attrs']), sm['bad']))
                    for a in sm['attrs']:
                        line = ('attr %s %s id=%d type=%d qual=%d comps=%d norm=%d '
                                'stride=%d start=%d end=%d'
                                % (sm['name'], ATTR_LABEL.get(a['id'], 'ATTR'),
                                   a['id'], a['type'], a['qual'], a['comps'],
                                   a['norm'], a['stride'], a['start'], a['end']))
                        if a['quant']:
                            line += ' scale=' + ','.join(f(f32(x), 9) for x in a['scale'])
                            line += ' bias=' + ','.join(f(f32(x), 9) for x in a['bias'])
                        w(line + '\n')
                if s.vertices:
                    for v in range(sm['verts']):
                        parts = ['v %s %d' % (sm['name'], v)]
                        if 'pos' in sm:
                            parts.append('P ' + v3(sm['pos'][v]))
                        if 'normals' in sm:
                            parts.append('N ' + v3(sm['normals'][v]))
                        if 'tan' in sm:
                            parts.append('T ' + v4(sm['tan'][v]))
                        if 'col' in sm:
                            parts.append('C ' + v4(sm['col'][v]))
                        if 'bone' in sm:
                            parts.append('B ' + ' '.join(str(x) for x in sm['bone'][v]))
                        if 'weight' in sm:
                            parts.append('W ' + ' '.join(f(x) for x in sm['weight'][v]))
                        if 'uv0' in sm:
                            parts.append('UV0 ' + v2(sm['uv0'][v]))
                        if 'uv1' in sm:
                            parts.append('UV1 ' + v2(sm['uv1'][v]))
                        w(' '.join(parts) + '\n')
                if s.triangles:
                    idx = sm['indices']
                    for t in range(len(idx) // 3):
                        w('t %s %d %d %d %d\n' % (sm['name'], t, idx[t * 3],
                                                  idx[t * 3 + 1], idx[t * 3 + 2]))

        if s.skeleton:
            for p, palette in enumerate(self.palettes):
                w('palette %d bones=%d start=%d end=%d singular=%d nodes=%s\n'
                  % (p, len(palette['bones']), palette['start'], palette['end'],
                     palette['singular'], ilist(palette['bones'])))
                if s.bindpose:
                    for b, m in enumerate(palette['bind']):
                        w('ibm %d %d node=%d %s\n'
                          % (p, b, palette['bones'][b],
                             ' '.join(f(m[r][c]) for r in range(4) for c in range(4))))
            for i, node in enumerate(self.nodes):
                w('node %d name=%s parent=%d mesh=%d pal=%d hasT=%d hasR=%d hasS=%d '
                  'T %s R %s S %s qlen=%s children=%s\n'
                  % (i, node['name'] or '-', node['parent'], node['mesh'],
                     node['pal'], node['hasT'], node['hasR'], node['hasS'],
                     v3(node['T']), v4(node['R']), v3(node['S']),
                     f(node['qlen']), ilist(node['children'])))
            for i, inst in enumerate(self.instances):
                w('inst %d group=%d node=%d pal=%d\n'
                  % (i, inst['group'], inst['node'], inst['pal']))
            if self.unplaced:
                w('unplaced %s\n' % ilist(self.unplaced))

        if s.clips:
            for c, clip in enumerate(self.clips):
                w('clip %d name=%s flag=%d channels=%d durationMs=%s\n'
                  % (c, clip['name'] or '-', 1 if clip['flag'] else 0,
                     len(clip['channels']), f(clip['duration'])))
                if not s.animation:
                    continue
                for h, ch in enumerate(clip['channels']):
                    kind = {0: 'location', 1: 'rotation', 2: 'scale'}.get(
                        ch['kind'], ch['kindName'] or '?')
                    w('chan %d %d node=%d kind=%s f2=%d keys=%d comps=%d tOff=%d '
                      'tType=%d vOff=%d vType=%d decoded=%d bool=%d\n'
                      % (c, h, ch['node'], kind, ch['f2'], ch['keys'], ch['comps'],
                         ch['tOff'], ch['tType'], ch['vOff'], ch['vType'],
                         ch['decoded'], ch['isBool']))
                    for k in range(ch['keys']):
                        line = 'key %d %d %d %s' % (c, h, k, f(ch['times'][k], 3))
                        if ch['decoded']:
                            line += ' ' + ' '.join(
                                str(ch['values'][k * ch['comps'] + comp])
                                if ch['isBool'] else
                                f(ch['values'][k * ch['comps'] + comp])
                                for comp in range(ch['comps']))
                        w(line + '\n')

        if s.layout:
            previous = 0
            gaps = nonzero = 0
            max_gap = 0
            common = 0
            same = True
            for i, (owner, start, end) in enumerate(self.layout):
                gap = 0 if i == 0 else start - previous
                w('layout %d %s %d %d %s\n'
                  % (i, owner, start, end, '-' if i == 0 else str(gap)))
                if i > 0:
                    gaps += 1
                    max_gap = max(max_gap, gap)
                    if gap != 0:
                        nonzero += 1
                        if nonzero == 1:
                            common = gap
                        elif gap != common:
                            same = False
                previous = end
            all_same = 1 if (same and nonzero > 3) else 0
            w('layoutsum blocks=%d gaps=%d nonzero=%d maxGap=%d allSame=%d '
              'commonGap=%d\n'
              % (len(self.layout), gaps, nonzero, max_gap, all_same, common))

        if s.warnings:
            for warning in self.warnings:
                w('warn %s\n' % warning)
        w('END %s\n' % self.name)


# ===========================================================================
#  .x
# ===========================================================================

def median(values):
    """numpy-Semantik: bei gerader Anzahl das Mittel der beiden
    mittleren Werte."""
    if not values:
        return 1.0
    values = sorted(values)
    n = len(values)
    if n % 2 == 1:
        return values[n // 2]
    return 0.5 * (values[n // 2 - 1] + values[n // 2])


class XDump(object):
    def __init__(self, path, data):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.warnings = []
        self.parse(data)

    def parse(self, data):
        self.file_size = len(data)
        self.signature = cstr(data, 0, 64)

        off = 0x58
        self.tags = {}
        while off + 12 <= len(data):
            tag = data[off:off + 4]
            if not all(32 <= c < 127 for c in tag):
                break
            if struct.unpack_from('<I', data, off + 4)[0] != 4:
                break
            self.tags[tag.decode('ascii')] = struct.unpack_from('<I', data, off + 8)[0]
            off += 12
        raw1, raw2, _pk1, _pk2 = struct.unpack_from('<4I', data, off)
        off += 16

        o1 = zlib.decompressobj()
        b1 = o1.decompress(data[off:])
        consumed = len(data) - off - len(o1.unused_data)
        o2 = zlib.decompressobj()
        b2 = o2.decompress(data[off + consumed:])
        if len(b1) != raw1 or len(b2) != raw2:
            self.warnings.append(
                'entpackte Groessen weichen vom Kopf ab (%d/%d statt %d/%d)'
                % (len(b1), len(b2), raw1, raw2))
        self.b1, self.b2 = b1, b2

        t = self.tags
        nbon = t.get('nbon', 0)
        ngeo = t.get('ngeo', 0)
        nact = t.get('nact', 0)
        abon = t.get('abon', 0)
        aact = t.get('aact', 0)
        ageo = t.get('ageo', 0)

        self.clips = []
        for k in range(nact):
            base = aact + k * ACT_STRIDE
            if base + ACT_STRIDE > len(b1):
                break
            name = cstr(b1, base) or 'clip_%d' % k
            start, end = struct.unpack_from('<HH', b1, base + 80)
            self.clips.append({'name': name, 'start': start, 'end': end,
                               'synthetic': 0})

        addrs = []
        for k in range(nbon):
            base = abon + k * BONE_STRIDE
            if base + 148 > len(b1):
                break
            a = struct.unpack_from('<I', b1, base + 144)[0] - ADDR_BASE
            if not (0 <= a < len(b2)):
                raise ValueError('Bone %d hat eine unplausible Blockadresse %d' % (k, a))
            addrs.append(a)
        if len(addrs) != nbon:
            raise ValueError('nur %d von %d Bone-Deskriptoren lesbar' % (len(addrs), nbon))

        order = sorted(range(nbon), key=lambda i: addrs[i])
        sizes = [0] * nbon
        for j, i in enumerate(order):
            nxt = addrs[order[j + 1]] if j + 1 < len(order) else len(b2)
            if nxt <= addrs[i]:
                raise ValueError('Bonebloecke ueberlappen')
            sizes[i] = nxt - addrs[i]

        self.frames = self.solve_frames(sizes, addrs)
        frames = self.frames

        self.bones = []
        for k in range(nbon):
            base = abon + k * BONE_STRIDE
            name = cstr(b1, struct.unpack_from('<I', b1, base + 8)[0])
            usable = bool(name) and (name[0].isalpha() or ord(name[0]) >= 0x80)
            bone = {'name': name if usable else 'Bone_%02d' % k,
                    'parent': -1, 'children': [],
                    'bpf': sizes[k] // frames}
            at = addrs[k]
            bone['pos'] = [struct.unpack_from('<3f', b2, at + fr * 12)
                           for fr in range(frames)]
            qb = at + frames * 12
            bone['quat'] = [list(struct.unpack_from('<4f', b2, qb + fr * 16))
                            for fr in range(frames)]
            if bone['bpf'] >= SCALE_MIN_BPF:
                sb = at + frames * 28
                bone['scale'] = [list(struct.unpack_from('<3f', b2, sb + fr * 12))
                                 for fr in range(frames)]
            else:
                bone['scale'] = None
            if bone['bpf'] in (32, 44):
                fb = at + frames * (28 if bone['bpf'] == 32 else 40)
                bone['visible'] = [1 if struct.unpack_from('<I', b2, fb + fr * 4)[0]
                                   else 0 for fr in range(frames)]
            else:
                bone['visible'] = None
            self.bones.append(bone)

        self.sanitise()
        self.hierarchy()
        self.parse_meta()
        self.parse_meshes(ageo, ngeo, nbon)

        if not self.clips and frames > 1:
            moved = 0
            for bone in self.bones:
                p0, q0 = bone['pos'][0], bone['quat'][0]
                changes = False
                for fr in range(1, frames):
                    p, q = bone['pos'][fr], bone['quat'][fr]
                    if any(abs(p[c] - p0[c]) > 1e-5 for c in range(3)) or \
                            any(abs(q[c] - q0[c]) > 1e-5 for c in range(4)):
                        changes = True
                        break
                if changes:
                    moved += 1
            if moved:
                self.clips.append({'name': 'full_timeline', 'start': 0,
                                   'end': frames - 1, 'synthetic': 1})
                self.warnings.append(
                    "die Datei deklariert keine Clips, aber %d von %d Bones bewegen "
                    "sich - ein synthetischer Clip 'full_timeline' deckt die ganze "
                    'Zeitleiste ab' % (moved, len(self.bones)))

        for clip in self.clips:
            if clip['end'] >= self.frames:
                self.warnings.append("Clip '%s' laeuft ueber das Ende der "
                                     'Zeitleiste hinaus' % clip['name'])

    def solve_frames(self, sizes, addrs):
        floor = max((c['end'] for c in self.clips), default=-1) + 1
        smallest = min(sizes)
        candidates = []
        for bpf in BYTES_PER_FRAME:
            if smallest == 0 or smallest % bpf:
                continue
            frames = smallest // bpf
            if frames == 0 or frames < floor:
                continue
            if not all(s % frames == 0 and (s // frames) in BYTES_PER_FRAME
                       for s in sizes):
                continue
            share, dev = self.quat_score(addrs, frames)
            candidates.append((dev, -share, frames, bpf))
        if not candidates:
            raise ValueError('Framezahl laesst sich nicht bestimmen '
                             '(Blockgroessen passen zu keiner bekannten '
                             'Frameaufteilung)')
        candidates.sort(key=lambda c: (c[0], c[1]))
        dev, negshare, frames, _bpf = candidates[0]
        self.dev = dev
        self.unit = -negshare
        self.rejected = [c[2] for c in candidates[1:]]
        if dev > 0.005:
            self.warnings.append(
                'beste Framezahl %d hat noch eine Medianabweichung von %s - die '
                'Boneblock-Aufteilung passt fuer diese Datei nicht'
                % (frames, f(dev)))
        return frames

    def quat_score(self, addrs, frames):
        b2 = self.b2
        devs = []
        unit = 0
        total = 0
        step = max(1, len(addrs) // 24)
        for k in range(0, len(addrs), step):
            at = addrs[k] + frames * 12
            if at + frames * 16 > len(b2):
                continue
            for fr in range(frames):
                q = struct.unpack_from('<4f', b2, at + fr * 16)
                length = math.sqrt(sum(float(c) * float(c) for c in q))
                if not math.isfinite(length):
                    continue
                d = abs(length - 1.0)
                devs.append(d)
                if d < 1e-3:
                    unit += 1
                total += 1
        if not devs:
            return 0.0, 1.0
        return unit / float(total), median(devs)

    def sanitise(self):
        self.nan_bones = self.nan_frames = 0
        for bone in self.bones:
            frames = len(bone['pos'])
            bad = []
            for fr in range(frames):
                broken = not all(math.isfinite(c) for c in bone['pos'][fr]) or \
                    not all(math.isfinite(c) for c in bone['quat'][fr])
                if not broken and bone['scale'] is not None:
                    broken = not all(math.isfinite(c) for c in bone['scale'][fr])
                bad.append(broken)
            good = [i for i in range(frames) if not bad[i]]
            count = frames - len(good)
            if count == 0:
                continue
            self.nan_bones += 1
            self.nan_frames += count
            if not good:
                bone['pos'] = [(0.0, 0.0, 0.0)] * frames
                bone['quat'] = [[0.0, 0.0, 0.0, 1.0] for _ in range(frames)]
                if bone['scale'] is not None:
                    bone['scale'] = [[1.0, 1.0, 1.0] for _ in range(frames)]
                continue
            pos_copy = list(bone['pos'])
            quat_copy = list(bone['quat'])
            scale_copy = list(bone['scale']) if bone['scale'] is not None else None
            import bisect
            for fr in range(frames):
                if not bad[fr]:
                    continue
                slot = bisect.bisect_left(good, fr)
                if slot >= len(good):
                    slot = len(good) - 1
                src = good[slot]
                bone['pos'][fr] = pos_copy[src]
                bone['quat'][fr] = quat_copy[src]
                if scale_copy is not None:
                    bone['scale'][fr] = scale_copy[src]
        if self.nan_bones:
            self.warnings.append(
                '%d Bone(s) mit NaN in %d Frames (beschaedigte Quelldaten) - durch '
                'den naechsten gueltigen Frame ersetzt'
                % (self.nan_bones, self.nan_frames))

        self.denorm_bones = self.denorm_keys = 0
        for bone in self.bones:
            any_bad = False
            for q in bone['quat']:
                length = math.sqrt(sum(float(c) * float(c) for c in q))
                if abs(length - 1.0) > 1e-3:
                    any_bad = True
                    self.denorm_keys += 1
            if not any_bad:
                continue
            self.denorm_bones += 1
            for q in bone['quat']:
                length = math.sqrt(sum(float(c) * float(c) for c in q))
                inv = 1.0 / max(length, 1e-9)
                for c in range(4):
                    q[c] = f32(float(q[c]) * inv)
        if self.denorm_bones:
            self.warnings.append(
                '%d Bone(s) mit nicht normierten Rotationen in %d Frames - normiert'
                % (self.denorm_bones, self.denorm_keys))

    def hierarchy(self):
        b1 = self.b1
        abon = self.tags.get('abon', 0)
        n = len(self.bones)
        self.links = 0

        def would_cycle(parent, child):
            p, guard = parent, 0
            while p >= 0 and guard < 512:
                if p == child:
                    return True
                p = self.bones[p]['parent']
                guard += 1
            return False

        for k in range(n):
            base = abon + k * BONE_STRIDE
            if base + 12 > len(b1):
                continue
            noff = struct.unpack_from('<I', b1, base + 8)[0]
            if noff >= len(b1):
                continue
            p = b1.find(b'\x00', noff)
            if p < 0:
                continue
            p += 1
            while p + 4 <= len(b1):
                w = b1[p:p + 4]
                if w[1] or w[2] or w[3]:
                    break
                child = w[0]
                if child >= n or child == 0 or would_cycle(k, child):
                    break
                self.bones[k]['children'].append(child)
                if self.bones[child]['parent'] < 0:
                    self.bones[child]['parent'] = k
                    self.links += 1
                p += 4

    def parse_meta(self):
        b1 = self.b1
        t = self.tags
        self.textures = []
        for i in range(t.get('ntex', 0)):
            base = t.get('atex', 0) + i * TEX_STRIDE
            if base + 8 > len(b1):
                break
            name = cstr(b1, struct.unpack_from('<I', b1, base + 4)[0])
            if name:
                self.textures.append(name)
            else:
                self.warnings.append('Textur %d: Namensoffset nicht lesbar' % i)
        self.materials = []
        for i in range(t.get('nmtl', 0)):
            base = t.get('amtl', 0) + i * MTL_STRIDE
            if base + MTL_STRIDE > len(b1):
                break
            tex = struct.unpack_from('<I', b1, base + 12)[0]
            self.materials.append(tex if tex < len(self.textures) else -1)

    def parse_meshes(self, ageo, ngeo, nbon):
        b1, b2 = self.b1, self.b2
        self.meshes = []
        for g in range(ngeo):
            base = ageo + g * GEO_STRIDE
            if base + GEO_STRIDE > len(b1):
                break
            noff = struct.unpack_from('<I', b1, base + 8)[0]
            material = struct.unpack_from('<I', b1, base + 16)[0]
            vc, tc = struct.unpack_from('<II', b1, base + 28)

            def addr(field):
                v = struct.unpack_from('<I', b1, base + field)[0] - ADDR_BASE
                return v if 0 <= v < len(b2) else -1

            a_pos, a_nrm, a_uv = addr(36), addr(44), addr(52)
            a_byte, a_idx, a_skin = addr(68), addr(76), addr(92)
            if a_pos < 0:
                if vc == 0 or tc == 0:
                    self.warnings.append('Mesh %d ist leer, uebersprungen' % g)
                else:
                    self.warnings.append('Mesh %d hat keine brauchbare '
                                         'Positionsadresse, uebersprungen' % g)
                continue
            if a_nrm < 0:
                a_nrm = a_pos + vc * 12
            if a_uv < 0:
                a_uv = a_nrm + vc * 12
            if a_byte < 0:
                a_byte = a_uv + vc * 8
            if a_idx < 0:
                a_idx = a_byte + vc
            if a_skin < 0:
                a_skin = a_idx + tc * 6

            mesh = {'name': cstr(b1, noff) or 'mesh_%d' % g,
                    'material': material, 'verts': vc, 'declaredTris': tc,
                    'dropped': 0, 'cleared': 0}
            mesh['pos'] = [struct.unpack_from('<3f', b2, a_pos + v * 12)
                           for v in range(vc)]
            mesh['nrm'] = [struct.unpack_from('<3f', b2, a_nrm + v * 12)
                           for v in range(vc)]
            mesh['uv'] = [struct.unpack_from('<2f', b2, a_uv + v * 8)
                          for v in range(vc)]
            mesh['pal'] = [b2[a_byte + v] for v in range(vc)]
            bones, weights = [], []
            for v in range(vc):
                s = a_skin + v * 24
                idx = list(b2[s + 1:s + 5])
                wgt = list(struct.unpack_from('<4f', b2, s + 8))
                for slot in range(4):
                    if idx[slot] >= nbon:
                        mesh['cleared'] += 1
                        idx[slot] = 0
                        wgt[slot] = 0.0
                bones.append(idx)
                weights.append(wgt)
            mesh['bone'] = bones
            mesh['weight'] = weights

            indices = []
            for t in range(tc):
                a, b, c = struct.unpack_from('<3H', b2, a_idx + t * 6)
                if a >= vc or b >= vc or c >= vc:
                    mesh['dropped'] += 1
                    continue
                indices.extend((a, b, c))
            mesh['indices'] = indices

            if mesh['dropped']:
                self.warnings.append(
                    "Mesh %d '%s': %d von %d Dreiecken verweisen auf nicht "
                    'vorhandene Vertices (0xFFFF-Fuellung), verworfen'
                    % (g, mesh['name'], mesh['dropped'], tc))
            if mesh['cleared']:
                self.warnings.append(
                    "Mesh %d '%s': %d Skin-Plaetze zeigen hinter die Bone-Liste, "
                    'Gewichte genullt' % (g, mesh['name'], mesh['cleared']))
            if not indices:
                self.warnings.append("Mesh %d '%s' hat keine brauchbaren Dreiecke, "
                                     'uebersprungen' % (g, mesh['name']))
                continue
            self.meshes.append(mesh)

    def write(self, out, s):
        w = out.write
        w('FILE %s format=x\n' % self.name)
        vcount = sum(m['verts'] for m in self.meshes)
        tcount = sum(len(m['indices']) // 3 for m in self.meshes)

        if s.container:
            w('xcontainer size=%d sig=%s block1=%d block2=%d frames=%d dev=%s '
              'unit=%s rejected=%s\n'
              % (self.file_size, self.signature or '-', len(self.b1), len(self.b2),
                 self.frames, f(self.dev, 9), f(self.unit, 6), ilist(self.rejected)))
            w('xcounts textures=%d materials=%d meshes=%d bones=%d clips=%d '
              'verts=%d tris=%d links=%d nanBones=%d nanFrames=%d denormBones=%d '
              'denormKeys=%d\n'
              % (len(self.textures), len(self.materials), len(self.meshes),
                 len(self.bones), len(self.clips), vcount, tcount, self.links,
                 self.nan_bones, self.nan_frames, self.denorm_bones,
                 self.denorm_keys))

        if s.meta:
            for k in sorted(self.tags):
                w('tag %s %d\n' % (k, self.tags[k]))
            for i, t in enumerate(self.textures):
                w('xtexture %d %s\n' % (i, t))
            for i, m in enumerate(self.materials):
                w('xmaterial %d texture=%d\n' % (i, m))

        for mi, mesh in enumerate(self.meshes):
            if s.geometry:
                w('xmesh %d name=%s material=%d verts=%d tris=%d declaredTris=%d '
                  'dropped=%d cleared=%d\n'
                  % (mi, mesh['name'] or '-', mesh['material'], mesh['verts'],
                     len(mesh['indices']) // 3, mesh['declaredTris'],
                     mesh['dropped'], mesh['cleared']))
            if s.vertices:
                for v in range(mesh['verts']):
                    w('xv %d %d P %s N %s UV %s PAL %d B %s W %s\n'
                      % (mi, v, v3(mesh['pos'][v]), v3(mesh['nrm'][v]),
                         v2(mesh['uv'][v]), mesh['pal'][v],
                         ' '.join(str(x) for x in mesh['bone'][v]),
                         ' '.join(f(x) for x in mesh['weight'][v])))
            if s.triangles:
                idx = mesh['indices']
                for t in range(len(idx) // 3):
                    w('xt %d %d %d %d %d\n' % (mi, t, idx[t * 3], idx[t * 3 + 1],
                                               idx[t * 3 + 2]))

        if s.skeleton:
            for i, bone in enumerate(self.bones):
                w('xbone %d name=%s parent=%d bpf=%d scale=%d vis=%d children=%s\n'
                  % (i, bone['name'] or '-', bone['parent'], bone['bpf'],
                     1 if bone['scale'] is not None else 0,
                     1 if bone['visible'] is not None else 0,
                     ilist(bone['children'])))

        if s.animation:
            for i, bone in enumerate(self.bones):
                for fr in range(len(bone['pos'])):
                    line = ('xkey %d %d P %s Q %s'
                            % (i, fr, v3(bone['pos'][fr]), v4(bone['quat'][fr])))
                    if bone['scale'] is not None:
                        line += ' S ' + v3(bone['scale'][fr])
                    if bone['visible'] is not None:
                        line += ' V %d' % bone['visible'][fr]
                    w(line + '\n')

        if s.clips:
            for i, clip in enumerate(self.clips):
                w('xclip %d name=%s start=%d end=%d synthetic=%d\n'
                  % (i, clip['name'] or '-', clip['start'], clip['end'],
                     clip['synthetic']))

        if s.warnings:
            for warning in self.warnings:
                w('warn %s\n' % warning)
        w('END %s\n' % self.name)


# ===========================================================================

def main(argv):
    inputs = []
    sections = Sections(False)
    output = None
    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == '--all':
            sections = Sections(True)
        elif arg == '--summary':
            sections = Sections(False)
        elif arg == '--verts':
            sections.vertices = True
        elif arg == '--tris':
            sections.triangles = True
        elif arg == '--bind':
            sections.bindpose = True
        elif arg == '--anim':
            sections.animation = True
        elif arg == '--layout':
            sections.layout = True
        elif arg in ('--threads', '--time'):
            # Nur damit derselbe Aufruf fuer beide Werkzeuge geht.
            if arg == '--threads':
                i += 1
        elif arg == '-o' and i + 1 < len(argv):
            i += 1
            output = argv[i]
        elif arg.startswith('-'):
            sys.stderr.write('unbekannte Option: %s\n' % arg)
            return 2
        else:
            inputs.append(arg)
        i += 1

    if not inputs:
        sys.stderr.write(__doc__)
        return 2

    files = []
    for item in inputs:
        if os.path.isdir(item):
            for name in sorted(os.listdir(item)):
                full = os.path.join(item, name)
                if os.path.isfile(full) and \
                        (name.lower().endswith('.model') or name.lower().endswith('.x')):
                    files.append(full)
        else:
            files.append(item)

    stream = open(output, 'w', encoding='utf-8', newline='\n') if output \
        else sys.stdout
    failed = 0
    try:
        for path in files:
            with open(path, 'rb') as handle:
                data = handle.read()
            try:
                if data[:4] == b'EG3D':
                    ModelDump(path, data).write(stream, sections)
                elif data[:5] == b'JUMPX':
                    XDump(path, data).write(stream, sections)
                else:
                    sys.stderr.write('UEBERSPRUNGEN %s: weder EG3D noch JUMPX\n' % path)
            except Exception as exc:
                sys.stderr.write('FEHLER %s: %s\n' % (path, exc))
                failed += 1
    finally:
        if output:
            stream.close()
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
