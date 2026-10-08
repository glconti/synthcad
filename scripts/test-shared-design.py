#!/usr/bin/env python3
"""Black-box acceptance checks for shared-source design graphs.

The harness copies the public fixture into a unique Unicode temporary path,
uses an isolated session registry, and terminates only viewer PIDs reported
for the session it opened.

Usage:
  python scripts/test-shared-design.py --cli PATH [--viewer PATH]
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "viewer" / "tests" / "agent-fixtures" / "shared-design"
EXIT_CODES = {"busy": 11, "load_failed": 5, "stale_revision": 7}


class AcceptanceFailure(RuntimeError):
    pass


def _key(value: str) -> str:
    return "".join(char.lower() for char in value if char.isalnum())


def _walk(value: Any):
    if isinstance(value, dict):
        for key, child in value.items():
            yield str(key), child
            yield from _walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk(child)


def _find(value: Any, *names: str) -> Any:
    wanted = {_key(name) for name in names}
    for key, child in _walk(value):
        if _key(key) in wanted:
            return child
    return None


def _same_path(first: Any, second: Path) -> bool:
    if not isinstance(first, str) or not first:
        return False
    try:
        return Path(first).resolve(strict=False) == second.resolve(strict=False)
    except (OSError, RuntimeError):
        return os.path.normcase(os.path.abspath(first)) == os.path.normcase(os.path.abspath(str(second)))


def _pid_records(value: Any) -> list[int]:
    found: list[int] = []

    def visit(node: Any) -> None:
        if isinstance(node, dict):
            for key, child in node.items():
                if _key(str(key)) in {"pid", "processid", "viewerpid", "viewerprocessid"}:
                    try:
                        pid = int(child)
                    except (TypeError, ValueError):
                        pid = 0
                    if pid > 0:
                        found.append(pid)
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)

    visit(value)
    return list(dict.fromkeys(found))


def _terminate_owned_pid(pid: int) -> None:
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        open_process = kernel.OpenProcess
        open_process.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        open_process.restype = ctypes.c_void_p
        terminate = kernel.TerminateProcess
        terminate.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        terminate.restype = ctypes.c_int
        wait = kernel.WaitForSingleObject
        wait.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        wait.restype = ctypes.c_uint32
        close = kernel.CloseHandle
        close.argtypes = [ctypes.c_void_p]
        close.restype = ctypes.c_int
        handle = open_process(0x0001 | 0x100000, 0, pid)  # TERMINATE | SYNCHRONIZE
        if not handle:
            if ctypes.get_last_error() == 87:  # already exited
                return
            raise OSError(ctypes.get_last_error(), f"OpenProcess failed for owned PID {pid}")
        try:
            if not terminate(handle, 0) and ctypes.get_last_error() != 87:
                raise OSError(ctypes.get_last_error(), f"TerminateProcess failed for owned PID {pid}")
            if wait(handle, 3000) == 258:  # WAIT_TIMEOUT
                raise OSError(f"owned viewer PID {pid} did not stop after termination")
        finally:
            close(handle)
        return

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except (ProcessLookupError, PermissionError, OSError):
            return
        time.sleep(0.05)
    raise OSError(f"owned viewer PID {pid} did not stop after SIGTERM")


def _discover_executable(explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).expanduser()
        if candidate.is_dir():
            for name in ("synthcad.exe", "dingcad_viewer.exe", "synthcad", "dingcad_viewer"):
                match = candidate / name
                if match.is_file():
                    return match.resolve()
        elif candidate.is_file():
            return candidate.resolve()
        located = shutil.which(explicit)
        if located:
            return Path(located).resolve()
        raise AcceptanceFailure(f"Executable does not exist: {explicit}")

    for variable in ("SYNTHCAD_CLI", "SYNTHCAD_EXECUTABLE"):
        value = os.environ.get(variable)
        if value:
            return _discover_executable(value)
    for name in ("synthcad", "dingcad_viewer"):
        located = shutil.which(name)
        if located:
            return Path(located).resolve()
    build_root = ROOT / "out" / "build"
    names = {"synthcad", "synthcad.exe", "dingcad_viewer", "dingcad_viewer.exe"}
    candidates = [item for item in build_root.rglob("*") if item.is_file() and item.name in names]
    candidates.sort(key=lambda item: item.stat().st_mtime, reverse=True)
    if candidates:
        return candidates[0].resolve()
    raise AcceptanceFailure("Could not locate SynthCAD; pass --cli PATH.")


class Client:
    def __init__(self, cli: Path, viewer: Path | None, temp_root: Path):
        self.cli = cli
        self.viewer = viewer
        self.session_dir = temp_root / "isolated-sessions"
        self.session_dir.mkdir()

    def call(self, *arguments: str, expected_code: int | tuple[int, ...] = 0,
             timeout: float = 20) -> dict[str, Any]:
        args = [str(self.cli), *arguments]
        if "--json" not in args:
            args.append("--json")
        env = os.environ.copy()
        env["SYNTHCAD_SESSION_DIR"] = str(self.session_dir)
        if self.viewer is not None:
            env["SYNTHCAD_VIEWER"] = str(self.viewer)
        try:
            completed = subprocess.run(args, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            raise AcceptanceFailure(f"CLI timed out for {args!r}: {error}") from error
        stdout = completed.stdout.decode("utf-8", errors="replace")
        stderr = completed.stderr.decode("utf-8", errors="replace")
        try:
            response = json.loads(stdout)
        except json.JSONDecodeError as error:
            raise AcceptanceFailure(
                f"CLI did not return one JSON response for {args!r}; exit={completed.returncode}\n"
                f"stdout={stdout!r}\nstderr={stderr!r}"
            ) from error
        if not isinstance(response, dict) or not isinstance(response.get("ok"), bool):
            raise AcceptanceFailure(f"Malformed CLI response: {response!r}")
        if response.get("protocolVersion") != 1:
            raise AcceptanceFailure(f"Unexpected protocol version: {response}")
        expected_codes = {expected_code} if isinstance(expected_code, int) else set(expected_code)
        if completed.returncode not in expected_codes:
            raise AcceptanceFailure(
                f"Unexpected exit code for {args!r}: expected one of {sorted(expected_codes)}, got {completed.returncode}\n"
                f"response={json.dumps(response, ensure_ascii=False)}\nstderr={stderr}"
            )
        if response["ok"] != (completed.returncode == 0):
            raise AcceptanceFailure(f"Response ok flag disagrees with exit status: {response}")
        if completed.returncode:
            code = _find(response, "code")
            expected_name = next((name for name, number in EXIT_CODES.items()
                                  if number == completed.returncode), None)
            if code != expected_name:
                raise AcceptanceFailure(f"Wrong error category for exit {completed.returncode}: {response}")
        return response


class SharedDesignAcceptance:
    def __init__(self, client: Client, timeout_ms: int, keep_temp: bool):
        self.client = client
        self.timeout_ms = timeout_ms
        self.keep_temp = keep_temp
        self.temp = None if keep_temp else tempfile.TemporaryDirectory(prefix="synthcad-shared-design-")
        self.temp_root = Path(tempfile.mkdtemp(prefix="synthcad-shared-design-")
                              if self.temp is None else self.temp.name).resolve()
        self.project = self.temp_root / "Prøva Space Δ" / "Shared Design Review"
        self.project.parent.mkdir(parents=True)
        shutil.copytree(FIXTURE, self.project)
        self.manifest = self.project / "synthcad.json"
        self.entry = self.project / "design.js"
        self.parts_source = self.project / "parts.js"
        self.session = f"shared-design-{os.getpid()}-{os.urandom(4).hex()}"
        self.owned_pids: list[int] = []
        self.valid_design = self.entry.read_text(encoding="utf-8")
        self.valid_parts = self.parts_source.read_text(encoding="utf-8")

    @staticmethod
    def _check(condition: bool, message: str) -> None:
        if not condition:
            raise AcceptanceFailure(message)

    @staticmethod
    def _payload(response: dict[str, Any]) -> dict[str, Any]:
        data = response.get("data")
        return data if isinstance(data, dict) else response

    @classmethod
    def _parts(cls, response: dict[str, Any]) -> list[dict[str, Any]]:
        parts = cls._payload(response).get("parts")
        if not isinstance(parts, list):
            raise AcceptanceFailure(f"Snapshot omitted a parts array: {response}")
        return [part for part in parts if isinstance(part, dict)]

    @classmethod
    def _part_map(cls, response: dict[str, Any]) -> dict[str, dict[str, Any]]:
        result: dict[str, dict[str, Any]] = {}
        for part in cls._parts(response):
            identifier = part.get("instanceId", part.get("id"))
            if isinstance(identifier, str):
                result[identifier] = part
        return result

    @classmethod
    def _displayed_revision(cls, response: dict[str, Any]) -> str:
        data = response.get("data")
        if isinstance(data, dict):
            value = data.get("displayedRevision")
            if isinstance(value, str) and value:
                return value
        value = response.get("revision")
        if isinstance(value, str) and value:
            return value
        value = _find(response, "displayedRevision")
        return value if isinstance(value, str) else ""

    def _write(self, path: Path, text: str) -> None:
        with path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)

    def _open(self) -> dict[str, Any]:
        response = self.client.call("open", str(self.project), "--session", self.session, "--hidden")
        self.owned_pids.extend(_pid_records(response))
        return response

    def _revision(self) -> str:
        deadline = time.monotonic() + self.timeout_ms / 1000
        while True:
            response = self.client.call(
                "revision", "--session", self.session,
                expected_code=(0, EXIT_CODES["busy"]),
            )
            if not response["ok"]:
                if _find(response, "code") != "busy" or time.monotonic() >= deadline:
                    raise AcceptanceFailure(f"revision request did not become ready: {response}")
                time.sleep(0.05)
                continue
            data = response.get("data")
            token = data.get("revision") if isinstance(data, dict) else None
            if isinstance(token, str) and token:
                return token
            raise AcceptanceFailure(f"revision response omitted a token: {response}")

    def _ready(self) -> dict[str, Any]:
        token = self._revision()
        return self.client.call("wait", "--session", self.session, "--revision", token,
                                "--timeout", str(self.timeout_ms), timeout=self.timeout_ms / 1000 + 5)

    def _snapshot(self) -> dict[str, Any]:
        return self.client.call("snapshot", "--session", self.session)

    def _state(self) -> dict[str, Any]:
        return self.client.call("state", "--session", self.session)

    def _select_view(self, view: str) -> dict[str, Any]:
        self.client.call("view", view, "--session", self.session)
        ready = self._ready()
        snapshot = self._snapshot()
        self._check(_find(snapshot, "activeView") == view or _find(snapshot, "view") == view,
                    f"snapshot did not report active view {view!r}: {snapshot}")
        return snapshot

    @staticmethod
    def _assert_number(actual: Any, expected: float, label: str) -> None:
        if not isinstance(actual, (int, float)) or abs(float(actual) - expected) > 1e-6:
            raise AcceptanceFailure(f"{label}: expected {expected}, got {actual!r}")

    @classmethod
    def _assert_vector(cls, actual: Any, expected: list[float], label: str) -> None:
        if not isinstance(actual, list) or len(actual) != len(expected):
            raise AcceptanceFailure(f"{label}: expected {expected}, got {actual!r}")
        for index, value in enumerate(expected):
            cls._assert_number(actual[index], value, f"{label}[{index}]")

    @classmethod
    def _assert_transform(cls, part: dict[str, Any], rotate: list[float], translate: list[float]) -> None:
        transform = part.get("transform")
        if not isinstance(transform, dict):
            raise AcceptanceFailure(f"Part omitted its resolved transform: {part}")
        cls._assert_vector(transform.get("rotate"), rotate, "transform.rotate")
        cls._assert_vector(transform.get("translate"), translate, "transform.translate")

    @classmethod
    def _assert_bounds(cls, part: dict[str, Any], minimum: list[float], maximum: list[float]) -> None:
        bounds = part.get("bounds")
        if not isinstance(bounds, dict):
            raise AcceptanceFailure(f"Part omitted bounds: {part}")
        cls._assert_vector(bounds.get("min"), minimum, "bounds.min")
        cls._assert_vector(bounds.get("max"), maximum, "bounds.max")

    def _assert_design_metadata(self, snapshot: dict[str, Any], view: str) -> str:
        payload = self._payload(snapshot)
        design = payload.get("design")
        if not isinstance(design, dict):
            raise AcceptanceFailure(f"Snapshot omitted design metadata: {snapshot}")
        self._check(design.get("activeView") == view,
                    f"design metadata has wrong activeView: {design}")
        identity = design.get("identity")
        self._check(isinstance(identity, str) and bool(identity),
                    f"design metadata omitted its identity: {design}")
        source_revision = payload.get("sourceRevision")
        self._check(isinstance(source_revision, str) and bool(source_revision),
                    f"snapshot omitted sourceRevision at its root: {payload}")
        return source_revision

    def _assert_assembly(self, snapshot: dict[str, Any], width: float) -> None:
        parts = self._part_map(snapshot)
        self._check(set(parts) == {"panel-left", "panel-right", "wall-reference"},
                    f"assembly should resolve three unique instance rows: {parts}")
        self._check(len(self._parts(snapshot)) == 3,
                    f"group aliases duplicated a physical part row: {self._parts(snapshot)}")
        instance_ids = [part.get("instanceId", part.get("id")) for part in self._parts(snapshot)]
        self._check(len(set(instance_ids)) == 3,
                    f"physical copies did not keep distinct instance IDs: {instance_ids}")
        left, right, wall = parts["panel-left"], parts["panel-right"], parts["wall-reference"]
        for part, source, expected_export in ((left, "panel-blank", True),
                                               (right, "panel-blank", True),
                                               (wall, "room-wall", False)):
            self._check(part.get("sourcePartId") == source,
                        f"wrong sourcePartId for {part.get('id')}: {part}")
            self._check(part.get("instanceId") == part.get("id"),
                        f"instanceId is not the physical instance ID: {part}")
            self._check(part.get("exportable") is expected_export,
                        f"wrong exportability for {part.get('id')}: {part}")
        self._assert_transform(left, [0, 0, 0], [-24, 0, 3])
        self._assert_transform(right, [0, 0, 90], [24, 0, 3])
        self._assert_transform(wall, [0, 0, 0], [0, -20, 12])
        self._assert_bounds(left, [-width / 2 - 24, -6, 0], [width / 2 - 24, 6, 6])
        self._assert_bounds(right, [18, -width / 2, 0], [30, width / 2, 6])

        groups = self._payload(snapshot).get("groups")
        if not isinstance(groups, list):
            raise AcceptanceFailure(f"Snapshot omitted group rows: {snapshot}")
        named: dict[str, dict[str, Any]] = {}
        for group in groups:
            if isinstance(group, dict) and isinstance(group.get("name"), str):
                named[group["name"]] = group
        self._check({"Mounting interface", "Printable panels"}.issubset(named),
                    f"snapshot omitted alias groups: {groups}")
        for label in ("Mounting interface", "Printable panels"):
            membership = _find(named[label], "partIds")
            self._check(isinstance(membership, list) and "panel-left" in membership,
                        f"physical instance panel-left is missing from alias group {label!r}: {named[label]}")

    def _assert_reloaded_source(self, width: float, assembly_transform: dict[str, Any],
                                previous_displayed: str) -> str:
        snapshot = self._select_view("assembly")
        self._assert_assembly(snapshot, width)
        self._check(self._displayed_revision(snapshot) != previous_displayed,
                    "imported geometry edit did not change displayedRevision")
        parts = self._part_map(snapshot)
        self._check(parts["panel-left"]["transform"] == assembly_transform["panel-left"],
                    "source geometry edit changed panel-left's assembly transform")
        self._check(parts["panel-right"]["transform"] == assembly_transform["panel-right"],
                    "source geometry edit changed panel-right's assembly transform")
        return self._assert_design_metadata(snapshot, "assembly")

    def _assert_failure_retained(self, displayed_revision: str, width: float) -> None:
        state = self._state()
        self._check((self._payload(state).get("status") or "").casefold() in {"failed", "error", "load_failed"},
                    f"failed design edit was not reported as failed: {state}")
        self._check(self._displayed_revision(state) == displayed_revision,
                    f"failed design edit replaced the displayed revision: {state}")
        self._check(_find(state, "exportValid") is False,
                    f"export stayed enabled after invalid design metadata: {state}")
        retained = self._snapshot()
        self._check(self._displayed_revision(retained) == displayed_revision,
                    f"snapshot lost its last valid displayed revision: {retained}")
        self._assert_assembly(retained, width)

    def _expect_failed_revision(self, displayed_revision: str, width: float) -> None:
        token = self._revision()
        failed = self.client.call("wait", "--session", self.session, "--revision", token,
                                  "--timeout", str(self.timeout_ms), expected_code=EXIT_CODES["load_failed"],
                                  timeout=self.timeout_ms / 1000 + 5)
        self._check(_find(failed, "code") == "load_failed",
                    f"invalid design did not return load_failed: {failed}")
        self._assert_failure_retained(displayed_revision, width)

    def run(self) -> None:
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        view_entries = manifest["views"]
        self._check(set(view_entries) == {"assembly", "inspection", "plate-1", "plate-2"},
                    f"fixture does not expose the four expected views: {view_entries}")
        self._check(set(view_entries.values()) == {"design.js"},
                    f"all named views must use one shared entry: {view_entries}")

        self._open()
        self._ready()
        assembly = self._snapshot()
        self._assert_assembly(assembly, 20)
        source_revision = self._assert_design_metadata(assembly, "assembly")
        assembly_identity = self._payload(assembly)["design"]["identity"]
        assembly_displayed = self._displayed_revision(assembly)
        self._check(bool(assembly_displayed), f"assembly snapshot omitted displayedRevision: {assembly}")

        # Stale guards must reject work against an unrelated displayed revision.
        stale = "0" * 64 if assembly_displayed != "0" * 64 else "f" * 64
        self.client.call("snapshot", "--session", self.session, "--expect-revision", stale,
                         expected_code=EXIT_CODES["stale_revision"])

        # Every manifest view uses design.js, but its resolved layout and
        # displayed revision must describe the selected graph view.
        inspection = self._select_view("inspection")
        inspection_displayed = self._displayed_revision(inspection)
        self._check(inspection_displayed != assembly_displayed,
                    "switching views that share one entry did not change displayedRevision")
        self._check(self._assert_design_metadata(inspection, "inspection") == source_revision,
                    "switching views changed the underlying sourceRevision")
        inspection_identity = self._payload(inspection)["design"]["identity"]
        self._check(inspection_identity != assembly_identity,
                    "design identity did not account for the active view layout")
        inspection_parts = self._part_map(inspection)
        self._check(set(inspection_parts) == {"panel-left", "panel-right", "wall-reference"},
                    f"inspection view resolved the wrong instances: {inspection_parts}")
        self._assert_transform(inspection_parts["panel-left"], [0, 0, 0], [-18, 18, 3])
        self._assert_transform(inspection_parts["panel-right"], [0, 0, 0], [18, -18, 3])

        plate_one = self._select_view("plate-1")
        one_parts = self._part_map(plate_one)
        self._check(set(one_parts) == {"panel-left"}, f"plate-1 contains unintended instances: {one_parts}")
        self._assert_transform(one_parts["panel-left"], [0, 0, 0], [0, 0, 3])
        self._assert_bounds(one_parts["panel-left"], [-10, -6, 0], [10, 6, 6])

        plate_two = self._select_view("plate-2")
        two_parts = self._part_map(plate_two)
        self._check(set(two_parts) == {"panel-right"}, f"plate-2 contains unintended instances: {two_parts}")
        self._assert_transform(two_parts["panel-right"], [0, 0, 0], [48, 0, 3])
        self._assert_bounds(two_parts["panel-right"], [38, -6, 0], [58, 6, 6])

        # Edit a tracked imported module; both physical copies should change
        # while their IDs, memberships and authored placements remain stable.
        before_parts = self.parts_source.read_text(encoding="utf-8")
        self._check("const sourceWidth = 20;" in before_parts,
                    "fixture sourceWidth marker changed unexpectedly")
        after_parts = before_parts.replace("const sourceWidth = 20;", "const sourceWidth = 28;", 1)
        self._write(self.parts_source, after_parts)
        new_source_revision = self._assert_reloaded_source(28, {
            "panel-left": {"rotate": [0, 0, 0], "translate": [-24, 0, 3]},
            "panel-right": {"rotate": [0, 0, 90], "translate": [24, 0, 3]},
        }, assembly_displayed)
        self._check(new_source_revision != source_revision,
                    "editing imported geometry did not change the sourceRevision")
        last_good_snapshot = self._snapshot()
        last_good_revision = self._displayed_revision(last_good_snapshot)

        inspection = self._select_view("inspection")
        inspection_parts = self._part_map(inspection)
        self._check(set(inspection_parts) == {"panel-left", "panel-right", "wall-reference"},
                    f"import edit dropped instances from inspection: {inspection_parts}")
        self._assert_bounds(inspection_parts["panel-left"], [-32, 12, 0], [-4, 24, 6])
        self._assert_bounds(inspection_parts["panel-right"], [4, -24, 0], [32, -12, 6])
        self._assert_transform(inspection_parts["panel-left"], [0, 0, 0], [-18, 18, 3])
        self._assert_transform(inspection_parts["panel-right"], [0, 0, 0], [18, -18, 3])
        self._check(self._assert_design_metadata(inspection, "inspection") == new_source_revision,
                    "source edit did not propagate to the inspection view")

        plate_one = self._select_view("plate-1")
        one_parts = self._part_map(plate_one)
        self._assert_bounds(one_parts["panel-left"], [-14, -6, 0], [14, 6, 6])
        self._assert_transform(one_parts["panel-left"], [0, 0, 0], [0, 0, 3])
        self._check(self._assert_design_metadata(plate_one, "plate-1") == new_source_revision,
                    "source edit did not propagate to plate-1")

        plate_two = self._select_view("plate-2")
        two_parts = self._part_map(plate_two)
        self._assert_bounds(two_parts["panel-right"], [34, -6, 0], [62, 6, 6])
        self._assert_transform(two_parts["panel-right"], [0, 0, 0], [48, 0, 3])
        self._check(self._assert_design_metadata(plate_two, "plate-2") == new_source_revision,
                    "source edit did not propagate to plate-2")

        # Restore assembly before injecting invalid metadata. The invalid
        # graph is intentionally unreachable from every selected view.
        self._select_view("assembly")
        last_good_snapshot = self._snapshot()
        last_good_revision = self._displayed_revision(last_good_snapshot)
        cycle_marker = "  ],\n  views: ["
        self._check(self.valid_design.count(cycle_marker) == 1,
                    "fixture group/view separator changed unexpectedly")
        unused_cycle = (
            "    {id:'unused-cycle-a', name:'Unused cycle A', members:[{group:'unused-cycle-b'}]},\n"
            "    {id:'unused-cycle-b', name:'Unused cycle B', members:[{group:'unused-cycle-a'}]},\n"
            "  ],\n  views: ["
        )
        self._write(self.entry, self.valid_design.replace(cycle_marker, unused_cycle, 1))
        self._expect_failed_revision(last_good_revision, 28)
        self._write(self.entry, self.valid_design)
        self._ready()
        recovered = self._snapshot()
        self._assert_assembly(recovered, 28)
        self._check(_find(self._state(), "exportValid") is True,
                    f"export did not recover after removing unused cycle: {self._state()}")

        dangling_marker = "  ],\n};"
        self._check(self.valid_design.count(dangling_marker) == 1,
                    "fixture view terminator changed unexpectedly")
        unused_dangling_view = (
            "    {id:'unused-dangling-view', name:'Unused dangling view', kind:'inspection', "
            "members:[{instance:'missing-instance'}]},\n  ],\n};"
        )
        last_good_revision = self._displayed_revision(recovered)
        self._write(self.entry, self.valid_design.replace(dangling_marker, unused_dangling_view, 1))
        self._expect_failed_revision(last_good_revision, 28)
        self._write(self.entry, self.valid_design)
        self._ready()
        final_snapshot = self._snapshot()
        self._assert_assembly(final_snapshot, 28)
        self._check(_find(self._state(), "exportValid") is True,
                    f"export did not recover after removing unused dangling view: {self._state()}")

    def cleanup(self) -> list[str]:
        issues: list[str] = []
        try:
            listed = self.client.call("sessions")
            records = _find(listed, "sessions")
            if isinstance(records, list):
                for record in records:
                    if not isinstance(record, dict):
                        continue
                    session = _find(record, "session", "sessionId")
                    path = _find(record, "projectPath")
                    if session == self.session and _same_path(path, self.manifest):
                        self.owned_pids.extend(_pid_records(record))
        except Exception as error:
            issues.append(f"could not verify the owned viewer session during cleanup: {error}")

        for pid in dict.fromkeys(self.owned_pids):
            if pid == os.getpid():
                issues.append(f"refused to terminate the harness process ({pid})")
                continue
            try:
                _terminate_owned_pid(pid)
            except OSError as error:
                issues.append(f"could not stop test-owned viewer PID {pid}: {error}")
        if self.keep_temp:
            print(f"TEST ARTIFACTS: {self.temp_root}", file=sys.stderr)
        else:
            try:
                self.temp.cleanup()
            except OSError as error:
                issues.append(f"temporary fixture cleanup failed: {error}")
        return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", required=True, help="SynthCAD CLI executable")
    parser.add_argument("--viewer", help="Viewer executable used by the CLI launcher")
    parser.add_argument("--timeout", type=int, default=10000, help="wait timeout in milliseconds")
    parser.add_argument("--keep-temp", action="store_true", help="preserve the temporary fixture for debugging")
    args = parser.parse_args(argv)
    if args.timeout < 100:
        parser.error("--timeout must be at least 100 ms")
    if args.timeout > 300000:
        parser.error("--timeout cannot exceed the CLI maximum of 300000 ms")

    harness: SharedDesignAcceptance | None = None
    result = 1
    try:
        cli = _discover_executable(args.cli)
        viewer = _discover_executable(args.viewer) if args.viewer else None
        temp = tempfile.TemporaryDirectory(prefix="synthcad-shared-design-client-")
        try:
            client = Client(cli, viewer, Path(temp.name).resolve())
            harness = SharedDesignAcceptance(client, args.timeout, args.keep_temp)
            harness.run()
            print("PASS: SC12 shared-design graph acceptance")
            result = 0
        finally:
            if harness is not None:
                cleanup_issues = harness.cleanup()
                for issue in cleanup_issues:
                    print(f"CLEANUP: {issue}", file=sys.stderr)
                if cleanup_issues and result == 0:
                    result = 1
            temp.cleanup()
    except (AcceptanceFailure, OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        result = 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
