#!/usr/bin/env python3
"""Native guided-pick acceptance checks using only public CLI and native input.

Creates its own hidden viewer and isolated session registry. Windows messages
and X11 XSendEvent address only that viewer's window. No viewer test hooks or
input-control CLI are used. --keep-temp retains screenshots and JSON CLI trace.
Linux requires DISPLAY and xdotool for owned-window discovery (Xvfb is suitable).
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("selection_tests", ROOT / "scripts/test-geometric-selection.py")
selection_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection_tests)
session = selection_tests.session
require = selection_tests.require
click = selection_tests.click


class OwnedProcess:
    """Bind cleanup to the process identity returned by this test's open call."""
    def __init__(self, pid, identity):
        self.pid, self.identity = pid, str(identity)
        self.handle = None
        require(self.identity, "Owned viewer record lacks processIdentity")
        if os.name == "nt":
            from ctypes import wintypes as W
            self.kernel = C.WinDLL("kernel32", use_last_error=True)
            self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
            self.kernel.OpenProcess.restype = W.HANDLE
            self.kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
            self.kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
            self.kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
            self.kernel.CloseHandle.argtypes = [W.HANDLE]
            self.handle = self.kernel.OpenProcess(0x1000 | 0x100000 | 0x0001, False, pid)
            require(self.handle, f"Cannot bind owned viewer process {pid}")
            created, exited, kernel, user = (W.FILETIME() for _ in range(4))
            require(self.kernel.GetProcessTimes(self.handle, C.byref(created), C.byref(exited),
                                                C.byref(kernel), C.byref(user)), "GetProcessTimes failed")
            actual = str((created.dwHighDateTime << 32) | created.dwLowDateTime)
        else:
            actual = self.linux_identity()
        require(actual == self.identity, "Viewer process identity changed before native testing")

    def linux_identity(self):
        try:
            text = Path(f"/proc/{self.pid}/stat").read_text()
            fields = text[text.rfind(")") + 2:].split()
            return None if fields[0] == "Z" else fields[19]
        except (FileNotFoundError, ProcessLookupError):
            return None

    def alive(self):
        if self.handle:
            return self.kernel.WaitForSingleObject(self.handle, 0) == 258
        return self.linux_identity() == self.identity

    def wait_closed(self, timeout=8):
        deadline = time.monotonic() + timeout
        while self.alive() and time.monotonic() < deadline:
            time.sleep(.05)
        require(not self.alive(), "Owned viewer did not exit after native close")

    def cleanup(self):
        try:
            if self.alive():
                if self.handle:
                    require(self.kernel.TerminateProcess(self.handle, 0), "Owned viewer termination failed")
                else:
                    # Recheck the start identity immediately before signalling.
                    if self.linux_identity() == self.identity:
                        os.kill(self.pid, signal.SIGTERM)
                self.wait_closed()
        finally:
            if self.handle:
                self.kernel.CloseHandle(self.handle)
                self.handle = None


def guided_layout(width, height):
    """Exact GuidedPickUi::Layout formulas, in logical pixels, from its source."""
    card_width, card_height = min(420., max(1., width / 2 - 24)), min(220., max(1., height * .5))
    x, y = max(0., width - 12 - card_width), max(0., height - 12 - card_height)
    pad_x, pad_y = min(12., card_width * .1), min(12., card_height * .1)
    content_width, inner_height = max(0., card_width - 2 * pad_x), max(0., card_height - 2 * pad_y)
    heading = min(22., inner_height)
    action_height = min(32., max(0., inner_height - heading))
    remaining = max(0., inner_height - heading - action_height)
    heading_gap = min(8., remaining * .35)
    action_y = y + card_height - pad_y - action_height
    question_y = y + pad_y + heading + heading_gap
    cancel_width = min(82., max(0., content_width * .38))
    gap = min(8., content_width * .04)
    confirm_width = max(0., content_width - cancel_width - gap)
    return {"cancel": [x + pad_x + cancel_width / 2, action_y + action_height / 2],
            "confirm": [x + pad_x + cancel_width + gap + confirm_width / 2, action_y + action_height / 2],
            "question": [x + pad_x + content_width / 2, (question_y + action_y - 8) / 2]}


