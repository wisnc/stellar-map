#!/usr/bin/env python3
"""
gen_catalog.py  -  build flash headers for the Cardputer star-map firmware.

Emits:
  star_catalog.h   J2000 unit vectors + magnitude + B-V*1000 + distance (LY)
  star_names.h     label / spectral type / constellation per star
  constellations.h constellation line segments as catalog-index pairs
  messier.h        110 Messier deep-sky objects (vector, mag, id, name, type, con)

Run:  python3 gen_catalog.py [mag_limit]   (default 6.5)
"""
import csv, io, math, sys, urllib.request, os

MAG_LIMIT = float(sys.argv[1]) if len(sys.argv) > 1 else 6.5
OUT = os.path.join(os.path.dirname(__file__), "..", "src", "data")
PC_TO_LY = 3.2615638

HYG_URLS = [
    "https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/CURRENT/hygdata_v41.csv",
    "https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv",
    "https://raw.githubusercontent.com/astronexus/HYG-Database/master/hygdata_v3.csv",
]
FAB_URLS = [
    "https://raw.githubusercontent.com/Stellarium/stellarium/v0.21.0/skycultures/western/constellationship.fab",
    "https://raw.githubusercontent.com/Stellarium/stellarium/v0.20.0/skycultures/western/constellationship.fab",
]
NGC_URLS = [
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv",
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/NGC.csv",
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/main/database_files/NGC.csv",
]
NGC_ADD_URLS = [
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/addendum/NGC_addendum.csv",
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/NGC_addendum.csv",
    "https://raw.githubusercontent.com/mattiaverga/OpenNGC/main/database_files/addendum/NGC_addendum.csv",
]


def fetch(urls, required=True):
    for u in urls:
        try:
            print("  trying", u)
            with urllib.request.urlopen(u, timeout=60) as r:
                data = r.read().decode("utf-8", "replace")
            print("  ok (%d bytes)" % len(data))
            return data, u
        except Exception as e:
            print("  fail:", e)
    if required:
        sys.exit("Could not fetch required resource.")
    return None, None


def cfloat(v):
    return ("%.6ff" % v)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def hms_to_rad(s):          # "HH:MM:SS.SS" -> radians
    p = s.split(":")
    h = float(p[0]) + float(p[1]) / 60.0 + float(p[2]) / 3600.0
    return h * 15.0 * math.pi / 180.0


def dms_to_rad(s):          # "+DD:MM:SS.S" -> radians
    s = s.strip()
    sign = -1.0 if s[0] == "-" else 1.0
    s = s.lstrip("+-")
    p = s.split(":")
    d = float(p[0]) + float(p[1]) / 60.0 + float(p[2]) / 3600.0
    return sign * d * math.pi / 180.0


def build_stars():
    print("Fetching HYG database...")
    hyg, hyg_url = fetch(HYG_URLS)
    rows = list(csv.DictReader(io.StringIO(hyg)))
    stars, hip2idx = [], {}
    for r in rows:
        try:
            mag = float(r.get("mag", ""))
        except ValueError:
            continue
        if mag > MAG_LIMIT:
            continue
        try:
            if float(r.get("dist", "1") or "1") == 0.0:
                continue                                # the Sun
        except ValueError:
            pass
        rarad, decrad = r.get("rarad", ""), r.get("decrad", "")
        if rarad and decrad:
            ra, dec = float(rarad), float(decrad)
        else:
            ra = float(r["ra"]) * math.pi / 12.0
            dec = float(r["dec"]) * math.pi / 180.0
        x = math.cos(dec) * math.cos(ra)
        y = math.cos(dec) * math.sin(ra)
        z = math.sin(dec)
        try:
            ci = int(round(float(r.get("ci", "")) * 1000))
        except ValueError:
            ci = 0x7FFF
        try:
            dpc = float(r.get("dist", ""))
        except ValueError:
            dpc = -1.0
        ly = -1.0 if (dpc <= 0 or dpc >= 100000.0) else dpc * PC_TO_LY
        proper = (r.get("proper", "") or "").strip()
        bf = (r.get("bf", "") or "").strip()
        hip = (r.get("hip", "") or "").strip()
        label = proper if proper else (bf if bf else ("HIP " + hip if hip else ""))
        spect = (r.get("spect", "") or "").strip()[:8]
        con = (r.get("con", "") or "").strip()
        idx = len(stars)
        stars.append((x, y, z, mag, ci, ly, label, spect, con))
        if hip:
            try:
                hip2idx[int(hip)] = idx
            except ValueError:
                pass
    print("Stars kept (mag <= %.2f): %d" % (MAG_LIMIT, len(stars)))
    return stars, hip2idx, hyg_url


