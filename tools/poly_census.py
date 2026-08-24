"""Census of the polygon fill modes used by the real game data.

This answers the question that decides the PSX rendering architecture: the
engine has ten polygon fill modes, all of which operate on *palette indices*
rather than colours, and only some of them have a GPU equivalent. Before
committing to a GTE/GPU actor path we need to know how much of the actual
game uses the modes that do not map.

Walks every body in BODY.HQR (and, with --all, the 3D data embedded in the
other archives) and counts polygons by material, plus lines and spheres.

Body layout, from translate/p_ob_iso.c (AffObjetIso / RotateNuage /
AnimNuage / PatchObjet):

    u16 infos                       bit 1 = INFO_ANIM
    ...
    u16 info_zone_size  @ +14       body data starts at info_zone_size + 16
    u16 nb_points;   point[6]  * n
    u16 nb_groups;   group[38] * n  (INFO_ANIM bodies only)
    u16 nb_normals;  normal[8] * n
    u16 nb_polys;    poly records
    u16 nb_lines;    line[8]   * n
    u16 nb_spheres;  sphere[8] * n

A polygon record is a u16 whose low byte is the material and whose high byte
is the vertex count, then:

    material >= 9   u16 colour, then (u16 normal, u16 vertex) per vertex
    material >= 7   u16 colour, u16 face normal, then u16 vertex per vertex
    material <  7   u16 colour, then u16 vertex per vertex

Usage:
    python tools/poly_census.py "I:/GOG/Little Big Adventure/Speedrun/Windows"
"""

from __future__ import annotations

import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hqr import Hqr  # noqa: E402

INFO_ANIM = 2

# Material byte -> the fill routine index the engine ends up dispatching on
# (see the `matiere` branches in p_ob_iso.c and TabPoly_* in s_fillv.c).
#   material 0..6  -> fill type 0..6,  flat colour, no lighting
#   material 7..8  -> fill type 0..1,  same fills, lit by a face normal
#   material >= 9  -> fill type m - 2, per-vertex normals
FILL_NAMES = {
    0: "Triste  (flat)",
    1: "Tele    (4-colour noise)",
    2: "Copper  (gradient)",
    3: "Bopper  (gradient)",
    4: "Marbre  (span gradient)",
    5: "Trans   (transparency)",
    6: "Trame   (screen door)",
    7: "Gouraud",
    8: "Dith    (gouraud + dither)",
}

# Can the PSX GPU express this natively?
GPU_VERDICT = {
    0: "direct   flat poly",
    1: "texture  tiled 4-colour noise texture",
    2: "check    per-scanline gradient",
    3: "check    per-scanline gradient",
    4: "direct   gouraud along the span",
    5: "direct   semi-transparency",
    6: "direct   dither / semi-transparency",
    7: "direct   gouraud",
    8: "direct   gouraud + dither",
}


def fill_type(material: int) -> int:
    if material >= 9:
        return material - 2
    if material >= 7:
        return material - 7
    return material


class BodyError(Exception):
    pass


def parse_body(data: bytes) -> dict:
    """Return per-body counts, or raise BodyError if the walk does not fit."""
    if len(data) < 16:
        raise BodyError("too short")

    u16 = lambda off: struct.unpack_from("<H", data, off)[0]  # noqa: E731

    infos = u16(0)
    pos = u16(14) + 16
    if pos > len(data):
        raise BodyError("info zone overruns the entry")

    def take(n: int) -> int:
        nonlocal pos
        if pos + n > len(data):
            raise BodyError(f"section overruns the entry at {pos}")
        pos += n
        return pos - n

    nb_points = u16(take(2))
    take(nb_points * 6)

    nb_groups = 0
    if infos & INFO_ANIM:
        nb_groups = u16(take(2))
        take(nb_groups * 38)

    nb_normals = u16(take(2))
    take(nb_normals * 8)

    nb_polys = u16(take(2))
    materials = Counter()
    vertex_hist = Counter()

    for _ in range(nb_polys):
        head = u16(take(2))
        material = head & 0xFF
        nb_vertex = head >> 8
        if nb_vertex == 0:
            raise BodyError("polygon with zero vertices")
        materials[material] += 1
        vertex_hist[nb_vertex] += 1

        if material >= 9:
            take(2 + nb_vertex * 4)
        elif material >= 7:
            take(4 + nb_vertex * 2)
        else:
            take(2 + nb_vertex * 2)

    nb_lines = u16(take(2))
    take(nb_lines * 8)

    nb_spheres = u16(take(2))
    take(nb_spheres * 8)

    return {
        "anim": bool(infos & INFO_ANIM),
        "points": nb_points,
        "groups": nb_groups,
        "polys": nb_polys,
        "lines": nb_lines,
        "spheres": nb_spheres,
        "materials": materials,
        "vertex_hist": vertex_hist,
        "consumed": pos,
        "size": len(data),
    }


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2

    root = Path(argv[1])
    archives = ["BODY.HQR"]
    if "--all" in argv:
        archives += ["RESS.HQR", "INVOBJ.HQR"]

    materials = Counter()
    vertex_hist = Counter()
    totals = Counter()
    bodies_ok = 0
    bodies_bad: list[tuple[str, int, str]] = []
    slack = Counter()

    for name in archives:
        path = root / name
        if not path.exists():
            print(f"!! {path} not found, skipped")
            continue

        archive = Hqr(path)
        print(f"== {name}: {len(archive)} entries")

        for index, data in archive.entries():
            if not data:
                continue
            try:
                body = parse_body(data)
            except (BodyError, struct.error, IndexError) as exc:
                bodies_bad.append((name, index, str(exc)))
                continue

            bodies_ok += 1
            materials.update(body["materials"])
            vertex_hist.update(body["vertex_hist"])
            totals["polys"] += body["polys"]
            totals["lines"] += body["lines"]
            totals["spheres"] += body["spheres"]
            totals["points"] += body["points"]
            totals["anim"] += 1 if body["anim"] else 0
            slack[body["size"] - body["consumed"]] += 1

    total_polys = sum(materials.values())

    print()
    print(f"bodies parsed      {bodies_ok}")
    print(f"  of which animated{totals['anim']:>6}")
    print(f"bodies rejected    {len(bodies_bad)}")
    print(f"polygons           {total_polys}")
    print(f"lines              {totals['lines']}")
    print(f"spheres            {totals['spheres']}")
    print(f"points             {totals['points']}")

    print()
    print("trailing bytes after the walk (0 = the layout is exactly right):")
    for value, count in sorted(slack.items())[:8]:
        print(f"  {value:>6} bytes   {count} bodies")

    print()
    print("polygons by fill type:")
    by_fill = Counter()
    lit = Counter()
    for material, count in materials.items():
        by_fill[fill_type(material)] += count
        lit["per-vertex" if material >= 9 else
            "face normal" if material >= 7 else "unlit"] += count

    for ftype in sorted(by_fill):
        count = by_fill[ftype]
        share = 100.0 * count / total_polys if total_polys else 0.0
        print(f"  {ftype}  {FILL_NAMES.get(ftype, '?'):<26}"
              f"{count:>8}  {share:5.2f}%   {GPU_VERDICT.get(ftype, '?')}")

    print()
    print("raw material bytes:")
    for material in sorted(materials):
        print(f"  {material:>3}  {materials[material]:>8}")

    print()
    print("lighting:", dict(lit))
    print("vertices per polygon:", dict(sorted(vertex_hist.items())))

    if bodies_bad:
        print()
        print("rejected entries (first 15):")
        for name, index, why in bodies_bad[:15]:
            print(f"  {name}[{index}]: {why}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
