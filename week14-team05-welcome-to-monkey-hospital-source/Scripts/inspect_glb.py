import json
import struct
import sys
from pathlib import Path


def node_name(gltf, i):
    return gltf["nodes"][i].get("name", f"node_{i}")


def read_accessor(gltf, bin_chunk, acc_idx):
    accessors = gltf.get("accessors", [])
    buffer_views = gltf.get("bufferViews", [])
    acc = accessors[acc_idx]
    bv = buffer_views[acc["bufferView"]]
    start = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]
    atype = acc["type"]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[atype]
    stride = bv.get("byteStride") or comps * 4
    raw = bin_chunk[start : start + count * stride]
    fmt = "<" + "f" * (count * comps)
    return struct.unpack(fmt, raw[: struct.calcsize(fmt)])


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else r"c:\Users\jungle\Downloads\tiffany_cox_death_animation.glb")
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    print(f"Path: {path}")
    print(f"Size: {len(data):,} bytes")
    print(f"Magic: {magic!r}, version: {version}, declared length: {length}")

    offset = 12
    chunks = []
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk_data = data[offset : offset + chunk_len]
        offset += chunk_len
        chunks.append((chunk_type, chunk_data))

    for i, (ctype, cdata) in enumerate(chunks):
        label = {0x4E4F534A: "JSON", 0x004E4942: "BIN"}.get(ctype, hex(ctype))
        print(f"Chunk {i}: {label}, {len(cdata):,} bytes")

    gltf = json.loads(chunks[0][1].decode("utf-8"))
    bin_chunk = chunks[1][1] if len(chunks) > 1 else b""

    print("\n=== glTF summary ===")
    print("asset:", gltf.get("asset"))
    print("scenes:", len(gltf.get("scenes", [])))
    print("nodes:", len(gltf.get("nodes", [])))
    print("meshes:", len(gltf.get("meshes", [])))
    print("materials:", len(gltf.get("materials", [])))
    print("skins:", len(gltf.get("skins", [])))
    print("animations:", len(gltf.get("animations", [])))

    print("\n=== Nodes ===")
    for i, n in enumerate(gltf.get("nodes", [])):
        parts = [f"[{i}] {n.get('name', '')}"]
        if "mesh" in n:
            parts.append(f"mesh={n['mesh']}")
        if "skin" in n:
            parts.append(f"skin={n['skin']}")
        if "children" in n:
            parts.append(f"children={n['children']}")
        if "translation" in n:
            parts.append(f"t={n['translation']}")
        if "rotation" in n:
            parts.append(f"r={n['rotation']}")
        if "scale" in n:
            parts.append(f"s={n['scale']}")
        print("  " + ", ".join(parts))

    print("\n=== Meshes ===")
    for i, m in enumerate(gltf.get("meshes", [])):
        print(f"  [{i}] name={m.get('name', '')} primitives={len(m.get('primitives', []))}")

    print("\n=== Skins ===")
    for i, s in enumerate(gltf.get("skins", [])):
        joints = s.get("joints", [])
        print(f"  [{i}] name={s.get('name', '')} joints={len(joints)} skeleton={s.get('skeleton')}")
        print(f"    root joints: {[node_name(gltf, j) for j in joints[:6]]}")

    print("\n=== Animations ===")
    for i, a in enumerate(gltf.get("animations", [])):
        name = a.get("name") or "(unnamed)"
        print(f"  [{i}] name={name} channels={len(a.get('channels', []))} samplers={len(a.get('samplers', []))}")
        max_t = 0.0
        min_t = 1e9
        for samp in a.get("samplers", []):
            inp = samp.get("input")
            if inp is not None:
                times = read_accessor(gltf, bin_chunk, inp)
                if times:
                    max_t = max(max_t, max(times))
                    min_t = min(min_t, min(times))
        dur = max_t - min_t if max_t > min_t else max_t
        print(f"    time: {min_t:.3f}s - {max_t:.3f}s (duration ~{dur:.3f}s)")
        paths = {}
        for ch in a.get("channels", []):
            p = ch.get("target", {}).get("path", "?")
            paths[p] = paths.get(p, 0) + 1
        print(f"    channel paths: {paths}")
        node_ids = {ch.get("target", {}).get("node") for ch in a.get("channels", [])}
        node_ids.discard(None)
        names = [node_name(gltf, n) for n in sorted(node_ids)]
        print(f"    animated nodes ({len(names)}): {names[:15]}{'...' if len(names) > 15 else ''}")

    print("\n=== Scale hint (mesh bbox, glTF Y-up) ===")
    accessors = gltf.get("accessors", [])
    for mi, m in enumerate(gltf.get("meshes", [])):
        for prim in m.get("primitives", []):
            pos_idx = prim.get("attributes", {}).get("POSITION")
            if pos_idx is None:
                continue
            acc = accessors[pos_idx]
            mn, mx = acc.get("min"), acc.get("max")
            if mn and mx:
                ext = [mx[i] - mn[i] for i in range(3)]
                print(f"  mesh[{mi}] {m.get('name', '')} min={mn} max={mx} extent={ext}")


if __name__ == "__main__":
    main()
