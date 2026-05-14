from __future__ import annotations

import json
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCENE_DIR = REPO_ROOT / "NipsEngine" / "Asset" / "Scene"


OBJ_ASSET_RE = re.compile(r'"ObjStaticMeshAsset"\s*:\s*("(?:[^"\\]|\\.)*")')
NORMALIZE_RE = re.compile(r'^\s*"NormalizeOnImport"\s*:\s*(?:true|false)\s*,?\s*$')


def to_static_mesh_bin_path(path: str) -> str:
    normalized = path.replace("\\", "/")
    if not normalized:
        return ""

    lower = normalized.lower()
    if lower.endswith(".bin"):
        return normalized

    if lower.endswith(".obj"):
        normalized = normalized[:-4] + ".bin"

    asset_mesh_prefix = "Asset/Mesh/"
    asset_prefix = "Asset/"
    if normalized.startswith(asset_mesh_prefix):
        relative = normalized[len(asset_mesh_prefix):]
    elif normalized.startswith(asset_prefix):
        relative = normalized[len(asset_prefix):]
    else:
        relative = Path(normalized).name

    return str(Path("Asset/Mesh/Bin") / Path(relative)).replace("\\", "/")


def migrate_scene(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    changed = False
    migrated_lines: list[str] = []

    for line in original.splitlines(keepends=True):
        if NORMALIZE_RE.match(line):
            changed = True
            continue

        def replace_asset(match: re.Match[str]) -> str:
            nonlocal changed
            source_path = json.loads(match.group(1))
            bin_path = to_static_mesh_bin_path(source_path)
            changed = True
            return f'"StaticMeshAsset" : {json.dumps(bin_path)}'

        migrated_lines.append(OBJ_ASSET_RE.sub(replace_asset, line))

    migrated = "".join(migrated_lines)
    if changed and migrated != original:
        path.write_text(migrated, encoding="utf-8", newline="")
        return True

    return False


def main() -> int:
    migrated_count = 0
    for scene_path in sorted(SCENE_DIR.glob("*.Scene")):
        if migrate_scene(scene_path):
            migrated_count += 1
            print(f"migrated {scene_path.relative_to(REPO_ROOT)}")

    print(f"migrated scenes: {migrated_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
