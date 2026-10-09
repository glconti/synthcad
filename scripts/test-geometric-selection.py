#!/usr/bin/env python3
"""Native-window geometric selection acceptance test (standard library only).

Creates and closes its own hidden viewer. Windows sends window-local messages;
Linux sends X11 events directly to the test PID's window (requires DISPLAY and
xdotool for window discovery only; suitable for Xvfb). Install xclip to verify
the real X11 clipboard using a separate client. No input-control CLI or
viewer test hooks are used. --keep-temp retains screenshots and CLI trace.
"""
from __future__ import annotations

import argparse
import ctypes as C
import ctypes.util
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("session_tests", ROOT / "scripts/test-agent-session.py")
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class WindowsInput:
    def __init__(self, pid):
        from ctypes import wintypes as W
        self.W = W
        self.user = C.WinDLL("user32", use_last_error=True)
        self.user.SetProcessDPIAware()
        self.user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        self.user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.user.GetDpiForWindow.argtypes = [W.HWND]
        self.hwnd = None
        callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def visit(hwnd, unused):
            owner = W.DWORD()
            self.user.GetWindowThreadProcessId(hwnd, C.byref(owner))
            rect = W.RECT()
            self.user.GetClientRect(hwnd, C.byref(rect))
            if owner.value == pid and rect.right > 600 and rect.bottom > 350:
                self.hwnd = hwnd
            return True
        callback = callback_type(visit)
        deadline = time.monotonic() + 10
        while self.hwnd is None and time.monotonic() < deadline:
            self.user.EnumWindows(callback, 0)
            time.sleep(.1)
        require(self.hwnd is not None, f"No native viewer window for owned PID {pid}")
        rect = W.RECT()
        self.user.GetClientRect(self.hwnd, C.byref(rect))
        self.width, self.height = rect.right, rect.bottom
        # The viewer opens at 1280x720 logical pixels. GLFW's content scale on
        # a hidden window can differ from GetDpiForWindow; use its actual
        # initial client size to recover the scale the viewer chose.
        self.scale = self.width / 1280
        require(abs(self.height / self.scale - 720) < 2, "Unexpected initial viewer size")

    def event(self, kind, x, y):
        x, y = round(x * self.scale), round(y * self.scale)
        msg = {"move": 0x200, "down": 0x201, "up": 0x202}[kind]
        require(self.user.PostMessageW(self.hwnd, msg, 1 if kind == "down" else 0,
                                      (y << 16) | (x & 0xffff)), "PostMessage failed")
        time.sleep(.09)

    def clipboard(self):
        self.user.GetClipboardData.argtypes = [C.c_uint]
        self.user.GetClipboardData.restype = C.c_void_p
        kernel = C.WinDLL("kernel32", use_last_error=True)
        kernel.GlobalLock.argtypes = [C.c_void_p]
        kernel.GlobalLock.restype = C.c_void_p
        kernel.GlobalUnlock.argtypes = [C.c_void_p]
        deadline = time.monotonic() + 3
        while not self.user.OpenClipboard(None):
            if C.get_last_error() == 5:
                raise PermissionError("Windows denied clipboard access (error 5)")
            require(time.monotonic() < deadline, f"OpenClipboard failed: Windows error {C.get_last_error()}")
            time.sleep(.05)
        try:
            handle = self.user.GetClipboardData(13)  # CF_UNICODETEXT
            require(handle, "Copy produced no Unicode clipboard text")
            pointer = kernel.GlobalLock(handle)
            require(pointer, "GlobalLock clipboard failed")
            try:
                return C.wstring_at(pointer)
            finally:
                kernel.GlobalUnlock(handle)
        finally:
            self.user.CloseClipboard()


