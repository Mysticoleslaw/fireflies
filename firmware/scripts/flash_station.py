"""Flash station: flashes every new D1 mini that gets plugged in, one after another.

For each new board: firmware (web app built in) over USB, reads its boot log for its name/id
(write it on the bug), then checks it joined the mesh via the master's API.

  python scripts/flash_station.py --count 13 --skip /dev/cu.usbserial-01BED3D4 \
      --master fireflies.local

Boards that fail are retried after you unplug and replug them. Progress is kept in
--state, so the station can be restarted without reflashing finished boards.
"""
from __future__ import annotations

import argparse
import glob
import json
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

import serial  # pyserial, installed alongside PlatformIO

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
PORT_PATTERNS = ["/dev/cu.usbserial*", "/dev/cu.wchusbserial*", "/dev/cu.usbmodem*"]
BOOT_LINE = re.compile(r"\[boot\] (.+) \(([0-9a-f]{12})\)")
MESH_TIMEOUT_S = 75  # a new bug needs ~10-30 s to find the master


def say(msg: str) -> None:
    print(msg, flush=True)


def connected_ports() -> set[str]:
    return {p for pattern in PORT_PATTERNS for p in glob.glob(pattern)}


def find_pio() -> str:
    beside_python = Path(sys.executable).with_name("pio")
    return str(beside_python) if beside_python.exists() else (shutil.which("pio") or "pio")


def pio(target: str, port: str) -> bool:
    cmd = [find_pio(), "run", "-e", "d1_mini", "-t", target, "--upload-port", port]
    result = subprocess.run(cmd, cwd=FIRMWARE_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        tail = [l for l in (result.stdout + result.stderr).splitlines() if l.strip()][-3:]
        say(f"   {target} failed: " + " | ".join(tail))
    return result.returncode == 0


def read_boot(port: str, seconds: float = 12) -> tuple[str, str] | None:
    """Resets the board and returns (name, id) from its boot line."""
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.dtr, s.rts = False, True
        time.sleep(0.1)
        s.rts = False
        end = time.time() + seconds
        while time.time() < end:
            match = BOOT_LINE.search(s.readline().decode("utf-8", "replace"))
            if match:
                return match.group(1), match.group(2)
    return None


def in_mesh(bug_id: str, masters: list[str]) -> str | None:
    """Waits for the bug to appear online in a master's roster; returns its role."""
    end = time.time() + MESH_TIMEOUT_S
    while time.time() < end:
        for host in masters:
            try:
                with urllib.request.urlopen(f"http://{host}/api/bugs", timeout=3) as res:
                    bugs = json.load(res)["bugs"]
            except (OSError, ValueError, KeyError):
                continue
            for bug in bugs:
                if bug["id"] == bug_id and bug["online"]:
                    return "master" if bug["master"] else "follower"
        time.sleep(3)
    return None


def apply_settings(bug_id: str, settings: str, masters: list[str]) -> bool:
    """Sends `settings` (query string like "glowPulsesMin=1&...") for one bug via the master."""
    query = urllib.parse.urlencode({"id": bug_id}) + "&" + settings
    for host in masters:
        try:
            req = urllib.request.Request(f"http://{host}/api/settings?{query}", method="POST")
            with urllib.request.urlopen(req, timeout=5):
                return True
        except OSError:
            continue
    return False


def load_state(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {"ports": [], "bugs": []}


def flash_one(port: str, number: int, masters: list[str], settings: str) -> dict | None:
    say(f"[{number}] {port}: flashing...")
    if not pio("upload", port):
        say(f"[{number}] FAILED on {port}: unplug it, check the cable, and plug it back in to retry")
        return None
    boot = read_boot(port)
    if not boot:
        say(f"[{number}] FAILED: {port} flashed but never printed a boot line: unplug/replug to retry")
        return None
    name, bug_id = boot
    role = in_mesh(bug_id, masters)
    mesh = f"joined the mesh as {role}" if role else f"did NOT show up in the mesh within {MESH_TIMEOUT_S} s"
    if role and settings:
        mesh += ", settings applied" if apply_settings(bug_id, settings, masters) else ", settings NOT applied"
    say(f"[{number}] OK  {name}  (id {bug_id})  {mesh}. Label it '{name.split()[-1]}', then plug in the next one")
    return {"port": port, "name": name, "id": bug_id, "mesh": role}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--count", type=int, default=13, help="how many new boards to flash")
    ap.add_argument("--skip", action="append", default=[], help="ports that are already done")
    ap.add_argument("--master", action="append", default=[], help="master address(es) to check the mesh")
    ap.add_argument("--state", type=Path, default=FIRMWARE_DIR / ".pio" / "flash_station.json")
    ap.add_argument("--settings", default="", help='applied to each bug once it joins, e.g. "glowPulsesMin=1&glowPulsesMax=6"')
    args = ap.parse_args()

    state = load_state(args.state)
    done = set(args.skip) | set(state["ports"])
    retry_after_unplug: set[str] = set()
    masters = args.master or ["fireflies.local"]
    say(f"Ready: {len(state['bugs'])}/{args.count} done. Plug in a board (already-done ports are skipped).")

    while len(state["bugs"]) < args.count:
        present = connected_ports()
        retry_after_unplug &= present  # unplugged failures become eligible again
        todo = sorted(present - done - retry_after_unplug)
        if not todo:
            time.sleep(1)
            continue
        port = todo[0]
        result = flash_one(port, len(state["bugs"]) + 1, masters, args.settings)
        if result is None:
            retry_after_unplug.add(port)
            continue
        done.add(port)
        state["ports"].append(port)
        state["bugs"].append(result)
        args.state.write_text(json.dumps(state, indent=2))

    say(f"All done: {len(state['bugs'])} boards flashed:")
    for bug in state["bugs"]:
        say(f"   {bug['name']}  {bug['id']}  {bug['mesh'] or 'NOT in mesh'}")


if __name__ == "__main__":
    main()