def wheel(native, point, steps):
    native.event("move", *point)
    if os.name == "nt":
        from ctypes import wintypes as W
        position = W.POINT(round(point[0] * native.scale), round(point[1] * native.scale))
        native.user.ClientToScreen.argtypes = [W.HWND, C.POINTER(W.POINT)]
        require(native.user.ClientToScreen(native.hwnd, C.byref(position)), "ClientToScreen failed")
        location = ((position.y & 0xffff) << 16) | (position.x & 0xffff)
        for _ in range(abs(steps)):
            delta = 120 if steps > 0 else -120
            require(native.user.PostMessageW(native.hwnd, 0x20A, (delta & 0xffff) << 16, location),
                    "Owned-window wheel message failed")
            time.sleep(.04)
    else:
        for _ in range(abs(steps)):
            for pressed in (True, False):
                event = native.Event()
                m = event.mouse
                m.type, m.display, m.window, m.root, m.same_screen = (4 if pressed else 5), native.display, native.window, native.root, 1
                m.x = m.x_root = round(point[0] * native.scale)
                m.y = m.y_root = round(point[1] * native.scale)
                m.button = 4 if steps > 0 else 5
                require(native.x.XSendEvent(native.display, native.window, 0, 4 if pressed else 8, C.byref(event)),
                        "Owned-window X11 wheel event failed")
                native.x.XFlush(native.display)
                time.sleep(.04)


