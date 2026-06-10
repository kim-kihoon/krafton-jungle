#!/usr/bin/env python3
"""Fix Hospital.Scene: collision, material slots, ASpotLightActor_1."""

import json
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "KraftonEngine" / "Content"
SCENE_PATH = CONTENT / "Scene" / "Hospital.Scene"

EXCLUDE_ACTOR_NAMES = {"CymbalMonkeyInplay"}
MESH_COMPONENT_CLASSES = {"UStaticMeshComponent", "USkeletalMeshComponent"}


def extract_static_materials(uasset_path: Path) -> list[str] | None:
    data = uasset_path.read_bytes()
    best = None
    best_start = -1
    for start in range(max(0, len(data) - 32768), len(data) - 8):
        count = struct.unpack_from("<I", data, start)[0]
        if count < 1 or count > 64:
            continue
        off = start + 4
        slots: list[str] = []
        ok = True
        for _ in range(count):
            if off + 4 > len(data):
                ok = False
                break
            n1 = struct.unpack_from("<I", data, off)[0]
            off += 4
            if n1 == 0 or n1 > 256 or off + n1 > len(data):
                ok = False
                break
            off += n1
            if off + 4 > len(data):
                ok = False
                break
            n2 = struct.unpack_from("<I", data, off)[0]
            off += 4
            if n2 < 10 or n2 > 512 or off + n2 > len(data):
                ok = False
                break
            path = data[off : off + n2].decode("utf-8", errors="replace")
            off += n2
            if not path.startswith("Content/Material/") or not path.endswith(".uasset"):
                ok = False
                break
            slots.append(path)
        if ok and len(slots) == count and start > best_start:
            best = slots
            best_start = start
    return best


def mesh_path_from_props(props: dict) -> str | None:
    if "StaticMeshPath" in props:
        return props["StaticMeshPath"]
    if "SkeletalMeshPath" in props:
        return props["SkeletalMeshPath"]
    return None


def fix_component_tree(node: dict, actor_name: str, stats: dict) -> None:
    if not isinstance(node, dict):
        return

    class_name = node.get("ClassName", "")
    props = node.get("Properties")
    if isinstance(props, dict) and class_name in MESH_COMPONENT_CLASSES:
        if actor_name not in EXCLUDE_ACTOR_NAMES:
            if props.get("CollisionEnabled") != 3:
                props["CollisionEnabled"] = 3
                stats["collision_fixed"] += 1

        mesh_path = mesh_path_from_props(props)
        if mesh_path and class_name == "UStaticMeshComponent":
            uasset = CONTENT / mesh_path.replace("Content/", "")
            if uasset.exists():
                default_mats = extract_static_materials(uasset)
                if default_mats and props.get("MaterialSlots") != default_mats:
                    props["MaterialSlots"] = default_mats
                    stats["material_fixed"] += 1

    for child in node.get("Children", []):
        fix_component_tree(child, actor_name, stats)


def fix_spotlight(actor: dict, stats: dict) -> None:
    if actor.get("Name") != "ASpotLightActor_1":
        return

    actor_props = actor.get("Properties", {})
    actor_props["PendingActorRotation"] = [90.0, 0.0, 0.0]

    root = actor.get("RootComponent", {})
    root_props = root.get("Properties", {})
    root_props["CachedEditRotator"] = [90.0, 0.0, 0.0]
    root_props["AttenuationRadius"] = 8.0
    root_props["Intensity"] = 10.0
    root_props["OuterConeAngle"] = 40.0
    root_props["InnerConeAngle"] = 25.0
    stats["spotlight_fixed"] += 1


def main() -> None:
    stats = {"collision_fixed": 0, "material_fixed": 0, "spotlight_fixed": 0}
    scene = json.loads(SCENE_PATH.read_text(encoding="utf-8"))

    for actor in scene.get("Actors", []):
        actor_name = actor.get("Name", "")
        fix_spotlight(actor, stats)
        root = actor.get("RootComponent")
        if root:
            fix_component_tree(root, actor_name, stats)

    SCENE_PATH.write_text(
        json.dumps(scene, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print("Wrote", SCENE_PATH)
    print(stats)


if __name__ == "__main__":
    main()
