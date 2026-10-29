"""Wireless update for every bug: followers first, then the master.

  python scripts/update_all.py                 # update bugs whose build differs
  python scripts/update_all.py --force         # update every online bug
  python scripts/update_all.py --only ee4624   # just some bugs (id or name suffix)

Builds once, asks the master to put the outdated followers into update mode (they join
home Wi-Fi with OTA open), pushes the firmware to each over OTA, updates the master last,
then checks every bug's build. (Followers can't rejoin until the master matches when a
release changes the bug-to-bug protocol, so builds are verified at the end, not per bug.)
Run with the PlatformIO venv's Python.
"""
from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
FIRMWARE_BIN = FIRMWARE_DIR / ".pio" / "build" / "d1_mini" / "firmware.bin"
ESPOTA = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif8266" / "tools" / "espota.py"
DEFAULT_MASTER = "fireflies.local"
UPDATE_MINUTES = 10
JOIN_TIMEOUT_S = 90    # followers joining home Wi-Fi in update mode
REJOIN_TIMEOUT_S = 90  # an updated bug rebooting and rejoining the mesh


def say(msg: str) -> None:
    print(msg, flush=True)


def secret(name: str) -> str:
    text = (FIRMWARE_DIR / "include" / "secrets.h").read_text()
    match = re.search(rf'#define {name} "([^"]*)"', text)
    if not match:
        sys.exit(f"{name} not found in include/secrets.h")
    return match.group(1)


def build() -> str:
    pio = Path(sys.executable).with_name("pio")
    result = subprocess.run([str(pio), "run", "-e", "d1_mini"], cwd=FIRMWARE_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("build failed:\n" + result.stdout[-2000:])
    header = (FIRMWARE_DIR / "src" / "generated" / "build_info.h").read_text()
    return re.search(r"FF_BUILD_ID 0x([0-9a-f]{8})", header).group(1)


def resolve(host: str) -> str:
    return socket.gethostbyname(host)  # resolve .local once; it's slow on macOS


def resolve_or_none(host: str) -> str | None:
    try:
        return resolve(host)
    except OSError:
        return None


def api(master: str, path: str, params: dict | None = None) -> dict:
    url = f"http://{master}{path}" + (f"?{urllib.parse.urlencode(params)}" if params else "")
    req = urllib.request.Request(url, method="POST" if params is not None else "GET")
    with urllib.request.urlopen(req, timeout=5) as res:
        return json.load(res)


def bugs(master: str) -> list[dict]:
    try:
        return api(master, "/api/bugs")["bugs"]
    except (OSError, ValueError):
        return []


def wait_for(master: str, predicate, timeout: float) -> dict | None:
    end = time.time() + timeout
    while time.time() < end:
        for bug in bugs(master):
            if predicate(bug):
                return bug
        time.sleep(3)
    return None


def push(ip: str, password: str) -> bool:
    cmd = [sys.executable, str(ESPOTA), "-i", ip, "-p", "8266", "-a", password, "-f", str(FIRMWARE_BIN)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


def update_follower(master: str, bug: dict, build_id: str, password: str) -> bool:
    name = bug["name"]
    try:
        api(master, "/api/update", {"id": bug["id"], "minutes": UPDATE_MINUTES})
    except OSError as err:
        say(f"  {name}: couldn't start update mode ({err})")
        return False
    ready = wait_for(master, lambda b: b["id"] == bug["id"] and b.get("ip"), JOIN_TIMEOUT_S)
    if not ready:
        say(f"  {name}: never joined Wi-Fi in update mode (out of range?)")
        return False
    say(f"  {name}: in update mode at {ready['ip']}, sending firmware...")
    if not push(ready["ip"], password):
        say(f"  {name}: upload failed (it returns to normal after {UPDATE_MINUTES} min)")
        return False
    say(f"  {name}: uploaded")
    return True


def update_master(master_ip: str, master: dict, password: str) -> bool:
    say(f"  {master['name']} (master) at {master_ip}: sending firmware...")
    ok = push(master_ip, password)
    say(f"  {master['name']}: {'uploaded' if ok else 'upload failed'}")
    return ok


def verify(hosts: list[str], ids: list[str], build_id: str) -> list[str]:
    """Waits for the mesh to settle on the new build; returns ids not confirmed."""
    pending = set(ids)
    end = time.time() + REJOIN_TIMEOUT_S
    while pending and time.time() < end:
        time.sleep(5)
        for ip in {resolve_or_none(h) for h in hosts} - {None}:
            for bug in bugs(ip):
                if bug["online"] and bug.get("build") == build_id:
                    pending.discard(bug["id"])
    return sorted(pending)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--master", default=DEFAULT_MASTER)
    ap.add_argument("--force", action="store_true", help="update bugs even if they run this build")
    ap.add_argument("--only", nargs="*", default=[], help="bug ids or name suffixes")
    args = ap.parse_args()

    password = secret("FF_OTA_PASSWORD")
    say("Building...")
    build_id = build()
    master_ip = resolve(args.master)
    everyone = bugs(master_ip)
    if not everyone:
        sys.exit(f"no bugs answered at {args.master} ({master_ip})")
    master = next(b for b in everyone if b["master"])
    wanted = lambda b: b["online"] and (args.force or b.get("build") != build_id) and (
        not args.only or any(b["id"].endswith(o) or b["name"].endswith(o) for o in args.only))
    followers = [b for b in everyone if not b["master"] and wanted(b)]
    say(f"Build {build_id}. Master {master['name']} at {master_ip}. "
        f"{len(followers)} follower(s) to update{', then the master' if wanted(master) else ''}.")

    targets = followers + ([master] if wanted(master) else [])
    uploaded = [b for b in followers if update_follower(master_ip, b, build_id, password)]
    if wanted(master) and update_master(master_ip, master, password):
        uploaded.append(master)
    say("Waiting for everyone to come back on the new build...")
    names = {b["id"]: b["name"] for b in targets}
    # The master role can move during a rollout, so look wherever the master might be now.
    unconfirmed = verify([master_ip, args.master, DEFAULT_MASTER] + [b["ip"] for b in followers if b.get("ip")],
                         [b["id"] for b in uploaded], build_id)
    failed = [names[b["id"]] for b in targets if b not in uploaded] + [names[i] for i in unconfirmed]
    say(f"Done: {len(targets) - len(failed)}/{len(targets)} on build {build_id}."
        + (f" Check: {', '.join(failed)}" if failed else ""))


if __name__ == "__main__":
    main()
