#!/usr/bin/env python3
"""DX11 shader build driver (migration M2).

Reads materialsystem/stdshaders_dx11/manifest/shaders.json, compiles every
permutation with fxc.exe (Windows SDK) to SM 5.0 DXBC, and packs one .vcsx
file per shader into <out>/shaders/dx11/.

.vcsx v1 layout (little-endian, no compression yet):
    char[4]  magic   'VCSX'
    u32      version  1
    u32      flags    0 (bit0 reserved for LZMA later)
    u32      permutation count
    u64      source hash (fnv1a64 of hlsl bytes + defines + target + entry)
    repeated permutation records:
        u64 key, u32 offset (from file start), u32 size
    blobs (raw DXBC)
"""

import argparse
import glob
import itertools
import json
import os
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(REPO, "materialsystem", "stdshaders_dx11")
DEFAULT_OUT = os.path.join(REPO, "build_tf_x64", "tf")


def find_fxc():
    pattern = r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\fxc.exe"
    hits = sorted(glob.glob(pattern))
    if not hits:
        sys.exit("fxc.exe not found under Windows Kits; install the Windows 10/11 SDK")
    return hits[-1]


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def permutations(dimensions):
    """Yield (key, defines) for every combination; combo-style multipliers in
    declaration order (first dimension = innermost/lowest multiplier)."""
    if not dimensions:
        yield 0, []
        return
    value_lists = [d["values"] for d in dimensions]
    multipliers = []
    mult = 1
    for vals in value_lists:
        multipliers.append(mult)
        mult *= len(vals)
    for combo in itertools.product(*[range(len(v)) for v in value_lists]):
        key = sum(idx * m for idx, m in zip(combo, multipliers))
        defines = [(dimensions[i]["name"], str(value_lists[i][combo[i]]))
                   for i in range(len(dimensions))]
        yield key, defines


def compile_one(fxc, hlsl_path, entry, target, defines, debug):
    cmd = [fxc, "/nologo", "/T", target, "/E", entry]
    if debug:
        # /Zi embeds source for RenderDoc; /Od keeps it line-accurate
        cmd += ["/Zi", "/Od"]
    else:
        cmd += ["/O3"]
    for name, val in defines:
        cmd += ["/D", f"{name}={val}"]
    cmd += ["/Fo", "-", hlsl_path]
    # fxc can't write DXBC to stdout reliably; use a temp file instead.
    tmp = hlsl_path + f".{entry}.{target}.tmp.dxbc"
    cmd[cmd.index("-")] = tmp
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"fxc failed for {hlsl_path} [{entry} {target} {defines}]:\n{res.stderr or res.stdout}")
    with open(tmp, "rb") as f:
        blob = f.read()
    os.remove(tmp)
    return blob


def build_pack(fxc, src_root, out_dir, shader, debug):
    hlsl_path = os.path.join(src_root, "hlsl", shader["file"])
    with open(hlsl_path, "rb") as f:
        src_bytes = f.read()

    entries = []
    for key, defines in permutations(shader.get("dimensions", [])):
        blob = compile_one(fxc, hlsl_path, shader["entry"], shader["target"], defines, debug)
        entries.append((key, blob))

    hash_input = src_bytes + shader["entry"].encode() + shader["target"].encode()
    for key, defines in permutations(shader.get("dimensions", [])):
        hash_input += repr(defines).encode()
    src_hash = fnv1a64(hash_input)

    header_size = 4 + 4 + 4 + 4 + 8
    table_size = len(entries) * (8 + 4 + 4)
    offset = header_size + table_size

    pack_path = os.path.join(out_dir, shader["name"] + ".vcsx")
    with open(pack_path, "wb") as f:
        f.write(b"VCSX")
        f.write(struct.pack("<III", 1, 0, len(entries)))
        f.write(struct.pack("<Q", src_hash))
        for key, blob in entries:
            f.write(struct.pack("<QII", key, offset, len(blob)))
            offset += len(blob)
        for _, blob in entries:
            f.write(blob)
    return pack_path, len(entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT, help="game dir; packs go to <out>/shaders/dx11")
    ap.add_argument("--debug", action="store_true", help="compile with /Zi for RenderDoc source debug")
    args = ap.parse_args()

    fxc = find_fxc()
    with open(os.path.join(args.src, "manifest", "shaders.json")) as f:
        manifest = json.load(f)

    out_dir = os.path.join(args.out, "shaders", "dx11")
    os.makedirs(out_dir, exist_ok=True)

    for shader in manifest["shaders"]:
        path, count = build_pack(fxc, args.src, out_dir, shader, args.debug)
        print(f"  {shader['name']}: {count} permutation(s) -> {path}")
    print(f"fxc: {fxc}")


if __name__ == "__main__":
    main()
