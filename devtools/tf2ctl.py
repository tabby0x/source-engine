#!/usr/bin/env python3
"""tf2ctl - drive a running TF2 instance for automated testing.

Transport is the engine's dev-automation channel (engine/devautomation.cpp):
a host-local named pipe (\\.\pipe\tf2devctl) enabled by -devautomation.
Nothing is exposed on the network and no input injection is involved.
Asynchronous "callbacks" come from tailing console.log (-condebug):
`exec --wait REGEX` sends a command and blocks until the regex shows up in
NEW console output; `wait REGEX` just blocks. No hardcoded sleeps.

Typical session:
  py -3 tf2ctl.py launch --backend dx11 --map ctf_2fort
  py -3 tf2ctl.py exec "dev_dismiss_ui"
  py -3 tf2ctl.py exec "jointeam spectate" --wait "joined team"
  py -3 tf2ctl.py exec "spec_mode 6"
  py -3 tf2ctl.py exec "spec_goto 0 -200 480 75 90"
  py -3 tf2ctl.py exec "getpos" --wait "^setpos"
  py -3 tf2ctl.py exec "jpeg shot_name 100"
  py -3 tf2ctl.py kill
"""

import argparse
import os
import re
import subprocess
import sys
import time

GAME_DIR = r"C:\Github\source-engine\build_tf_x64"
GAME_EXE = os.path.join(GAME_DIR, "hl2_launcher.exe")
CONSOLE_LOG = os.path.join(GAME_DIR, "tf", "console.log")
SCREENSHOT_DIR = os.path.join(GAME_DIR, "tf", "screenshots")
PIPE_PATH = r"\\.\pipe\tf2devctl"


# ---------------------------------------------------------------- pipe
def pipe_send(command, timeout=10.0):
    """Send one console command over the dev-automation pipe."""
    deadline = time.monotonic() + timeout
    last_err = None
    while time.monotonic() < deadline:
        try:
            with open(PIPE_PATH, "r+b", buffering=0) as pipe:
                pipe.write((command + "\n").encode("utf-8"))
                reply = pipe.read(64)
                return reply.decode("utf-8", "replace").strip()
        except OSError as e:
            last_err = e
            time.sleep(0.5)
    raise ConnectionError(f"dev pipe unavailable: {last_err}")


# ---------------------------------------------------------------- log tail
def log_size():
    try:
        return os.path.getsize(CONSOLE_LOG)
    except OSError:
        return 0


def wait_for(pattern, offset, timeout):
    """Block until `pattern` appears in console.log past byte `offset`.
    Returns the matching line, or None on timeout."""
    rx = re.compile(pattern)
    deadline = time.monotonic() + timeout
    pos = offset
    tail = ""
    while time.monotonic() < deadline:
        try:
            with open(CONSOLE_LOG, "r", encoding="utf-8", errors="replace") as f:
                f.seek(pos)
                new = f.read()
        except OSError:
            new = ""
        if new:
            pos += len(new.encode("utf-8", "replace"))
            buf = tail + new
            lines = buf.split("\n")
            tail = lines.pop()  # partial last line
            for line in lines:
                if rx.search(line):
                    return line
        time.sleep(0.25)
    return None


# ---------------------------------------------------------------- actions
def do_launch(args):
    subprocess.run(["taskkill", "/F", "/IM", "hl2_launcher.exe"],
                   capture_output=True)
    time.sleep(2)
    if os.path.exists(CONSOLE_LOG):
        os.remove(CONSOLE_LOG)

    cmd = [GAME_EXE, "-game", "tf", "-insecure", "-novid", "-windowed",
           "-w", str(args.width), "-h", str(args.height), "-condebug",
           "-devautomation",
           "+sv_cheats", "1", "+fps_max", "60", "+mat_queue_mode", "0"]
    if args.backend == "dx11":
        cmd += ["-shaderapi", "shaderapidx11"]
    for extra in args.arg or []:
        cmd += extra.split()
    if args.map:
        cmd += ["+map", args.map]
    subprocess.Popen(cmd, cwd=GAME_DIR)

    # The pipe answers as soon as Host_Init ran; then wait for the map.
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        try:
            if pipe_send("ping", timeout=2) == "pong":
                break
        except ConnectionError:
            time.sleep(1)
    else:
        sys.exit(f"TIMEOUT waiting for dev pipe ({args.timeout}s)")
    print("PIPE: ready")

    if args.map:
        line = wait_for(r"Redownloading all lightmaps|^\S+ connected$", 0,
                        args.timeout)
        if line is None:
            sys.exit(f"TIMEOUT waiting for map load ({args.timeout}s)")
        print(f"LOADED: {line.strip()}")


def do_exec(args):
    offset = log_size()
    reply = pipe_send(args.command)
    print(f"PIPE: {reply}")
    if args.wait:
        line = wait_for(args.wait, offset, args.timeout)
        if line is None:
            sys.exit(f"TIMEOUT waiting for /{args.wait}/ ({args.timeout}s)")
        print(f"MATCH: {line.strip()}")


