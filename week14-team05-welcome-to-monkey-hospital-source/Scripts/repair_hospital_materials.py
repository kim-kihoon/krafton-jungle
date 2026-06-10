#!/usr/bin/env python3
"""One-shot repair for hospital-map Auto materials (HasNormalMap / SectionColor)."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUTO = ROOT / "KraftonEngine" / "Content" / "Material" / "Auto"
HOSPITAL_MESH = (
    ROOT / "KraftonEngine" / "Content" / "Data" / "hospital-map-data" / "hospital-map_StaticMesh.uasset"
)


def hospital_material_paths() -> set[Path]:
    if not HOSPITAL_MESH.is_file():
        return set()
    data = HOSPITAL_MESH.read_bytes()
    import re

    names = re.findall(rb"Content/Material/Auto/([^\x00]+?)\.uasset", data)
    out: set[Path] = set()
    for raw in names:
        name = raw.decode("utf-8", errors="replace").split("\r")[0].strip()
        if name:
            out.add(AUTO / f"{name}.uasset")
    return out


def path_looks_like_normal(texture_path: str) -> bool:
    stem = Path(texture_path).stem.lower()
    tokens = ("_normal", "-normal", "normal", "_norm", "_bump", "-bump", "bump")
    return any(t in stem for t in tokens) or stem.endswith("_n") or stem.endswith("-n")


def find_cb_blob(data: bytes) -> tuple[int, bytes] | None:
    for off in range(0, len(data) - 52):
        bsize = struct.unpack_from("<I", data, off)[0]
        if bsize not in (48, 64):
            continue
        if off + 4 + bsize > len(data):
            continue
        blob = data[off + 4 : off + 4 + bsize]
        sc = struct.unpack_from("<4f", blob, 0)
        s = struct.unpack_from("<7f", blob, 16)
        if not all(-0.01 <= x <= 10 for x in sc):
            continue
        if s[1] < 0 or s[1] > 1.01:
            continue
        if s[0] not in (0.0, 1.0):
            continue
        return off, blob
    return None


def normal_texture_path(data: bytes) -> str:
    key = b"NormalTexture"
    i = 0
    while True:
        i = data.find(key, i)
        if i < 0:
            return ""
        j = data.find(b"Content/", i)
        if j < 0:
            i += len(key)
            continue
        end = data.find(b"\x00", j)
        return data[j:end].decode("utf-8", errors="replace")


def patch_file(path: Path, *, force: bool = False) -> bool:
    data = bytearray(path.read_bytes())
    if not force and b"hospital-map" not in data:
        return False
    found = find_cb_blob(data)
    if not found:
        print(f"  skip (no CB): {path.name}")
        return False
    off, blob = found
    bsize = struct.unpack_from("<I", data, off)[0]
    sc = list(struct.unpack_from("<4f", blob, 0))
    scalars = list(struct.unpack_from("<7f", blob, 16))
    normal_path = normal_texture_path(data)
    has_diffuse = b"Content/Texture/" in data
    b_emissive = scalars[2] >= 0.5
    changed = False

    if scalars[0] >= 0.5 and (not normal_path or not path_looks_like_normal(normal_path)):
        scalars[0] = 0.0
        changed = True

    if not b_emissive:
        if has_diffuse:
            if sc[0] < 0.99 or sc[1] < 0.99 or sc[2] < 0.99:
                sc = [1.0, 1.0, 1.0, 1.0]
                changed = True
        elif sc[0] < 0.2 and sc[1] < 0.2 and sc[2] < 0.2:
            sc = [0.2, 0.2, 0.2, 1.0]
            changed = True

    if not changed:
        return False

    new_blob = struct.pack("<4f", *sc) + struct.pack("<7f", *scalars)
    if len(new_blob) < bsize:
        new_blob += bytes(bsize - len(new_blob))
    data[off + 4 : off + 4 + bsize] = new_blob
    path.write_bytes(data)
    print(f"  patched: {path.name} HasNM={scalars[0]} SC={tuple(round(x, 3) for x in sc[:3])}")
    return True


def main() -> int:
    if not AUTO.is_dir():
        print("Auto material dir missing", AUTO)
        return 1
    targets = hospital_material_paths()
    if not targets:
        print("No hospital mesh materials found; patching hospital-map texture materials only.")
        targets = {p for p in AUTO.glob("*.uasset") if b"hospital-map" in p.read_bytes()}
    n = 0
    for uasset in sorted(targets):
        if uasset.is_file() and patch_file(uasset, force=True):
            n += 1
    print(f"Done. {n} material(s) updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