def build_segments(hip2idx):
    print("Fetching constellation lines...")
    fab, fab_url = fetch(FAB_URLS, required=False)
    segs, missing = [], 0
    if fab:
        for line in fab.splitlines():
            t = line.split()
            if len(t) < 3:
                continue
            try:
                n = int(t[1])
            except ValueError:
                continue
            hips = t[2:2 + 2 * n]
            for k in range(0, len(hips) - 1, 2):
                try:
                    a, b = int(hips[k]), int(hips[k + 1])
                except ValueError:
                    continue
                ia, ib = hip2idx.get(a), hip2idx.get(b)
                if ia is not None and ib is not None:
                    segs.append((ia, ib))
                else:
                    missing += 1
        print("Line segments: %d (skipped %d)" % (len(segs), missing))
    return segs, fab_url


def map_type(t):
    t = t.strip()
    if t in ("G", "GPair", "GTrpl", "GGroup"): return "Galaxy"
    if t == "OCl": return "Open Cluster"
    if t == "GCl": return "Globular"
    if t == "PN": return "Planetary Neb"
    if t == "SNR": return "Supernova Rem"
    if t in ("HII", "EmN", "Neb", "Cl+N", "RfN", "DrkN"): return "Nebula"
    if t in ("*", "**", "*Ass"): return "Star(s)"
    return t if t else "Object"


def build_messier():
    print("Fetching OpenNGC (Messier)...")
    main, ngc_url = fetch(NGC_URLS)
    add, _ = fetch(NGC_ADD_URLS, required=False)
    text = main + ("\n" + add if add else "")
    best = {}                                          # messier number -> row dict
    for r in csv.DictReader(io.StringIO(text), delimiter=";"):
        m = (r.get("M", "") or "").strip()
        if not m:
            continue
        try:
            num = int(m)
        except ValueError:
            continue
        if not (r.get("RA", "") or "").strip() or not (r.get("Dec", "") or "").strip():
            continue
        typ = (r.get("Type", "") or "").strip()
        if num in best and best[num][0] != "Dup" and typ == "Dup":
            continue
        best[num] = (typ, r)
    objs = []
    for num in sorted(best):
        typ, r = best[num]
        try:
            ra = hms_to_rad(r["RA"]); dec = dms_to_rad(r["Dec"])
        except Exception:
            continue
        x = math.cos(dec) * math.cos(ra)
        y = math.cos(dec) * math.sin(ra)
        z = math.sin(dec)
        try:
            mag = float(r.get("V-Mag", "") or "")
        except ValueError:
            try:
                mag = float(r.get("B-Mag", "") or "")
            except ValueError:
                mag = 99.0
        names = (r.get("Common names", "") or "").strip()
        name = names.split(",")[0].strip() if names else ""
        con = (r.get("Const", "") or "").strip()
        objs.append((x, y, z, mag, "M" + str(num), name, map_type(typ), con))

    # objects absent from the OpenNGC main file (no NGC number / disputed id)
    have = {o[4] for o in objs}
    fallback = [
        # ra_h, dec_deg, mag, id, name, type, con
        (3.79000, 24.11667, 1.6,  "M45", "Pleiades",       "Open Cluster", "Tau"),
        (12.37014, 58.08306, 9.6, "M40", "Winnecke 4",     "Star(s)",      "UMa"),
        (15.10819, 55.76333, 9.9, "M102", "Spindle Galaxy","Galaxy",       "Dra"),
    ]
    for rah, decd, mag, mid, name, typ, con in fallback:
        if mid in have:
            continue
        ra = rah * 15.0 * math.pi / 180.0
        dec = decd * math.pi / 180.0
        objs.append((math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra),
                     math.sin(dec), mag, mid, name, typ, con))
    objs.sort(key=lambda o: int(o[4][1:]))
    print("Messier objects: %d" % len(objs))
    return objs, ngc_url