def do_wait(args):
    line = wait_for(args.pattern, log_size() if args.new_only else 0,
                    args.timeout)
    if line is None:
        sys.exit(f"TIMEOUT waiting for /{args.pattern}/ ({args.timeout}s)")
    print(f"MATCH: {line.strip()}")


def _tga_to_png(tga_path, png_path):
    """Convert an uncompressed BGR(A) TGA (engine `screenshot` output) to PNG
    using only the stdlib."""
    import struct
    import zlib

    with open(tga_path, "rb") as f:
        hdr = f.read(18)
        id_len, _, img_type = hdr[0], hdr[1], hdr[2]
        w, h = struct.unpack("<HH", hdr[12:16])
        bpp = hdr[16]
        descriptor = hdr[17]
        if img_type != 2 or bpp not in (24, 32):
            raise ValueError(f"unsupported TGA (type {img_type}, {bpp}bpp)")
        f.seek(id_len, 1)
        data = f.read(w * h * (bpp // 8))

    nch = bpp // 8
    top_down = bool(descriptor & 0x20)
    rows = []
    for y in range(h):
        src_y = y if top_down else h - 1 - y
        row = bytearray()
        base = src_y * w * nch
        for x in range(w):
            o = base + x * nch
            b, g, r = data[o], data[o + 1], data[o + 2]
            row += bytes((r, g, b))
        rows.append(b"\x00" + bytes(row))

    def chunk(tag, payload):
        out = struct.pack(">I", len(payload)) + tag + payload
        return out + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    with open(png_path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 6)))
        f.write(chunk(b"IEND", b""))


def do_shot(args):
    """In-engine screenshot via the `screenshot` console command (backbuffer
    TGA in tf/screenshots), converted to PNG. No window capture involved."""
    before = set(os.listdir(SCREENSHOT_DIR)) if os.path.isdir(SCREENSHOT_DIR) else set()
    pipe_send("screenshot")
    deadline = time.monotonic() + args.timeout
    new_tga = None
    while time.monotonic() < deadline:
        if os.path.isdir(SCREENSHOT_DIR):
            fresh = [f for f in set(os.listdir(SCREENSHOT_DIR)) - before
                     if f.lower().endswith(".tga")]
            if fresh:
                new_tga = os.path.join(SCREENSHOT_DIR, fresh[0])
                # wait for the write to finish (size stable)
                last = -1
                while True:
                    size = os.path.getsize(new_tga)
                    if size == last and size > 0:
                        break
                    last = size
                    time.sleep(0.25)
                break
        time.sleep(0.25)
    if not new_tga:
        sys.exit("TIMEOUT waiting for screenshot")
    if args.out:
        _tga_to_png(new_tga, args.out)
        print(f"SHOT: {args.out}")
    else:
        print(f"SHOT: {new_tga}")


def do_kill(args):
    """Graceful first: a clean 'quit' lets Steam close the app session
    properly. Repeated taskkill /F left the Steam client wedged — it stopped
    answering GetAuthTicketForWebApi entirely (loadouts silently failed)
    until a Steam restart."""
    try:
        pipe_send("quit", timeout=2)
        for _ in range(40):  # up to 10s for a clean exit
            r = subprocess.run(
                ["tasklist", "/fi", "imagename eq hl2_launcher.exe"],
                capture_output=True, text=True)
            if "hl2_launcher.exe" not in (r.stdout or ""):
                print("quit (graceful)")
                return
            time.sleep(0.25)
    except (ConnectionError, OSError):
        pass
    subprocess.run(["taskkill", "/F", "/IM", "hl2_launcher.exe"],
                   capture_output=True)
    print("killed (forced)")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    lp = sub.add_parser("launch", help="launch TF2 and wait for map load")
    lp.add_argument("--backend", choices=["dx9", "dx11"], default="dx9")
    lp.add_argument("--map", default=None)
    lp.add_argument("--width", type=int, default=1280)
    lp.add_argument("--height", type=int, default=720)
    lp.add_argument("--timeout", type=float, default=150)
    lp.add_argument("--arg", action="append",
                    help="extra launch args (repeatable, space-split)")
    lp.set_defaults(func=do_launch)

    ep = sub.add_parser("exec", help="run a console command via the dev pipe")
    ep.add_argument("command")
    ep.add_argument("--wait", default=None,
                    help="block until this regex appears in new console output")
    ep.add_argument("--timeout", type=float, default=60)
    ep.set_defaults(func=do_exec)

    wp = sub.add_parser("wait", help="block until regex appears in console.log")
    wp.add_argument("pattern")
    wp.add_argument("--timeout", type=float, default=60)
    wp.add_argument("--new-only", action="store_true",
                    help="only match output produced after this call")
    wp.set_defaults(func=do_wait)

    sp = sub.add_parser("shot", help="in-engine screenshot (jpeg command)")
    sp.add_argument("--out", default=None, help="copy the shot to this path")
    sp.add_argument("--timeout", type=float, default=15)
    sp.set_defaults(func=do_shot)

    kp = sub.add_parser("kill", help="kill the game")
    kp.set_defaults(func=do_kill)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
