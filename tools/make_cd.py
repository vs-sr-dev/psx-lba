"""Build the psx-lba CD image from your own copy of Little Big Adventure.

No game data ships with this repository. Point this at a DOS install — in a
GOG copy that is `Speedrun/Windows/`, NOT `Common/`, which holds the 2023
remaster's archives under the same file names — and it stages what the engine
reads and writes the mkpsxiso project for it.

    python tools/make_cd.py "I:/GOG/Little Big Adventure/Speedrun/Windows"
    python tools/make_cd.py <path> --vox EN        # add one language's voices
    python tools/make_cd.py <path> --vox ALL       # all five, ~99 MB

Then, from the repository root:

    docker run --rm -v ${PWD}:/work -w /work psx-lba-psn00b \
        mkpsxiso -y -o build/lba1psx.bin -c build/lba1psx.cue build/iso.xml

The ISO9660 filenames are the DOS ones unchanged: every archive is already
8.3 and uppercase, which is one of the small mercies of porting a 1994 DOS
game to a 1994 console.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

# What the engine opens. The names come from the engine's own call sites, so
# this list is a specification rather than an inventory: if the port asks for
# something not here, the TTY says so by name.
CORE = [
    "LBA.CFG",      # configuration; the first thing the engine looks for
    "SETUP.LST",
    "RESS.HQR",     # menus, fonts, the title screens
    "TEXT.HQR",
    "SCENE.HQR",
    "SPRITES.HQR",
    "BODY.HQR",     # the 3D actors: 132 bodies, 19826 polygons
    "ANIM.HQR",
    "FILE3D.HQR",
    "INVOBJ.HQR",
    "LBA_BRK.HQR",  # the brick grid — 3.9 MB, the largest thing on the disc
    "LBA_BLL.HQR",
    "LBA_GRI.HQR",
    "MIDI_MI.HQR",  # XMI music; unused for now, see docs/FEASIBILITY.md §6
    "MIDI_SB.HQR",
    "SAMPLES.HQR",
]

LANGUAGES = ["EN", "FR", "DE", "SP", "IT"]

# LBA.CFG in a DOS install names the sound cards that machine had. Most of
# those names are harmless here — the port's driver stubs report success, which
# is what the DS port does, because ADELINE.C calls exit(1) on a driver that
# fails to load and a config the user did not write will always name one.
#
# MixerDriver is the exception. LIB_MIX/MIXER.C is real code, not a stub: it
# loads the DLL itself, and there is no DLL to load. A PlayStation has no mixer
# chip, so the honest value is the one DOS SETUP.EXE would have written on a
# machine without one.
CFG_OVERRIDES = {
    "MixerDriver": "NoMixer",
}

SYSTEM_CNF = """BOOT=cdrom:\\LBA1PSX.EXE;1
TCB=4
EVENT=10
STACK=801FFF00
"""


def stage(src_root: Path, out: Path, vox: str | None) -> list[tuple[str, Path]]:
    """Copy what the disc needs into `out`, returning (iso name, path) pairs."""
    entries: list[tuple[str, Path]] = []
    missing: list[str] = []

    out.mkdir(parents=True, exist_ok=True)

    for name in CORE:
        src = src_root / name
        if not src.exists():
            missing.append(name)
            continue
        dst = out / name
        if name == "LBA.CFG":
            write_cfg(src, dst)
        elif not dst.exists() or dst.stat().st_size != src.stat().st_size:
            shutil.copy2(src, dst)
        entries.append((name, dst))

    if missing:
        print(f"!! missing from {src_root}:")
        for name in missing:
            print(f"     {name}")
        print("   Is this the DOS install? A GOG copy keeps it in "
              "Speedrun/Windows/,")
        print("   and Common/ is the 2023 remaster with the same file names.")

    if vox:
        wanted = LANGUAGES if vox.upper() == "ALL" else [vox.upper()]
        vox_dir = src_root / "VOX"
        if not vox_dir.is_dir():
            print(f"!! no VOX/ under {src_root}, voices skipped")
        else:
            for path in sorted(vox_dir.glob("*.VOX")):
                if path.stem.split("_")[0].upper() not in wanted:
                    continue
                dst = out / "VOX" / path.name
                dst.parent.mkdir(exist_ok=True)
                if not dst.exists() or dst.stat().st_size != path.stat().st_size:
                    shutil.copy2(path, dst)
                entries.append((f"VOX/{path.name}", dst))

    return entries


def write_cfg(src: Path, dst: Path) -> None:
    """Copy LBA.CFG, rewriting only the settings a PlayStation contradicts."""
    out_lines = []
    changed = []

    for raw in src.read_text(encoding="latin-1").splitlines():
        key = raw.split(":", 1)[0].strip()
        if key in CFG_OVERRIDES:
            value = CFG_OVERRIDES[key]
            out_lines.append(f"{key}: {value}")
            changed.append(f"{key} -> {value}")
        else:
            out_lines.append(raw)

    text = chr(10).join(out_lines) + chr(10)
    dst.write_text(text, encoding="latin-1", newline=chr(13)+chr(10))

    for note in changed:
        print(f"   LBA.CFG: {note}")


def write_xml(entries, exe: Path, out_dir: Path, xml_path: Path) -> None:
    system_cnf = out_dir / "SYSTEM.CNF"
    system_cnf.write_text(SYSTEM_CNF, encoding="ascii", newline="\r\n")

    staged_exe = out_dir / "LBA1PSX.EXE"
    shutil.copy2(exe, staged_exe)

    # mkpsxiso resolves source paths against the directory holding the XML,
    # not its own working directory. Everything the disc carries is staged
    # under out_dir, so the staging directory ends up being an exact picture of
    # the disc — including the executable, which is copied in rather than
    # referenced out of the build tree.
    def rel(p: Path) -> str:
        return p.relative_to(xml_path.parent).as_posix()

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '',
        '<!-- Generated by tools/make_cd.py. Do not edit: the file list comes',
        '     from your own install and is regenerated on every run. -->',
        '',
        '<iso_project image_name="lba1psx.bin" cue_sheet="lba1psx.cue">',
        '  <track type="data">',
        '    <identifiers',
        '      system="PLAYSTATION"',
        '      application="PLAYSTATION"',
        '      volume="LBA1PSX"',
        '      volume_set="LBA1PSX"',
        '      publisher="PSX-LBA"',
        '      data_preparer="mkpsxiso"',
        '    />',
        '    <directory_tree>',
        f'      <file name="SYSTEM.CNF" type="data" source="{rel(system_cnf)}"/>',
        f'      <file name="LBA1PSX.EXE" type="data" source="{rel(staged_exe)}"/>',
    ]

    vox_entries = [(n, p) for n, p in entries if n.startswith("VOX/")]
    flat = [(n, p) for n, p in entries if not n.startswith("VOX/")]

    # NB: no field padding inside name="...". mkpsxiso takes the attribute
    # literally, so aligning the columns here writes the spaces onto the disc:
    # the engine then asks for LBA.CFG and the file is called "LBA.CFG     ".
    for name, path in flat:
        lines.append(f'      <file name="{name}" type="data" '
                     f'source="{rel(path)}"/>')

    if vox_entries:
        lines.append('      <dir name="VOX">')
        for name, path in vox_entries:
            base = name.split("/", 1)[1]
            lines.append(f'        <file name="{base}" type="data" '
                         f'source="{rel(path)}"/>')
        lines.append('      </dir>')

    lines += [
        '    </directory_tree>',
        '  </track>',
        '</iso_project>',
        '',
    ]

    xml_path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("assets", help="the DOS install (GOG: Speedrun/Windows)")
    ap.add_argument("--vox", help="language code, or ALL, to stage voices")
    ap.add_argument("--out", default="build", help="staging directory")
    ap.add_argument("--exe", default="platform/psx/build/lba1psx.exe",
                    help="the PS-EXE to boot")
    args = ap.parse_args(argv[1:])

    src = Path(args.assets)
    if not src.is_dir():
        print(f"!! {src} is not a directory")
        return 2

    out = Path(args.out)
    exe = Path(args.exe)
    if not exe.exists():
        print(f"!! {exe} not built yet — build platform/psx first")
        return 2

    entries = stage(src, out / "cd", args.vox)
    write_xml(entries, exe, out / "cd", out / "iso.xml")

    total = sum(p.stat().st_size for _, p in entries)
    print(f"staged {len(entries)} files, {total / (1024*1024):.1f} MB")
    print(f"wrote  {out / 'iso.xml'}")
    print()
    print("now:  docker run --rm -v ${PWD}:/work -w /work psx-lba-psn00b \\")
    print(f"          mkpsxiso -y -o {out}/lba1psx.bin -c {out}/lba1psx.cue "
          f"{out}/iso.xml")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