class X11Input:
    """XSendEvent addresses one owned window, without moving the global cursor."""
    def __init__(self, pid):
        require(os.environ.get("DISPLAY"), "Linux UI testing requires an X11 DISPLAY (e.g. Xvfb)")
        require(shutil.which("xdotool"), "Linux window discovery requires xdotool")
        self.x = C.CDLL(ctypes.util.find_library("X11"))
        self.x.XOpenDisplay.argtypes = [C.c_char_p]
        self.x.XOpenDisplay.restype = C.c_void_p
        self.display = self.x.XOpenDisplay(None)
        require(self.display, "Cannot open X11 display")
        self.x.XDefaultRootWindow.argtypes = [C.c_void_p]
        self.x.XDefaultRootWindow.restype = C.c_ulong
        self.root = self.x.XDefaultRootWindow(self.display)
        deadline = time.monotonic() + 10
        self.window = None
        while time.monotonic() < deadline:
            found = subprocess.run(["xdotool", "search", "--pid", str(pid)], capture_output=True, text=True)
            if found.returncode == 0 and found.stdout.strip():
                self.window = int(found.stdout.splitlines()[0]); break
            time.sleep(.1)
        require(self.window, f"No X11 window for owned PID {pid}")
        geometry = subprocess.check_output(["xdotool", "getwindowgeometry", "--shell", str(self.window)], text=True)
        fields = dict(line.split("=", 1) for line in geometry.splitlines() if "=" in line)
        self.width, self.height = int(fields["WIDTH"]), int(fields["HEIGHT"])
        self.scale = self.width / 1280
        require(abs(self.height / self.scale - 720) < 2, "Unexpected initial viewer size")
        class Mouse(C.Structure):
            _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                        ("display", C.c_void_p), ("window", C.c_ulong), ("root", C.c_ulong),
                        ("subwindow", C.c_ulong), ("time", C.c_ulong), ("x", C.c_int),
                        ("y", C.c_int), ("x_root", C.c_int), ("y_root", C.c_int),
                        ("state", C.c_uint), ("button", C.c_uint), ("same_screen", C.c_int)]
        class Event(C.Union):
            _fields_ = [("mouse", Mouse), ("pad", C.c_long * 24)]
        self.Event = Event
        self.x.XSendEvent.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_long, C.POINTER(Event)]
        self.x.XFlush.argtypes = [C.c_void_p]

    def event(self, kind, x, y):
        event = self.Event()
        m = event.mouse
        m.type = {"move": 6, "down": 4, "up": 5}[kind]
        m.display, m.window, m.root, m.same_screen = self.display, self.window, self.root, 1
        x, y = round(x * self.scale), round(y * self.scale)
        m.x, m.y, m.x_root, m.y_root = x, y, x, y
        m.button = 1 if kind != "move" else 0
        m.state = 256 if kind == "up" else 0
        mask = {"move": 64, "down": 4, "up": 8}[kind]
        require(self.x.XSendEvent(self.display, self.window, 0, mask, C.byref(event)), "XSendEvent failed")
        self.x.XFlush(self.display)
        time.sleep(.09)

    def clipboard(self):
        if not shutil.which("xclip"):
            raise FileNotFoundError("xclip missing; install xclip to verify X11 clipboard ownership")
        return subprocess.check_output(["xclip", "-selection", "clipboard", "-o"],
                                       timeout=5).decode("utf-8")


def click(native, point):
    native.event("move", *point)
    native.event("down", *point)
    native.event("up", *point)


def subtract(a, b):
    return [x - y for x, y in zip(a, b)]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(a):
    length = math.sqrt(dot(a, a))
    return [x / length for x in a]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def project(point, camera, width, height):
    # Exact CAD-to-render coordinate convention used by the viewer.
    world = [point[0] * .1, point[2] * .1, -point[1] * .1]
    forward = unit(subtract(camera["target"], camera["position"]))
    right = unit(cross(forward, camera["up"]))
    up = cross(right, forward)
    relative = subtract(world, camera["position"])
    depth = dot(relative, forward)
    require(depth > 0, "Test point lies behind camera")
    focal = height / (2 * math.tan(math.radians(camera["fovy"]) / 2))
    return [width / 2 + focal * dot(relative, right) / depth,
            height / 2 - focal * dot(relative, up) / depth]


