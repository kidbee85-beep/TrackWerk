#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the RG40XX V (KNULLI) community test from an existing, verified Brick First Install ZIP.

The program is byte-for-byte the Brick's aarch64 build; only the launcher, the two menu descriptors
and the documents differ. So this reads the Brick ZIP, checks that the ELF in it is the one the
Brick Program Update carries, swaps the launcher and descriptors, rewrites START-HERE.txt and
SHA256SUMS.txt and writes one deterministic ZIP. Nothing is built, signed or published.

  python3 package.py --brick-first-install TrackWerk-1.2.1-Brick-KNULLI-First-Install.zip \\
                     --brick-program-update TrackWerk-1.2.1-Brick-Program-Update.zip \\
                     --out OUTPUT_DIR
"""
from __future__ import annotations

import argparse
import hashlib
import json
import stat
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERSION = "1.2.1"
NAME = f"TrackWerk-{VERSION}-RG40XXV-KNULLI-Community-Test"
ELF = "trackwerk/trackwerk.aarch64"
ZIP_TIME = (2026, 11, 19, 0, 0, 0)

START_HERE = """TRACKWERK 1.2.1 / RG40XX V / KNULLI - COMMUNITY TEST

NOT YET RUN ON THE DEVICE. This is the TrimUI Brick program with a launcher for the Anbernic
RG40XX V. Read README-RG40XXV.txt first (what is known, what is not, how to report).

Needs KNULLI "Scarab" (2026-05-11) or newer. https://knulli.org/

1. Power down the device and open its SD card on your computer.
2. Copy TrackWerk.sh and the trackwerk folder into roms/ports/ on the card's SHARE partition.
3. Optional menu art: python3 setup_brick_menu.py --target /media/CARD/roms/ports/trackwerk
   Review the plan and repeat with --apply.
4. Eject the card safely, boot and open Ports > TrackWerk.

If it does not start: send trackwerk/log.txt and your KNULLI version. The launcher tries the GPU
renderer first and then the window-surface mode, and logs what the device reports.
Samples go in roms/ports/trackwerk/data/Samples.
"""

SOURCE_NOTE = (
    "TrackWerk 1.2.1 corresponding source: TrackWerk-1.2.1-Source.zip\n"
    "Program: the same aarch64 build as TrackWerk-1.2.1-Brick-KNULLI-First-Install.zip\n"
    "Launcher: source/shell/rg40xxv/TrackWerk.sh\n"
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"package.py: {message}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--brick-first-install", type=Path, required=True)
    ap.add_argument("--brick-program-update", type=Path, required=True,
                    help="used only to confirm that the ELF is the verified Brick program")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    with zipfile.ZipFile(args.brick_program_update) as zf:
        verified = sha(zf.read("trackwerk.aarch64"))
    with zipfile.ZipFile(args.brick_first_install) as zf:
        names = zf.namelist()
        contents = {n: zf.read(n) for n in names if not n.endswith("/")}
    require(ELF in contents, "the First Install ZIP holds no trackwerk/trackwerk.aarch64")
    require(sha(contents[ELF]) == verified,
            "the ELF in the First Install ZIP differs from the Brick Program Update's")
    require(contents[ELF][:4] == b"\x7fELF" and contents[ELF][4] == 2 and
            int.from_bytes(contents[ELF][18:20], "little") == 183, "the program is not an aarch64 ELF")
    forbidden = {"data", "log.txt", "log.previous.txt", "video-mode.txt", "private-signing", ".git", ".DS_Store"}
    for n in contents:
        require(not forbidden.intersection(Path(n).parts), f"the input holds {n}")

    out = dict(contents)
    out.pop("SHA256SUMS.txt", None)
    out["TrackWerk.sh"] = (HERE / "TrackWerk.sh").read_bytes()
    out["trackwerk/port.json"] = (HERE / "port.json").read_bytes()
    out["trackwerk/gameinfo.xml"] = (HERE / "gameinfo.xml").read_bytes()
    out["README-RG40XXV.txt"] = (HERE / "README.txt").read_bytes()
    out["START-HERE.txt"] = START_HERE.encode()
    out["SOURCE.txt"] = SOURCE_NOTE.encode()
    json.loads(out["trackwerk/port.json"])
    require(b"RG40XXV" in out["TrackWerk.sh"], "launcher is not the RG40XX V one")

    sums = "".join(f"{sha(out[n])}  {n}\n" for n in sorted(out))
    out["SHA256SUMS.txt"] = sums.encode()

    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / f"{NAME}.zip"
    require(not target.exists(), f"{target} exists; choose a new output folder")
    executable = {"TrackWerk.sh", "setup_brick_menu.py"}
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zf:
        for n in sorted(out):
            info = zipfile.ZipInfo(n, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            exe = n in executable or n.startswith("trackwerk/tools/") or n == ELF
            info.external_attr = (stat.S_IFREG | (0o755 if exe else 0o644)) << 16
            zf.writestr(info, out[n])
    zip_sha = sha(target.read_bytes())
    (args.out / f"{NAME}.zip.sha256").write_text(f"{zip_sha}  {target.name}\n")
    print(f"{target.name}  {target.stat().st_size} bytes  sha256 {zip_sha}")
    print(f"program sha256 {verified} (identical to the Brick build)")


if __name__ == "__main__":
    main()
