#!/usr/bin/env python3
"""Public-file SC10 overview/profile acceptance using owned native windows.

Standard library only. Copies shared-design into a temporary Unicode path;
uses the public CLI and the same native input as geometric-selection acceptance.
Linux requires DISPLAY and xdotool. --keep-temp retains PNGs and CLI trace.
"""
from __future__ import annotations

import argparse
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("selection_tests", ROOT / "scripts/test-geometric-selection.py")
selection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection)
session = selection.session
require = selection.require


def wheel(native, amount, x=250, y=400):
    native.event("move", x, y)
    if os.name == "nt":
        point = native.W.POINT(round(x * native.scale), round(y * native.scale))
        native.user.ClientToScreen(native.hwnd, C.byref(point))
        require(native.user.PostMessageW(native.hwnd, 0x20A, (amount * 120 & 0xffff) << 16,
                                         (point.y << 16) | (point.x & 0xffff)), "Wheel PostMessage failed")
    else:
        for _ in range(abs(amount)):
            for kind in (4, 5):
                event = native.Event()
                m = event.mouse
                m.type, m.display, m.window, m.root, m.same_screen = kind, native.display, native.window, native.root, 1
                m.x = m.x_root = round(x * native.scale)
                m.y = m.y_root = round(y * native.scale)
                m.button = 4 if amount > 0 else 5
                native.x.XSendEvent(native.display, native.window, 0, 4 if kind == 4 else 8, C.byref(event))
                native.x.XFlush(native.display)
                time.sleep(.05)
    time.sleep(.2)


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-overview-"))
    print(f"Overview acceptance evidence: {folder}", flush=True)
    project = folder / "Prøva Δ" / "Modelli città 日本"
    project.parent.mkdir()
    shutil.copytree(session.FIXTURES / "shared-design", project)
    config = project / "synthcad.json"
    metadata = json.loads(config.read_text(encoding="utf-8"))
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 35)
    client.session_dir, client.trace_path = folder / "sessions", folder / "cli-trace.jsonl"
    pids = []
    name = "overview-test"
    def call(*cmd, **kwargs):
        return client.call(*cmd, "-s", name, **kwargs)
    def revision():
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            result = call("revision", expected_code=(0, 11))
            if result["ok"]:
                return session._revision(result)
            time.sleep(.1)
        raise RuntimeError("Revision stayed busy")
    def settle(previous=None, error=0):
        deadline = time.monotonic() + 25
        while True:
            current = revision()
            if previous is None or current != previous:
                return call("wait", "--revision", current, "--timeout", "25000", expected_code=error)
            require(time.monotonic() < deadline, "Public-file edit did not change displayed revision")
            time.sleep(.1)
    def write_manifest():
        before = revision()
        config.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
        settle(before)
    def overview():
        return call("overview")["data"]
    def snapshot():
        return call("snapshot")["data"]
    def screenshot(label):
        destination = folder / (label + ".png")
        call("screenshot", str(destination))
        require(destination.read_bytes().startswith(b"\x89PNG"), "Screenshot missing")
    try:
        opened = client.call("open", str(project), "--hidden", "--session", name)
        pids.extend(pid for pid, _ in session._pid_records(opened))
        require(pids, "Open did not return owned viewer PID")
        settle()
        native = selection.WindowsInput(pids[0]) if os.name == "nt" else selection.X11Input(pids[0])
        if args.expected_scale is not None:
            require(abs(native.scale - args.expected_scale) < .02, "Requested DPI scale was not applied")
        print(f"Owned window PID {pids[0]}: {native.width}x{native.height}, UI scale {native.scale}", flush=True)
        initial = overview()
        require(call("profile")["data"] == initial["profile"], "Profile CLI and overview context differ")
        require(initial["profile"]["status"] == "incomplete" and initial["profile"]["profileRevision"] is None,
                "Omitted project profile should be explicit incomplete context")
        require({v["id"] for v in initial["views"]} == {"assembly", "inspection", "plate-1", "plate-2"}, "View registry incomplete")
        require(all(not v["loaded"] and v["modelRevision"] is None for v in initial["views"] if v["id"] != "assembly"), "Unvisited views fabricated model revisions")
        baseline = snapshot()
        selection.click(native, [322, 77])
        screenshot("empty-overview")
        camera = snapshot()["camera"]
        wheel(native, -4)
        require(snapshot()["camera"] == camera, "Overview scrolling moved camera")
        screenshot("empty-profile")
        wheel(native, 30)
        # View buttons have content-dependent wrapped rows. Search only the
        # overview body at its documented button column, then scroll its body.
        switched = False
        for _ in range(4):
            for y in range(170, 510, 16):
                selection.click(native, [100, y])
                if overview().get("activeView") == "inspection":
                    settle()
                    switched = True
                    break
            if switched:
                break
            wheel(native, -3)
        require(switched, "Native overview named inspection view button did not switch views")
        screenshot("native-named-view")
        call("view", "assembly")
        settle()
        wheel(native, 100)
        selection.click(native, [484, 40])
        close_camera = snapshot()["camera"]
        selection.click(native, [322, 77])
        selection.click(native, [484, 40])
        require(snapshot()["camera"] == close_camera, "Overview Close moved camera")

        metadata.update({"activeProfile": "custom", "profiles": {"custom": {
            "name": "Misurato città 日本", "printer": {"id": "user-machine", "name": "Measured machine"},
            "buildVolume": [210, 205, 195], "exclusions": [{"name": "clip", "min": [0, 0], "max": [8, 10]}],
            "nozzleDiameter": .4, "material": {"id": "user-material", "name": "User material"},
            "provenance": {"type": "user", "description": "Measured by owner"}}}})
        write_manifest()
        profiled = overview()
        require(profiled["profile"]["status"] == "complete", "Complete custom profile unavailable")
        require(profiled["modelRevision"] == initial["modelRevision"], "Profile-only update changed model revision")
        basis = {"view": "assembly", "modelRevision": profiled["modelRevision"], "profileRevision": profiled["profile"]["profileRevision"]}
        metadata.update({"measurements": [{"id": "width", "name": "Larghezza città 日本", "value": 24, "unit": "mm", "status": "measured", "notes": "Owner measurement"}],
                         "assumptions": [{"id": "fit", "text": "Provisional clearance", "status": "provisional"}],
                         "checks": [{"id": "fit", "name": "Authored fit", "result": "passed", "scope": "heuristic", "basis": basis}],
                         "exports": [{"id": "old-export", "path": "unopened-missing.stl", "format": "stl", "basis": basis}],
                         "evidence": [{"id": "proposal", "text": "Proposed fit trial", "stage": "proposed", "basis": basis},
                                      {"id": "unvisited", "text": "Plate proposal", "stage": "proposed", "basis": dict(basis, view="plate-1")}]})
        before = revision()
        write_manifest()
        current = overview()
        require(revision() != before and current["modelRevision"] == basis["modelRevision"], "Authored records self-invalidated model revision")
        require(current["checks"][0]["freshness"] == "current" and current["checks"][0]["origin"] == "authored", "Authored current basis unavailable")
        require(current["evidence"][1]["freshness"] == "unknown" and current["evidence"][0]["stage"] == "proposed", "Unvisited evidence or stage inferred")
        selection.click(native, [322, 77])
        screenshot("metadata-overview")
        wheel(native, -10)
        screenshot("custom-profile")
        wheel(native, -30)
        screenshot("authored-records")
        wheel(native, 60)
        selection.click(native, [484, 40])

        metadata["measurements"].append({"id": "bad", "value": "invalid"})
        write_manifest()
        partial = overview()
        require(partial["status"] == "partial" and len(partial["measurements"]) == 1 and partial["errors"], "Malformed metadata sibling discarded valid measurement")
        require(snapshot()["parts"] and session._find(call("state"), "exportValid") is True, "Optional metadata error blocked geometry/export")
        selection.click(native, [322, 77])
        wheel(native, -100)
        screenshot("metadata-errors")
        wheel(native, 100)
        selection.click(native, [484, 40])

        imported = project / "parts.js"
        before = revision()
        imported.write_text(imported.read_text(encoding="utf-8") + "\n// source revision change\n", encoding="utf-8")
        settle(before)
        changed = overview()
        require(changed["checks"][0]["freshness"] == "stale" and changed["exports"][0]["freshness"] == "stale", "Imported source change did not stale authored records")
        metadata["checks"][0]["basis"] = dict(basis, modelRevision=changed["modelRevision"])
        write_manifest()
        require(overview()["checks"][0]["freshness"] == "current", "Rebased check unavailable")
        metadata["profiles"]["custom"]["nozzleDiameter"] = .6
        metadata["profiles"]["custom"]["provisional"] = ["nozzleDiameter"]
        write_manifest()
        require(overview()["checks"][0]["freshness"] == "stale", "Known changed provisional profile did not stale prior check")

        second = folder / "Second project"
        shutil.copytree(session.FIXTURES / "shared-design", second)
        other = client.call("open", str(second), "--hidden", "--session", "overview-other")
        pids.extend(pid for pid, _ in session._pid_records(other))
        deadline = time.monotonic() + 25
        while True:
            other_rev = client.call("revision", "-s", "overview-other", expected_code=(0, 11))
            if other_rev["ok"]:
                break
            require(time.monotonic() < deadline, "Second project revision stayed busy")
            time.sleep(.1)
        client.call("wait", "--revision", session._revision(other_rev), "-s", "overview-other")
        isolated = client.call("overview", "-s", "overview-other")["data"]
        require(isolated["profile"]["activeProfile"] is None and isolated["profile"]["profileRevision"] is None,
                "Profile leaked into second project")

        before = revision()
        config.write_text(json.dumps(dict(metadata, defaultView="missing")), encoding="utf-8")
        settle(before, error=5)
        require(session._find(call("state"), "exportValid") is False, "Required manifest error left export valid")
        retained = overview()
        require(retained["metadataCurrent"] is False and retained["profile"]["status"] == "invalid",
                "Broken required manifest presented last-loaded metadata as current")
        screenshot("required-manifest-error")
        before = revision()
        config.write_text(json.dumps(metadata, ensure_ascii=False), encoding="utf-8")
        settle(before)
        require(snapshot()["parts"] and session._find(call("state"), "exportValid") is True, "Required manifest recovery failed")
        require(overview()["metadataCurrent"] is True, "Recovered metadata stayed marked unavailable")
        require({p["id"]: p["exportable"] for p in snapshot()["parts"]} ==
                {p["id"]: p["exportable"] for p in baseline["parts"]}, "Overview operations changed export flags")
        print("PASS project overview, profiles, freshness, public-file errors/recovery, native UI and camera isolation", flush=True)
    finally:
        for pid in dict.fromkeys(pids):
            try:
                session._terminate_owned_pid(pid)
            except OSError:
                # Container PID 1 may retain an already-terminated child as a
                # zombie; avoid hiding a meaningful test failure with cleanup.
                if os.name != "nt" and Path(f"/proc/{pid}/stat").exists():
                    require(Path(f"/proc/{pid}/stat").read_text().split()[2] == "Z", f"Owned viewer PID {pid} still running")
                elif os.name == "nt":
                    raise
        if not args.keep_temp:
            shutil.rmtree(folder)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    parser.add_argument("--viewer")
    parser.add_argument("--keep-temp", action="store_true")
    parser.add_argument("--expected-scale", type=float, help="Assert actual viewer scale before native input")
    run(parser.parse_args())