def emit_stars(stars, hyg_url):
    with open(os.path.join(OUT, "star_catalog.h"), "w") as f:
        f.write("// Generated by gen_catalog.py  (source: %s)\n" % hyg_url)
        f.write("// J2000 unit vectors, magnitude, B-V*1000 (0x7FFF=unknown), distance LY (-1=unknown).\n")
        f.write("#pragma once\n#include <cstdint>\n\n")
        f.write("constexpr int STAR_COUNT = %d;\n\n" % len(stars))
        f.write("const float star_xyz[STAR_COUNT][3] = {\n")
        for i in range(0, len(stars), 4):
            f.write("  " + " ".join("{%s,%s,%s}," % (cfloat(s[0]), cfloat(s[1]), cfloat(s[2]))
                                    for s in stars[i:i + 4]) + "\n")
        f.write("};\n\n")
        f.write("const float star_mag[STAR_COUNT] = {\n")
        for i in range(0, len(stars), 10):
            f.write("  " + " ".join(cfloat(s[3]) + "," for s in stars[i:i + 10]) + "\n")
        f.write("};\n\n")
        f.write("const int16_t star_ci[STAR_COUNT] = {\n")
        for i in range(0, len(stars), 12):
            f.write("  " + " ".join("%d," % s[4] for s in stars[i:i + 12]) + "\n")
        f.write("};\n\n")
        f.write("const float star_dist_ly[STAR_COUNT] = {\n")
        for i in range(0, len(stars), 10):
            f.write("  " + " ".join(cfloat(s[5]) + "," for s in stars[i:i + 10]) + "\n")
        f.write("};\n")


def emit_names(stars):
    with open(os.path.join(OUT, "star_names.h"), "w") as f:
        f.write("// Generated by gen_catalog.py\n#pragma once\n\n")
        for arr, k in (("star_label", 6), ("star_spect", 7), ("star_con", 8)):
            f.write("const char* const %s[STAR_COUNT] = {\n" % arr)
            for s in stars:
                f.write("  " + cstr(s[k]) + ",\n")
            f.write("};\n\n")


def emit_segments(segs, fab_url):
    with open(os.path.join(OUT, "constellations.h"), "w") as f:
        f.write("// Generated by gen_catalog.py  (source: %s)\n" % (fab_url or "none"))
        f.write("#pragma once\n#include <cstdint>\n\n")
        f.write("constexpr int CONST_SEG_COUNT = %d;\n\n" % len(segs))
        f.write("const uint16_t const_seg[CONST_SEG_COUNT > 0 ? CONST_SEG_COUNT : 1][2] = {\n")
        if segs:
            for i in range(0, len(segs), 8):
                f.write("  " + " ".join("{%d,%d}," % (a, b) for a, b in segs[i:i + 8]) + "\n")
        else:
            f.write("  {0,0}\n")
        f.write("};\n")


def emit_messier(objs, ngc_url):
    with open(os.path.join(OUT, "messier.h"), "w") as f:
        f.write("// Generated by gen_catalog.py  (source: %s, OpenNGC CC-BY-SA-4.0)\n" % ngc_url)
        f.write("#pragma once\n\n")
        f.write("constexpr int MESSIER_COUNT = %d;\n\n" % len(objs))
        f.write("const float messier_xyz[MESSIER_COUNT][3] = {\n")
        for o in objs:
            f.write("  {%s,%s,%s},\n" % (cfloat(o[0]), cfloat(o[1]), cfloat(o[2])))
        f.write("};\n\n")
        f.write("const float messier_mag[MESSIER_COUNT] = {\n")
        for i in range(0, len(objs), 10):
            f.write("  " + " ".join(cfloat(o[3]) + "," for o in objs[i:i + 10]) + "\n")
        f.write("};\n\n")
        for arr, k in (("messier_id", 4), ("messier_name", 5), ("messier_type", 6), ("messier_con", 7)):
            f.write("const char* const %s[MESSIER_COUNT] = {\n" % arr)
            for o in objs:
                f.write("  " + cstr(o[k]) + ",\n")
            f.write("};\n\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    stars, hip2idx, hyg_url = build_stars()
    segs, fab_url = build_segments(hip2idx)
    objs, ngc_url = build_messier()
    emit_stars(stars, hyg_url)
    emit_names(stars)
    emit_segments(segs, fab_url)
    emit_messier(objs, ngc_url)
    print("Wrote headers to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