def fixture(folder):
    (folder / "parts.js").write_text("export const size=20;\nexport const box=cube({size:[size,20,20],center:true});\n"
                                     "export const round=cylinder({height:20,radius:10,center:true});\n", encoding="utf-8")
    design = {"schemaVersion": 1, "defaultView": "assembly",
              "parts": [{"id": "box-source", "name": "Cube source", "solid": "BOX"},
                        {"id": "round-source", "name": "Cylinder source", "solid": "ROUND", "exportable": False}],
              "instances": [{"id": "cube", "name": "Cube", "part": "box-source"},
                            {"id": "cylinder", "name": "Cylinder", "part": "round-source", "transform": {"translate": [40, 0, 0]}}],
              "views": [{"id": "assembly", "name": "Assembly", "kind": "assembly", "members": [{"instance": "cube"}, {"instance": "cylinder"}]},
                        {"id": "inspection", "name": "Inspection", "kind": "inspection", "members": [{"instance": "cube"}, {"instance": "cylinder"}],
                         "placements": {"cube": {"translate": [0, 20, 0]}}}]}
    text = json.dumps(design).replace('"BOX"', "box").replace('"ROUND"', "round")
    (folder / "design.js").write_text("import {box,round} from './parts.js';\nexport const design=" + text + ";\n", encoding="utf-8")
    (folder / "synthcad.json").write_text(json.dumps({"schemaVersion": 1, "defaultView": "assembly",
                                                    "views": {"assembly": "design.js", "inspection": "design.js"}}), encoding="utf-8")


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-selection-"))
    print(f"Test evidence directory: {folder}", flush=True)
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 30)
    client.session_dir, client.trace_path = folder / 'projects', folder / "cli-trace.jsonl"
    pid = None
    try:
        fixture(folder)
        opened = client.call('project', 'open', str(folder), "--hidden", '--name', "selection-test")
        records = session._pid_records(opened)
        require(records, "Open response did not report the owned viewer PID")
        pid = records[0][0]
        def call(*cmd, **kwargs):
            return client.call(*cmd, '--project', "selection-test", **kwargs)
        def settle():
            deadline = time.monotonic() + 20
            while True:
                revision = call('project', 'revision', expected_code=(0, 11))
                if revision["ok"]:
                    return call('project', 'wait', "--revision", session._revision(revision))["revision"]
                require(time.monotonic() < deadline, "Initial dependencies did not become ready")
                time.sleep(.1)
        settle()
        native = WindowsInput(pid) if os.name == "nt" else X11Input(pid)
        if args.expected_scale is not None:
            require(abs(native.scale - args.expected_scale) < .02, "Requested DPI scale was not applied by the viewer")
        width, height = native.width / native.scale, native.height / native.scale
        print(f"Owned window PID {pid}: {native.width}x{native.height}, UI scale {native.scale}", flush=True)
        def snapshot():
            return call('project', 'inspect')["data"]
        def selection():
            return call('review', 'selection')["data"]["selection"]
        def mode(index):
            # SelectionUi::Layout: 360x116 card, bottom-right 12px inset.
            click(native, [width - 372 + 10 + index * 86.25 + 40.625, height - 128 + 21])
        def pick(kind, point, owner="cube"):
            click(native, project(point, snapshot()["camera"], width, height))
            selected = selection()
            require(selected and selected.get("geometry", {}).get("kind") == kind,
                    f"Native click expected {kind}, got {selected}")
            geometry = selected["geometry"]
            require(geometry["partId"] == owner, f"Wrong native click owner: {geometry}")
            require(all(key in selected for key in ("key", "name", "group", "partIds")), "Legacy selection fields lost")
            resolved = call('review', 'selection', geometry["reference"])["data"]["geometry"]
            require(resolved["kind"] == kind and resolved["partId"] == owner and
                    resolved["positionKind"] == "representative", "Copied reference did not resolve current context")
            return geometry
        call('review', 'frame', "cube")
        baseline = snapshot()
        flags = {p["id"]: p["exportable"] for p in baseline["parts"]}
        mode(0)
        pick("part", [0, 0, 0])
        # Visibility icons are independent from export; a hidden owner cannot
        # intercept a viewport pick, and restoring it preserves export flags.
        click(native, [320, 215])
        require(not next(p for p in snapshot()["parts"] if p["id"] == "cube")["visible"], "Tree eye did not hide cube")
        click(native, project([0, 0, 0], snapshot()["camera"], width, height))
        require(selection() is None, "Hidden geometry remained pickable")
        click(native, [320, 215])
        pick("part", [0, 0, 0])
        camera = snapshot()["camera"]
        mode(1)
        require(snapshot()["camera"] == camera, "Selection mode button moved the camera")
        face = pick("planar-face", [0, 0, 10])
        require(face["sourcePartId"] == "box-source" and face["instanceId"] == "cube", "Model identity context missing")
        mode(2)
        pick("edge", [10, 0, 10])
        mode(3)
        pick("vertex", [10, -10, 10])
        call('review', 'screenshot', str(folder / "vertex-selection.png"))
        token = selection()["geometry"]["reference"]
        click(native, [width - 141, height - 34])
        try:
            require(native.clipboard() == token, "Native Copy button did not copy exact reference")
            print("PASS native Copy button clipboard token")
        except (PermissionError, FileNotFoundError) as error:
            print(f"SKIP clipboard readback: {error}; Copy success remains unverified")
        call('review', 'selection', "scsel1.not-hex", expected_code=2)
        before = selection()
        call('review', 'highlight', "cylinder")
        require(selection() == before and snapshot()["highlights"] == ["cylinder"], "Agent highlights changed human selection")
        # A held button crossing the 5px threshold must orbit without selecting.
        camera = snapshot()["camera"]
        for kind, point in [("move", [640, 400]), ("down", [640, 400]),
                            ("move", [680, 420]), ("move", [700, 430]), ("up", [700, 430])]:
            native.event(kind, *point)
        require(snapshot()["camera"] != camera and selection() == before, "Drag must orbit and preserve selection")
        call('review', 'frame', "cylinder")
        mode(1)
        # Choose a lateral point facing the camera, away from cap boundaries.
        camera = snapshot()["camera"]
        dx, dy = camera["position"][0] / .1 - 40, -camera["position"][2] / .1
        length = math.hypot(dx, dy)
        curved = pick("curved-patch", [40 + 10*dx/length, 10*dy/length, 0], "cylinder")
        call('review', 'screenshot', str(folder / "curved-selection.png"))
        require({p["id"]: p["exportable"] for p in snapshot()["parts"]} == flags, "Picking changed exportability")
        old = curved["reference"]
        source = folder / "parts.js"
        source.write_text(source.read_text(encoding="utf-8").replace("size=20", "size=22"), encoding="utf-8")
        call('review', 'selection', old, expected_code=7)
        settle()
        call('review', 'selection', old, expected_code=7)
        call('review', 'frame', "cube")
        fresh = pick("planar-face", [0, 0, 10])["reference"]
        valid_source = source.read_text(encoding="utf-8")
        source.write_text(valid_source + "\nthis is a syntax error;\n", encoding="utf-8")
        revision = call('project', 'revision')
        call('project', 'wait', "--revision", session._revision(revision), expected_code=5)
        require(snapshot()["displayedRevision"] == selection()["geometry"]["revision"],
                "Broken reload did not retain the selected displayed revision")
        call('review', 'selection', fresh, expected_code=7)
        source.write_text(valid_source, encoding="utf-8")
        settle()
        call('review', 'view', "inspection")
        settle()
        call('review', 'selection', fresh, expected_code=7)
        print(f"PASS native {'Windows' if os.name == 'nt' else 'X11'} selection, reference, reload/view invalidation, camera and export checks")
        print(f"Evidence: {folder}")
    finally:
        if pid:
            try:
                client.call('review', 'screenshot', str(folder / "final-state.png"), '--project', "selection-test")
            except Exception:
                pass
            session._terminate_owned_pid(pid)
        if not args.keep_temp:
            shutil.rmtree(folder)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    parser.add_argument("--viewer")
    parser.add_argument("--keep-temp", action="store_true")
    parser.add_argument("--expected-scale", type=float, help="Assert actual viewer scaling before native input checks")
    run(parser.parse_args())