def graceful_close(native, owned):
    require(owned.alive(), "Owned viewer exited before native close")
    if os.name == "nt":
        owner = native.W.DWORD()
        native.user.GetWindowThreadProcessId(native.hwnd, C.byref(owner))
        require(owner.value == owned.pid, "Native close target no longer belongs to owned viewer")
        require(native.user.PostMessageW(native.hwnd, 0x10, 0, 0), "WM_CLOSE failed")
    else:
        # GLFW handles WM_DELETE_WINDOW ClientMessage, even without an X11 WM.
        class Data(C.Union):
            _fields_ = [("bytes", C.c_char * 20), ("shorts", C.c_short * 10), ("longs", C.c_long * 5)]
        class ClientMessage(C.Structure):
            _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                        ("display", C.c_void_p), ("window", C.c_ulong), ("message_type", C.c_ulong),
                        ("format", C.c_int), ("data", Data)]
        class Event(C.Union):
            _fields_ = [("client", ClientMessage), ("pad", C.c_long * 24)]
        native.x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
        native.x.XInternAtom.restype = C.c_ulong
        event = Event()
        message = event.client
        message.type, message.display, message.window, message.format = 33, native.display, native.window, 32
        message.message_type = native.x.XInternAtom(native.display, b"WM_PROTOCOLS", False)
        message.data.longs[0] = native.x.XInternAtom(native.display, b"WM_DELETE_WINDOW", False)
        require(native.x.XSendEvent(native.display, native.window, 0, 0,
                                   C.cast(C.byref(event), C.POINTER(native.Event))), "WM_DELETE_WINDOW failed")
        native.x.XFlush(native.display)


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-guided-pick-"))
    print(f"Test evidence directory: {folder}", flush=True)
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 40)
    client.session_dir, client.trace_path = folder / "sessions", folder / "cli-trace.jsonl"
    owned = None
    try:
        selection_tests.fixture(folder)
        opened = client.call("open", str(folder), "--hidden", "--session", "guided-pick-test")
        records = session._pid_records(opened)
        require(records, "Open response lacks owned viewer PID")
        pid, record = records[0]
        # Public open omits the transport's start identity. Read only this
        # harness's isolated registry, matching its returned PID and session.
        identity = record.get("processIdentity", "")
        if not identity:
            for path in client.session_dir.glob("*.json"):
                registered = json.loads(path.read_text(encoding="utf-8"))
                if registered.get("pid") == pid and registered.get("session") == "guided-pick-test":
                    identity = registered.get("processIdentity", "")
                    break
        owned = OwnedProcess(pid, identity)

        def call(*cmd, **kwargs):
            return client.call(*cmd, "-s", "guided-pick-test", **kwargs)

        def settle(expected_code=0):
            deadline = time.monotonic() + 20
            while True:
                revision = call("revision", expected_code=(0, 11))
                if revision["ok"]:
                    return call("wait", "--revision", session._revision(revision), expected_code=expected_code)
                require(time.monotonic() < deadline, "Initial dependencies did not become ready")
                time.sleep(.1)

        def snapshot():
            return call("snapshot")["data"]

        def human_selection():
            return call("selection")["data"]["selection"]

        def start(identifier, question="Which surface should receive the mount?", **kwargs):
            return call("pick", "--id", identifier, "--kind", "surface", "--question", question, **kwargs)

        def status(identifier):
            return call("pick-status", identifier)["data"]["request"]

        def events(cursor, wait=0, request_timeout=None, **kwargs):
            options = [] if request_timeout is None else ["--timeout", str(request_timeout)]
            return call("events", "--after", cursor, "--wait", str(wait), *options, **kwargs)

        def event_types(response):
            return [event["type"] for event in response["data"]["events"]]

        settle()
        native = selection_tests.WindowsInput(pid) if os.name == "nt" else selection_tests.X11Input(pid)
        if args.expected_scale is not None:
            require(abs(native.scale - args.expected_scale) < .02, "Requested DPI scale was not applied")
        width, height = native.width / native.scale, native.height / native.scale
        layout = guided_layout(width, height)
        print(f"Owned PID {pid}: {native.width}x{native.height}, UI scale {native.scale}", flush=True)
        call("frame", "cube")
        call("highlight", "cylinder")
        # Select a matching surface before the request, so disabled confirmation
        # proves a fresh human choice is required, even when the old kind matches.
        # Same SelectionUi::Layout mode center used by the geometric harness.
        click(native, [width - 372 + 10 + 86.25 + 40.625, height - 128 + 21])
        click(native, selection_tests.project([0, 0, 10], snapshot()["camera"], width, height))
        previous = human_selection()
        require(previous and previous["geometry"]["partId"] == "cube" and previous["geometry"]["kind"] == "planar-face",
                "Prior native surface selection missing")
        baseline = snapshot()
        flags = {part["id"]: (part["exportable"], part["visible"]) for part in baseline["parts"]}
        cursor = baseline["eventCursor"]
        started = start("confirm-1")["data"]
        require(started["created"] and started["request"]["status"] == "pending", "Start did not return pending receipt")
        require(human_selection() == previous, "Starting a request changed prior human selection")
        call("screenshot", str(folder / "question-pending.png"))
        click(native, layout["confirm"])
        require(status("confirm-1")["status"] == "pending", "Confirm submitted a pre-request selection")
        replay = start("confirm-1")["data"]
        require(not replay["created"] and replay["request"] == started["request"], "Duplicate start did not replay receipt")
        require(replay["eventCursor"] == started["eventCursor"], "Duplicate start emitted another event")
        start("conflicting", expected_code=11)
        start("confirm-1", "Changed question", expected_code=2)
        observed = events(cursor)
        require(event_types(observed) == ["pick-started"], "Unexpected request start events")
        cursor = observed["data"]["cursor"]
        require(events(cursor)["data"] == {"events": [], "cursor": cursor}, "Immediate event read should be empty")
        began = time.monotonic()
        timed = events(cursor, 150, request_timeout=0, expected_code=6)
        require(time.monotonic() - began >= .12, "Event wait did not wait")
        require(timed["error"]["details"]["cursor"] == cursor and status("confirm-1")["status"] == "pending",
                "Event timeout advanced cursor or cancelled the request")
        click(native, selection_tests.project([0, 0, 10], snapshot()["camera"], width, height))
        candidate = human_selection()["geometry"]
        require(candidate["kind"] == "planar-face" and candidate["partId"] == "cube", "Native surface candidate missing")
        replay = start("confirm-1")["data"]
        require(not replay["created"] and replay["eventCursor"] == cursor and human_selection()["geometry"] == candidate,
                "Duplicate start reset the native candidate or emitted an event")
        call("screenshot", str(folder / "question-candidate.png"))
        with ThreadPoolExecutor(max_workers=1) as executor:
            # Maximum event wait must be accepted even with a zero global
            # timeout; native confirmation wakes it without waiting five minutes.
            waiting = executor.submit(events, cursor, 300000, request_timeout=0)
            time.sleep(.25)
            require(not waiting.done(), "Event wait completed before human confirmation")
            click(native, layout["confirm"])
            confirmed = waiting.result(timeout=8)
        require(event_types(confirmed) == ["pick-confirmed"], "Native Confirm did not emit exactly one confirmation")
        result = status("confirm-1")
        require(result["status"] == "confirmed" and result["selection"] == candidate, "Confirmed geometry differs from human choice")
        event = confirmed["data"]["events"][0]
        require(event["requestId"] == "confirm-1" and event["selection"] == candidate and event["revision"] == candidate["revision"],
                "Confirmation event lacks bound geometry context")
        resolved = call("reference", candidate["reference"])["data"]["geometry"]
        require(resolved["partId"] == "cube" and resolved["kind"] == "planar-face" and
                resolved["sourcePartId"] == "box-source" and resolved["instanceId"] == "cube", "Confirmed reference did not resolve")
        require(human_selection()["geometry"] == candidate and snapshot()["highlights"] == ["cylinder"],
                "Reading the result changed selection or agent highlights")
        require({part["id"]: (part["exportable"], part["visible"]) for part in snapshot()["parts"]} == flags,
                "Guided selection changed exportability or visibility")
        call("screenshot", str(folder / "confirmed-selection.png"))
        cursor = confirmed["data"]["cursor"]
        require(not start("confirm-1")["data"]["created"], "Terminal duplicate created another request")

        started_ui_cancel = start("ui-cancel")["data"]
        click(native, layout["cancel"])
        require(status("ui-cancel")["status"] == "cancelled", "Native Cancel did not cancel request")
        require(event_types(events(started_ui_cancel["eventCursor"])) == ["pick-cancelled"], "Native Cancel event missing")
        cli_cancel = start("cli-cancel")["data"]
        cancelled = call("pick-cancel", "cli-cancel")["data"]
        require(cancelled["request"]["status"] == "cancelled", "CLI cancel failed")
        require(call("pick-cancel", "cli-cancel")["data"] == cancelled, "CLI cancel is not idempotent")
        require(event_types(events(cli_cancel["eventCursor"])) == ["pick-cancelled"], "CLI cancel emitted wrong events")

        long_question = "SCROLL START — Choose a surface.\n" + "\n".join(f"Line {index:02}: Preserve the selected geometry and read this context." for index in range(35)) + "\nSCROLL END"
        start("long-question", long_question)
        camera = snapshot()["camera"]
        call("screenshot", str(folder / "long-question-top.png"))
        wheel(native, layout["question"], -45)
        call("screenshot", str(folder / "long-question-bottom.png"))
        require((folder / "long-question-top.png").read_bytes() != (folder / "long-question-bottom.png").read_bytes(),
                "Question wheel scrolling produced no visual change")
        require(snapshot()["camera"] == camera and status("long-question")["status"] == "pending", "Question scrolling moved camera or submitted request")
        click(native, layout["cancel"])

        source = folder / "parts.js"
        start("reload-invalidates")
        source.write_text(source.read_text(encoding="utf-8").replace("size=20", "size=22"), encoding="utf-8")
        settle()
        require(status("reload-invalidates")["status"] == "invalidated", "Source reload did not invalidate pending request")
        call("pick", "--id", "confirm-1", "--kind", "surface", "--question", started["request"]["question"],
             "--expect-revision", result["revision"], expected_code=7)
        replay = start("confirm-1")["data"]
        require(not replay["created"] and replay["request"] == result, "Unguarded terminal receipt changed after reload")
        start("failed-import")
        design = folder / "design.js"
        valid_design = design.read_text(encoding="utf-8")
        design.write_text("import './missing-guided-pick-module.js';\n" + valid_design, encoding="utf-8")
        settle(expected_code=5)
        require(status("failed-import")["status"] == "invalidated", "Failed import did not invalidate pending request")
        design.write_text(valid_design, encoding="utf-8")
        settle()

        current = snapshot()["eventCursor"]
        prefix = current.rsplit(":", 1)[0]
        for bad in ("malformed-cursor", prefix + ":bad", prefix + ":18446744073709551615", "another-session:0"):
            response = events(bad, expected_code=13)
            require(response["error"]["code"] == "stale_cursor", "Malformed/future/wrong-session cursor error changed")
        # Exercise bounded retention through public commands; requests themselves
        # remain queryable even after their start/cancel events have expired.
        for index in range(129):
            identifier = f"retention-{index}"
            start(identifier)
            call("pick-cancel", identifier)
        response = events(current, expected_code=13)
        require(response["error"]["code"] == "stale_cursor", "Expired cursor did not fail explicitly")
        retained = events("0")["data"]
        require(len(retained["events"]) == 256, "Session event history is not bounded to 256 events")
        require(retained.get("truncated") is True, "Retained history omitted its truncation marker")
        require(status("confirm-1") == result, "Event eviction lost terminal request record")
        print("PASS guided selection, retry identity, confirmation, cancellation, reload, long question and bounded events", flush=True)

        closing = start("viewer-close")["data"]
        with ThreadPoolExecutor(max_workers=1) as executor:
            waiting = executor.submit(events, closing["eventCursor"], 10000, expected_code=9)
            time.sleep(.25)
            require(not waiting.done(), "Close test event wait was not live")
            graceful_close(native, owned)
            response = waiting.result(timeout=12)
        details = response["error"]["details"]
        require([event["type"] for event in details["events"]] == ["pick-cancelled", "viewer-closed"],
                "Native close did not deliver final events to live wait")
        require(details["events"][0]["requestId"] == "viewer-close" and details["events"][0]["reason"] == "viewer_closed",
                "Native close omitted pending request cancellation context")
        require(isinstance(details["cursor"], str), "Native close omitted final event cursor")
        owned.wait_closed()
        print(f"PASS native {'Windows' if os.name == 'nt' else 'X11'} graceful close wakes live event wait", flush=True)
        print(f"Evidence: {folder}", flush=True)
    finally:
        if owned:
            owned.cleanup()
        if not args.keep_temp:
            shutil.rmtree(folder)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    parser.add_argument("--viewer")
    parser.add_argument("--keep-temp", action="store_true")
    parser.add_argument("--expected-scale", type=float, help="Assert actual viewer scale before native input")
    run(parser.parse_args())
