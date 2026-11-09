# -*- coding: utf-8 -*-
"""Gegenprobe Max-Parser (eg3ddump.exe --all) gegen Blender-Parser (eg3d_parse / eg3d_x).

    python vergleiche_max_blender.py <eg3ddump.exe> <blender-quellordner> <modellordner> [-o bericht.txt]

Geht rekursiv durch den Modellordner. Je Datei wird der C++-Volldump
eingelesen und numerisch (nicht zeichenweise) gegen die Arrays des
Blender-Parsers verglichen: jeder Vertex (alle Attribute), jedes Dreieck,
jede inverse Bindmatrix, jeder Node, jeder Animationskey.
"""
import os
import subprocess
import sys
import tempfile
import traceback
from collections import defaultdict

import numpy as np


class Log(object):
    def __init__(self):
        self.lines = []

    def _p(self, lvl, fmt, *a):
        try:
            msg = fmt % a if a else fmt
        except Exception:
            msg = "%s %s" % (fmt, a)
        self.lines.append((lvl, msg))

    def info(self, f, *a): self._p("INFO", f, *a)
    def warn(self, f, *a): self._p("WARN", f, *a)
    warning = warn
    def error(self, f, *a): self._p("ERROR", f, *a)
    def debug(self, f, *a): pass
    def trace(self, f, *a): pass


def close(a, b, rel=2e-6, abs_=2.5e-6):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if a.shape != b.shape:
        return False, "Form %s != %s" % (a.shape, b.shape)
    if a.size == 0:
        return True, ""
    tol = abs_ + rel * np.maximum(np.abs(a), np.abs(b))
    d = np.abs(a - b)
    bad = d > tol
    if bad.any():
        i = np.unravel_index(np.argmax(d - tol), d.shape)
        return False, "%d Werte weichen ab, max %.6g bei %s (max=%r blender=%r)" % (
            int(bad.sum()), float(d[i]), tuple(int(x) for x in i), float(a[i]), float(b[i]))
    return True, ""


def run_dump(exe, path):
    fd, out = tempfile.mkstemp(suffix=".txt")
    os.close(fd)
    try:
        r = subprocess.run([exe, path, "--all", "-o", out], capture_output=True)
        with open(out, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        return r.returncode, r.stderr.decode("utf-8", "replace"), text
    finally:
        os.remove(out)


def kv(tokens):
    d = {}
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            d[k] = v
    return d


# --------------------------------------------------------------------------
#  .model
# --------------------------------------------------------------------------

def parse_cpp_model(text):
    sub = {}
    verts = defaultdict(list)
    tris = defaultdict(list)
    ibm = defaultdict(list)
    pal = {}
    nodes = {}
    clips = {}
    keys = defaultdict(list)   # (clip, chan) -> list of [t, v...]
    chans = {}
    counts = {}
    for line in text.splitlines():
        tk = line.split(" ")
        tag = tk[0]
        if tag == "v":
            name = tk[1]
            rec = {}
            cur = None
            for t in tk[3:]:
                if t in ("P", "N", "T", "C", "B", "W", "UV0", "UV1"):
                    cur = t
                    rec[cur] = []
                else:
                    rec[cur].append(float(t))
            verts[name].append(rec)
        elif tag == "t":
            tris[tk[1]].append([int(x) for x in tk[3:6]])
        elif tag == "submesh":
            sub[tk[1]] = kv(tk[2:])
        elif tag == "palette":
            pal[int(tk[1])] = kv(tk[2:])
        elif tag == "ibm":
            ibm[int(tk[1])].append([float(x) for x in tk[4:20]])
        elif tag == "node":
            d = kv(tk[2:])
            i = tk.index("T")
            d["T"] = [float(x) for x in tk[i + 1:i + 4]]
            d["R"] = [float(x) for x in tk[i + 5:i + 9]]
            d["S"] = [float(x) for x in tk[i + 10:i + 13]]
            # Name kann Leerzeichen enthalten
            n0 = line.find(" name=") + 6
            n1 = line.find(" parent=")
            d["name"] = line[n0:n1]
            nodes[int(tk[1])] = d
        elif tag == "clip":
            d = kv(tk[2:])
            n0 = line.find(" name=") + 6
            n1 = line.find(" flag=")
            d["name"] = line[n0:n1]
            clips[int(tk[1])] = d
        elif tag == "chan":
            chans[(int(tk[1]), int(tk[2]))] = kv(tk[3:])
        elif tag == "key":
            keys[(int(tk[1]), int(tk[2]))].append([float(x) for x in tk[4:]])
        elif tag == "counts":
            counts = kv(tk[1:])
    return dict(sub=sub, verts=verts, tris=tris, pal=pal, ibm=ibm, nodes=nodes,
                clips=clips, chans=chans, keys=keys, counts=counts)


ATTR_KEY = {0: "P", 2: "T", 3: "C", 4: "B", 5: "W", 6: "UV0", 7: "UV1"}


def compare_model(path, cpp, bl_parse, errs, stats):
    log = Log()
    m = bl_parse.parse_model(path, log=log)
    stats["bl_warn"] = [x for x in log.lines if x[0] in ("WARN", "ERROR")]
    c = cpp
    subs = m.all_submeshes()
    stats["meshes"] = len(subs)
    stats["verts"] = sum(sm.vertex_count for _, sm in subs)
    stats["tris"] = sum(len(sm.triangles) if sm.triangles is not None else 0 for _, sm in subs)
    stats["bones"] = len(m.nodes)
    stats["clips"] = len(m.clips)
    if len(subs) != len(c["sub"]):
        errs.append("Submeshes: max=%d blender=%d" % (len(c["sub"]), len(subs)))
    for gi, sm in subs:
        name = sm.name
        if name not in c["sub"]:
            errs.append("%s fehlt im Max-Dump" % name)
            continue
        cv = c["verts"].get(name, [])
        if len(cv) != sm.vertex_count:
            errs.append("%s Vertexzahl max=%d blender=%d" % (name, len(cv), sm.vertex_count))
            continue
        if int(c["sub"][name].get("material", -1)) != sm.material:
            errs.append("%s Material max=%s blender=%d" % (name, c["sub"][name].get("material"), sm.material))
        for aid, key in ATTR_KEY.items():
            have_bl = aid in sm.attrs
            have_c = bool(cv) and key in cv[0]
            if have_bl != have_c:
                errs.append("%s Attribut %s: max=%s blender=%s" % (name, key, have_c, have_bl))
                continue
            if not have_bl:
                continue
            a = np.array([r[key] for r in cv])
            b = np.asarray(sm.attrs[aid], dtype=np.float64).reshape(len(cv), -1)
            if a.shape != b.shape and a.shape[0] == b.shape[0]:
                b = b[:, :a.shape[1]]
            ok, why = close(a, b)
            if not ok:
                errs.append("%s %s: %s" % (name, key, why))
        if sm.normals is not None and cv and "N" in cv[0]:
            a = np.array([r["N"] for r in cv])
            ok, why = close(a, sm.normals)
            if not ok:
                errs.append("%s N: %s" % (name, why))
        ct = np.array(c["tris"].get(name, []), dtype=np.int64).reshape(-1, 3)
        bt = np.asarray(sm.triangles if sm.triangles is not None else np.zeros((0, 3)), dtype=np.int64).reshape(-1, 3)
        if ct.shape != bt.shape or not np.array_equal(ct, bt):
            errs.append("%s Dreiecke: max=%d blender=%d gleich=%s" % (
                name, len(ct), len(bt), ct.shape == bt.shape and np.array_equal(ct, bt)))
    # Paletten + inverse Bindmatrizen
    if len(m.palettes) != len(c["pal"]):
        errs.append("Paletten max=%d blender=%d" % (len(c["pal"]), len(m.palettes)))
    for pi, p in enumerate(m.palettes):
        if pi not in c["pal"]:
            continue
        cn = c["pal"][pi].get("nodes", "-")
        cn = [] if cn == "-" else [int(x) for x in cn.split(",")]
        if list(p["bones"]) != cn:
            errs.append("Palette %d Bones max=%s blender=%s" % (pi, cn[:8], list(p["bones"])[:8]))
        a = np.array(c["ibm"].get(pi, []), dtype=np.float64).reshape(-1, 16)
        b = np.array([np.asarray(x, dtype=np.float64).reshape(16) for x in p["bind"]]).reshape(-1, 16)
        ok, why = close(a, b, rel=5e-6, abs_=5e-6)
        if not ok:
            okT, _ = close(a, np.array([x.reshape(4, 4).T.reshape(16) for x in b]).reshape(-1, 16), rel=5e-6, abs_=5e-6)
            errs.append("Palette %d Bindmatrizen: %s%s" % (pi, why, " (transponiert gleich)" if okT else ""))
    # Nodes
    if len(m.nodes) != len(c["nodes"]):
        errs.append("Nodes max=%d blender=%d" % (len(c["nodes"]), len(m.nodes)))
    nbad = 0
    for n in m.nodes:
        cn = c["nodes"].get(n.index)
        if cn is None:
            continue
        msgs = []
        if cn["name"] != n.name:
            msgs.append("Name %r/%r" % (cn["name"], n.name))
        if int(cn["parent"]) != n.parent:
            msgs.append("Parent %s/%d" % (cn["parent"], n.parent))
        for key, val in (("T", n.translation), ("R", n.rotation), ("S", n.scale)):
            ok, why = close(cn[key], val, rel=5e-6, abs_=5e-6)
            if not ok:
                msgs.append("%s %s/%s" % (key, cn[key], list(val)))
        if msgs:
            nbad += 1
            if nbad <= 5:
                errs.append("Node %d: %s" % (n.index, "; ".join(msgs)))
    if nbad > 5:
        errs.append("... %d Nodes insgesamt abweichend" % nbad)
    # Clips + Keys
    if len(m.clips) != len(c["clips"]):
        errs.append("Clips max=%d blender=%d" % (len(c["clips"]), len(m.clips)))
    kbad = 0
    nkeys = 0
    for ci, clip in enumerate(m.clips):
        cc = c["clips"].get(ci)
        if cc is None:
            continue
        if cc["name"] != clip.name:
            errs.append("Clip %d Name %r/%r" % (ci, cc["name"], clip.name))
        if int(cc["channels"]) != len(clip.channels):
            errs.append("Clip %d Kanaele max=%s blender=%d" % (ci, cc["channels"], len(clip.channels)))
            continue
        for hi, ch in enumerate(clip.channels):
            ck = c["keys"].get((ci, hi), [])
            ch_c = c["chans"].get((ci, hi), {})
            if int(ch_c.get("node", -9)) != ch.node:
                kbad += 1
                if kbad <= 5:
                    errs.append("Clip %d Kanal %d Node max=%s blender=%d" % (ci, hi, ch_c.get("node"), ch.node))
                continue
            if ch.values is None:
                if ck:
                    kbad += 1
                continue
            nkeys += len(ck)
            a = np.array(ck, dtype=np.float64)
            vals = np.asarray(ch.values, dtype=np.float64).reshape(len(ch.times_ms), -1)
            b = np.column_stack([np.asarray(ch.times_ms, dtype=np.float64), vals]) if len(vals) else np.zeros((0, 1))
            ok, why = close(a, b, rel=5e-6, abs_=1e-3 if False else 5e-6) if a.shape == b.shape else (False, "Form %s != %s" % (a.shape, b.shape))
            if not ok:
                # Zeiten werden mit 3 Nachkommastellen gedruckt
                if a.shape == b.shape:
                    ok1, _ = close(a[:, 0], b[:, 0], rel=0, abs_=6e-4)
                    ok2, why2 = close(a[:, 1:], b[:, 1:], rel=5e-6, abs_=5e-6)
                    ok, why = ok1 and ok2, why2 if not ok2 else "Zeiten"
            if not ok:
                kbad += 1
                if kbad <= 5:
                    errs.append("Clip %d (%s) Kanal %d: %s" % (ci, clip.name, hi, why))
    stats["keys"] = nkeys
    if kbad > 5:
        errs.append("... %d Kanaele insgesamt abweichend" % kbad)


# --------------------------------------------------------------------------
#  .x
# --------------------------------------------------------------------------

def parse_cpp_x(text):
    meshes = {}
    verts = defaultdict(list)
    tris = defaultdict(list)
    bones = {}
    clips = {}
    keys = defaultdict(list)
    counts = {}
    warns = []
    mats = {}
    for line in text.splitlines():
        tk = line.split(" ")
        tag = tk[0]
        if tag == "xv":
            rec = {}
            cur = None
            for t in tk[3:]:
                if t in ("P", "N", "UV", "PAL", "B", "W"):
                    cur = t
                    rec[cur] = []
                else:
                    rec[cur].append(float(t))
            verts[int(tk[1])].append(rec)
        elif tag == "xt":
            tris[int(tk[1])].append([int(x) for x in tk[3:6]])
        elif tag == "xmesh":
            d = kv(tk[2:])
            n0 = line.find(" name=") + 6
            n1 = line.find(" material=")
            d["name"] = line[n0:n1]
            meshes[int(tk[1])] = d
        elif tag == "xbone":
            d = kv(tk[2:])
            n0 = line.find(" name=") + 6
            n1 = line.find(" parent=")
            d["name"] = line[n0:n1]
            bones[int(tk[1])] = d
        elif tag == "xclip":
            d = kv(tk[2:])
            n0 = line.find(" name=") + 6
            n1 = line.find(" start=")
            d["name"] = line[n0:n1]
            clips[int(tk[1])] = d
        elif tag == "xkey":
            rec = {}
            cur = None
            for t in tk[3:]:
                if t in ("P", "Q", "S", "V"):
                    cur = t
                    rec[cur] = []
                else:
                    rec[cur].append(float(t))
            keys[int(tk[1])].append(rec)
        elif tag == "xcounts":
            counts = kv(tk[1:])
        elif tag == "warn":
            warns.append(line[5:])
        elif tag == "xmaterial":
            mats[int(tk[1])] = kv(tk[2:])
    return dict(meshes=meshes, verts=verts, tris=tris, bones=bones, clips=clips,
                keys=keys, counts=counts, warns=warns, mats=mats)


def compare_x(path, cpp, bl_x, errs, stats):
    log = Log()
    m = bl_x.parse_jumpx(path, log=log)
    stats["bl_warn"] = [x for x in log.lines if x[0] in ("WARN", "ERROR")]
    c = cpp
    stats["meshes"] = len(m.meshes)
    stats["verts"] = sum(len(x.verts) if x.verts is not None else 0 for x in m.meshes)
    stats["tris"] = sum(len(x.tris) if x.tris is not None else 0 for x in m.meshes)
    stats["bones"] = len(m.bones)
    stats["clips"] = len(m.clips)
    stats["frames"] = m.frames
    if len(m.meshes) != len(c["meshes"]):
        errs.append("Meshes max=%d blender=%d" % (len(c["meshes"]), len(m.meshes)))
    for mi, mesh in enumerate(m.meshes):
        cm = c["meshes"].get(mi)
        if cm is None:
            continue
        if cm["name"] != mesh.name:
            errs.append("Mesh %d Name %r/%r" % (mi, cm["name"], mesh.name))
        if int(cm["material"]) != mesh.material:
            errs.append("Mesh %d Material max=%s blender=%d" % (mi, cm["material"], mesh.material))
        cv = c["verts"].get(mi, [])
        nv = len(mesh.verts) if mesh.verts is not None else 0
        if len(cv) != nv:
            errs.append("Mesh %d Vertexzahl max=%d blender=%d" % (mi, len(cv), nv))
            continue
        for key, arr in (("P", mesh.verts), ("N", mesh.normals), ("UV", mesh.uvs),
                         ("B", mesh.bone_idx), ("W", mesh.weights)):
            have_c = bool(cv) and key in cv[0]
            have_b = arr is not None
            if have_c != have_b:
                errs.append("Mesh %d %s: max=%s blender=%s" % (mi, key, have_c, have_b))
                continue
            if not have_b or not cv:
                continue
            a = np.array([r[key] for r in cv], dtype=np.float64)
            b = np.asarray(arr, dtype=np.float64).reshape(len(cv), -1)
            ok, why = close(a, b)
            if not ok:
                errs.append("Mesh %d %s: %s" % (mi, key, why))
        if mesh.palette is not None and cv and "PAL" in cv[0]:
            a = np.array([r["PAL"] for r in cv], dtype=np.float64).reshape(-1)
            b = np.asarray(mesh.palette, dtype=np.float64).reshape(-1)
            if a.shape != b.shape:
                b = np.full(a.shape, float(b[0]) if b.size else -1)
            ok, why = close(a, b)
            if not ok:
                errs.append("Mesh %d PAL: %s" % (mi, why))
        ct = np.array(c["tris"].get(mi, []), dtype=np.int64).reshape(-1, 3)
        bt = np.asarray(mesh.tris if mesh.tris is not None else np.zeros((0, 3)), dtype=np.int64).reshape(-1, 3)
        if ct.shape != bt.shape or not np.array_equal(ct, bt):
            errs.append("Mesh %d Dreiecke: max=%d blender=%d" % (mi, len(ct), len(bt)))
    if len(m.bones) != len(c["bones"]):
        errs.append("Bones max=%d blender=%d" % (len(c["bones"]), len(m.bones)))
    bbad = 0
    nkeys = 0
    for b in m.bones:
        cb = c["bones"].get(b.index)
        if cb is None:
            continue
        msgs = []
        if cb["name"] != b.name:
            msgs.append("Name %r/%r" % (cb["name"], b.name))
        if int(cb["parent"]) != b.parent:
            msgs.append("Parent %s/%d" % (cb["parent"], b.parent))
        ck = c["keys"].get(b.index, [])
        nf = len(b.pos) if b.pos is not None else 0
        if len(ck) != nf:
            msgs.append("Frames max=%d blender=%d" % (len(ck), nf))
        elif nf:
            nkeys += nf
            for key, arr in (("P", b.pos), ("Q", b.quat), ("S", b.scale)):
                if arr is None:
                    if "S" == key and "S" in ck[0]:
                        a = np.array([r["S"] for r in ck])
                        if not np.allclose(a, 1.0, atol=1e-6):
                            msgs.append("S nur in Max (nicht 1)")
                    continue
                if key not in ck[0]:
                    msgs.append("%s fehlt in Max" % key)
                    continue
                a = np.array([r[key] for r in ck], dtype=np.float64)
                bb = np.asarray(arr, dtype=np.float64).reshape(nf, -1)
                ok, why = close(a, bb, rel=5e-6, abs_=5e-6)
                if not ok and key == "Q":
                    ok2, _ = close(np.abs(a), np.abs(bb), rel=5e-6, abs_=5e-6)
                    if ok2:
                        # q und -q sind dieselbe Drehung
                        dots = np.sum(a * bb, axis=1)
                        if np.all(np.abs(np.abs(dots) - 1) < 1e-4):
                            ok = True
                if not ok:
                    msgs.append("%s: %s" % (key, why))
            if b.visible is not None:
                if "V" in ck[0]:
                    a = np.array([r["V"][0] for r in ck])
                    if not np.array_equal(a.astype(bool), np.asarray(b.visible).astype(bool)):
                        msgs.append("Sichtbarkeit weicht ab")
                else:
                    msgs.append("Sichtbarkeit nur in Blender")
        if msgs:
            bbad += 1
            if bbad <= 5:
                errs.append("Bone %d (%s): %s" % (b.index, b.name, "; ".join(msgs)))
    if bbad > 5:
        errs.append("... %d Bones insgesamt abweichend" % bbad)
    stats["keys"] = nkeys
    real = [cl for cl in m.clips if not cl.synthetic]
    cclips = [v for k, v in sorted(c["clips"].items()) if v.get("synthetic") != "1"]
    if len(real) != len(cclips):
        errs.append("Clips max=%d blender=%d" % (len(cclips), len(real)))
    else:
        for i, (a, b) in enumerate(zip(cclips, real)):
            if a["name"] != b.name or int(a["start"]) != b.start or int(a["end"]) != b.end:
                errs.append("Clip %d max=%s %s-%s blender=%s %d-%d" % (
                    i, a["name"], a["start"], a["end"], b.name, b.start, b.end))


def main():
    exe, blsrc, root = sys.argv[1:4]
    out = None
    if "-o" in sys.argv:
        out = sys.argv[sys.argv.index("-o") + 1]
    sys.path.insert(0, blsrc)
    import eg3d_parse as bl_parse
    import eg3d_x as bl_x
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith((".model", ".x")):
                files.append(os.path.join(d, f))
    files.sort()
    report = []
    total_bad = 0
    for p in files:
        rel = os.path.relpath(p, root)
        with open(p, "rb") as fh:
            magic = fh.read(5)
        kind = "model" if magic[:4] == b"EG3D" else "x" if magic == b"JUMPX" else None
        if kind is None:
            report.append("SKIP  %s (keine Signatur, %d Byte)" % (rel, os.path.getsize(p)))
            continue
        rc, err, text = run_dump(exe, p)
        errs = []
        stats = {}
        if rc != 0 or not text.strip():
            errs.append("Max-Parser rc=%d: %s" % (rc, err.strip()[:300]))
        try:
            if kind == "model":
                cpp = parse_cpp_model(text)
                compare_model(p, cpp, bl_parse, errs, stats)
            else:
                cpp = parse_cpp_x(text)
                compare_x(p, cpp, bl_x, errs, stats)
                stats["cpp_warn"] = cpp["warns"]
        except Exception as exc:
            errs.append("Blender-Parser/Vergleich: %s: %s" % (type(exc).__name__, exc))
            errs.append(traceback.format_exc().strip().splitlines()[-3])
        head = "%s %s  meshes=%s verts=%s tris=%s bones=%s clips=%s keys=%s" % (
            "OK   " if not errs else "DIFF ", rel, stats.get("meshes"), stats.get("verts"),
            stats.get("tris"), stats.get("bones"), stats.get("clips"), stats.get("keys"))
        report.append(head)
        for e in errs:
            report.append("      ! " + e)
        for lvl, msg in stats.get("bl_warn", [])[:6]:
            report.append("      blender %s: %s" % (lvl, msg[:200]))
        for w in stats.get("cpp_warn", [])[:6]:
            report.append("      max warn: %s" % w[:200])
        if errs:
            total_bad += 1
        print(head, flush=True)
    report.append("")
    report.append("Dateien: %d, abweichend: %d" % (len(files), total_bad))
    txt = "\n".join(report)
    if out:
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(txt + "\n")
    print("Dateien: %d, abweichend: %d" % (len(files), total_bad))


if __name__ == "__main__":
    main()
