#!/usr/bin/env python3
"""Black-box acceptance checks for the local SynthCAD agent-session CLI.

The harness copies public fixtures into a unique temporary directory, then
performs all source edits and screenshot writes there. It uses only Python's
standard library and terminates only viewer PIDs returned for test-owned
sessions.

Usage:
  python scripts/test-agent-session.py [CLI_PATH] [VIEWER_PATH]
  python scripts/test-agent-session.py --cli PATH [--viewer PATH]
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
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
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "viewer" / "tests" / "agent-fixtures"
EXIT_CODES = {
    "invalid_argument": 2,
    "no_session": 3,
    "ambiguous_session": 4,
    "load_failed": 5,
    "timeout": 6,
    "stale_revision": 7,
    "superseded": 8,
    "cancelled": 9,
    "io_error": 10,
    "busy": 11,
    "not_found": 12,
}


class AcceptanceFailure(RuntimeError):
    pass


def _key(value: str) -> str:
    return "".join(char.lower() for char in value if char.isalnum())


def _walk(value: Any) -> Iterable[tuple[str, Any]]:
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


def _revision(response: dict[str, Any]) -> str:
    data = response.get("data")
    candidates = []
    if isinstance(data, dict):
        candidates.extend(
            data.get(field)
            for field in ("revision", "desiredRevision", "requestedRevision", "revisionToken")
        )
    candidates.append(response.get("revision"))
    for value in candidates:
        if isinstance(value, str) and value:
            return value
    raise AcceptanceFailure(f"Response has no revision token: {json.dumps(response, ensure_ascii=False)}")


def _flatten_parts(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    parts: list[dict[str, Any]] = []
    seen: set[int] = set()

    def visit(value: Any) -> None:
        if isinstance(value, dict):
            marker = id(value)
            if marker in seen:
                return
            seen.add(marker)
            if any(k in value for k in ("id", "partId", "part_id")) and any(
                k in value for k in ("name", "bounds", "exportable", "visible")
            ):
                parts.append(value)
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(snapshot.get("data", snapshot))
    unique: dict[str, dict[str, Any]] = {}
    for part in parts:
        identifier = _find(part, "id", "partId", "part_id")
        if isinstance(identifier, str):
            unique[identifier] = part
    return list(unique.values())


def _part_map(snapshot: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for part in _flatten_parts(snapshot):
        identifier = _find(part, "id", "partId", "part_id")
        if isinstance(identifier, str):
            result[identifier] = part
    return result


def _pid_records(value: Any) -> list[tuple[int, dict[str, Any]]]:
    """Return reported process IDs together with the smallest owning record."""
    found: list[tuple[int, dict[str, Any]]] = []

    def visit(node: Any) -> None:
        if isinstance(node, dict):
            for key, child in node.items():
                if _key(str(key)) in {"pid", "processid", "viewerpid", "viewerprocessid"}:
                    try:
                        number = int(child)
                    except (TypeError, ValueError):
                        continue
                    if number > 0:
                        found.append((number, node))
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)

    visit(value)
    return found


def _terminate_owned_pid(pid: int) -> bool:
    """Stop one explicitly reported test viewer and wait for its exit."""
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
            error = ctypes.get_last_error()
            if error == 87:  # ERROR_INVALID_PARAMETER: process has already exited.
                return False
            raise OSError(error, f"OpenProcess failed for test-owned PID {pid}")
        try:
            if not terminate(handle, 0):
                error = ctypes.get_last_error()
                if error == 87:
                    return False
                raise OSError(error, f"TerminateProcess failed for test-owned PID {pid}")
            if wait(handle, 3000) == 258:  # WAIT_TIMEOUT
                raise OSError(f"test-owned viewer PID {pid} did not stop after termination")
            return True
        finally:
            close(handle)

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return False
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        except PermissionError:
            return True
        except OSError:
            return True
        # A container's PID 1 may leave an exited detached viewer unreaped.
        # kill(pid, 0) still succeeds for zombies; they are no longer running.
        if sys.platform.startswith("linux"):
            try:
                status = Path(f"/proc/{pid}/stat").read_text()
                if status.rsplit(")", 1)[1].split()[0] in ("Z", "X"):
                    return True
            except FileNotFoundError:
                return True
        time.sleep(0.05)
    raise OSError(f"test-owned viewer PID {pid} did not stop after SIGTERM")


def discover_executable(explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).expanduser()
        if candidate.is_dir():
            names = ("synthcad-cli.exe", "synthcad.exe", "dingcad_viewer.exe", "synthcad-cli", "synthcad", "dingcad_viewer")
            for name in names:
                match = candidate / name
                if match.is_file():
                    return match.resolve()
        elif candidate.is_file():
            return candidate.resolve()
        located = shutil.which(explicit)
        if located:
            return Path(located).resolve()
        raise AcceptanceFailure(f"CLI executable does not exist: {explicit}")

    for variable in ("SYNTHCAD_CLI", "SYNTHCAD_EXECUTABLE"):
        value = os.environ.get(variable)
        if value:
            return discover_executable(value)
    for name in ("synthcad", "dingcad_viewer"):
        located = shutil.which(name)
        if located:
            return Path(located).resolve()

    build_root = ROOT / "out" / "build"
    names = {"synthcad", "synthcad.exe", "dingcad_viewer", "dingcad_viewer.exe"}
    candidates = [candidate for candidate in build_root.rglob("*")
                  if candidate.is_file() and candidate.name in names]
    candidates.sort(key=lambda item: item.stat().st_mtime, reverse=True)
    if candidates:
        return candidates[0].resolve()
    raise AcceptanceFailure(
        "Could not find SynthCAD. Pass the executable path as CLI_PATH or --cli PATH."
    )


class Client:
    def __init__(self, executable: Path, viewer: Path | None, command_timeout: float):
        self.executable = executable
        self.viewer = viewer
        self.command_timeout = command_timeout
        self.session_dir: Path | None = None
        self.trace_path: Path | None = None

    def call(
        self,
        *arguments: str,
        expected_code: int | Iterable[int] = 0,
        timeout: float | None = None,
    ) -> dict[str, Any]:
        expected_codes = {expected_code} if isinstance(expected_code, int) else set(expected_code)
        args = [str(self.executable), *map(str, arguments)]
        if "--json" not in args:
            args.append("--json")
        env = os.environ.copy()
        if self.viewer is not None:
            # The viewer binary is explicit so a separate CLI launcher can be
            # exercised against a selected build without guessing its sibling.
            env["SYNTHCAD_VIEWER"] = str(self.viewer)
        if self.session_dir is not None:
            env["SYNTHCAD_SESSION_DIR"] = str(self.session_dir)
        try:
            completed = subprocess.run(
                args,
                cwd=ROOT,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout or self.command_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise AcceptanceFailure(
                f"CLI command timed out: {args!r}\nstdout={error.stdout!r}\nstderr={error.stderr!r}"
            ) from error
        stdout = completed.stdout.decode("utf-8", errors="replace")
        stderr = completed.stderr.decode("utf-8", errors="replace")
        if self.trace_path is not None:
            trace = {
                "args": args,
                "returncode": completed.returncode,
                "stdout_bytes": base64.b64encode(completed.stdout).decode("ascii"),
                "stderr_bytes": base64.b64encode(completed.stderr).decode("ascii"),
            }
            with self.trace_path.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(trace, ensure_ascii=True) + "\n")
        try:
            response = json.loads(stdout)
        except json.JSONDecodeError as error:
            raise AcceptanceFailure(
                f"CLI did not return one JSON envelope for {args!r}; exit={completed.returncode}\n"
                f"stdout={stdout!r}\nstderr={stderr!r}"
            ) from error
        if not isinstance(response, dict) or not isinstance(response.get("ok"), bool):
            raise AcceptanceFailure(f"Malformed JSON envelope: {completed.stdout!r}")
        if response.get("protocolVersion") != 1:
            raise AcceptanceFailure(f"Unsupported or missing protocol version: {response}")
        if completed.returncode not in expected_codes:
            raise AcceptanceFailure(
                f"Unexpected exit code for {args!r}: expected one of {sorted(expected_codes)}, got {completed.returncode}\n"
                f"response={json.dumps(response, ensure_ascii=False)}\nstderr={stderr}"
            )
        if response["ok"] != (completed.returncode == 0):
            raise AcceptanceFailure(
                f"Envelope ok does not match exit status for {args!r}: "
                f"{json.dumps(response, ensure_ascii=False)}"
            )
        if completed.returncode:
            expected_error = next((name for name, code in EXIT_CODES.items() if code == completed.returncode), None)
            actual_error = _find(response, "code")
            if expected_error and actual_error != expected_error:
                raise AcceptanceFailure(
                    f"Exit {completed.returncode} used error category {actual_error!r}, expected {expected_error!r}: {response}"
                )
        if response.get("command") not in (None, arguments[0] if arguments else None):
            raise AcceptanceFailure(f"Response command does not match request: {response}")
        return response


class AgentSessionAcceptance:
    def __init__(self, client: Client, timeout_ms: int, keep_temp: bool = False):
        self.client = client
        self.timeout_ms = timeout_ms
        self.keep_temp = keep_temp
        self.temp = None if keep_temp else tempfile.TemporaryDirectory(prefix="synthcad-agent-acceptance-")
        self.temp_root = Path(tempfile.mkdtemp(prefix="synthcad-agent-acceptance-")
                              if self.temp is None else self.temp.name).resolve()
        self.client.session_dir = self.temp_root / "isolated-sessions"
        self.client.trace_path = self.temp_root / "cli-trace.jsonl"
        self.test_prefix = f"codex-agent-test-{os.getpid()}"
        self.owned: list[dict[str, Any]] = []
        self.paths = self._make_fixtures()
        self.session_a = self.test_prefix + "-review"
        self.session_b = self.test_prefix + "-ornament"

    def _make_fixtures(self) -> dict[str, Path]:
        # Both path components exercise Unicode and spaces on Windows/Linux.
        work = self.temp_root / "Prøva Space Δ"
        project = work / "Reviewed Assembly"
        ornament = work / "Curved Ornament.js"
        project.parent.mkdir(parents=True)
        shutil.copytree(FIXTURES / "review-project", project)
        shutil.copyfile(FIXTURES / "curved-ornament.js", ornament)
        return {
            "root": work,
            "project": project,
            "config": project / "synthcad.json",
            "entry": project / "assembly.js",
            "inspection": project / "inspection.js",
            "module": project / "modules" / "shape-library.js",
            "ornament": ornament,
            "screenshot": work / "review screenshot.png",
            "bad_project": work / "Malformed Project",
        }

    def check(self, condition: bool, message: str) -> None:
        if not condition:
            raise AcceptanceFailure(message)

    def _opened_pids(self) -> list[int]:
        output: list[int] = []
        for owned in self.owned:
            output.extend(owned["pids"])
        return list(dict.fromkeys(output))

    def _remember_session(self, response: dict[str, Any], path: Path, name: str) -> None:
        data = response.get("data", {})
        session_id = _find(response, "session", "sessionId")
        if not isinstance(session_id, str) or not session_id:
            session_id = name
        pids = [pid for pid, _ in _pid_records(data)]
        self.owned.append({"id": session_id, "name": name, "path": path, "pids": pids})

    def open(self, path: Path, session: str) -> dict[str, Any]:
        response = self.client.call(
            "open", str(path), "--session", session, "--hidden"
        )
        record_path = path / "synthcad.json" if path.is_dir() else path
        self._remember_session(response, record_path, session)
        return response

    def revision(self, session: str) -> str:
        deadline = time.monotonic() + self.timeout_ms / 1000
        while True:
            response = self.client.call(
                "revision", "--session", session, expected_code=(0, EXIT_CODES["busy"])
            )
            if response["ok"]:
                return _revision(response)
            if self._error_code(response) != "busy":
                raise AcceptanceFailure(f"revision request failed: {response}")
            if time.monotonic() >= deadline:
                raise AcceptanceFailure(f"revision stayed busy while the session was loading: {response}")
            time.sleep(0.05)

    def wait(self, session: str, revision: str, expected_code: int = 0) -> dict[str, Any]:
        response = self.client.call(
            "wait", "--session", session, "--revision", revision,
            "--timeout", str(self.timeout_ms), expected_code=expected_code,
            timeout=self.timeout_ms / 1000 + 5,
        )
        return response

    def state(self, session: str) -> dict[str, Any]:
        return self.client.call("state", "--session", session)

    def snapshot(self, session: str, *extra: str, expected_code: int = 0) -> dict[str, Any]:
        return self.client.call(
            "snapshot", "--session", session, *extra, expected_code=expected_code
        )

    def _load_state(self, response: dict[str, Any]) -> str:
        # Overview/profile records also have status fields. The scene state is
        # the immediate response payload, not the first nested metadata status.
        payload = response.get("data", response)
        value = next((payload[key] for key in ("loadState", "status", "state")
                      if isinstance(payload, dict) and key in payload), None)
        return value.casefold() if isinstance(value, str) else ""

    def _displayed_revision(self, response: dict[str, Any]) -> str:
        value = _find(response, "displayedRevision", "loadedRevision", "currentRevision")
        if isinstance(value, str) and value:
            return value
        return ""

    def _error_code(self, response: dict[str, Any]) -> str:
        value = _find(response, "code")
        return value.casefold() if isinstance(value, str) else ""

    def _assert_revision(self, response: dict[str, Any], revision: str, label: str) -> None:
        found = self._displayed_revision(response)
        if not found:
            found = response.get("revision", "")
        self.check(found == revision, f"{label}: expected revision {revision!r}, got {found!r}; {response}")

    def _part_ids(self, snapshot: dict[str, Any]) -> set[str]:
        return set(_part_map(snapshot))

    def run(self) -> None:
        # SC02/SC03: stable machine-readable output, no-session behavior, and
        # simultaneous opens to one Unicode project must converge on one ID.
        capabilities = self.client.call("capabilities")
        commands = _find(capabilities, "commands")
        self.check(isinstance(commands, list), f"capabilities omits command list: {capabilities}")
        required = {"open", "sessions", "snapshot", "selection", "state", "revision", "wait", "highlight", "frame", "view", "screenshot"}
        self.check(required.issubset(set(commands)), f"capabilities missing {sorted(required - set(commands))}: {commands}")
        self.client.call("snapshot", expected_code=EXIT_CODES["no_session"])

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            opens = list(pool.map(lambda _: self.open(self.paths["project"], self.session_a), range(2)))
        session_ids = {response.get("session") for response in opens}
        self.check(len(session_ids) == 1, f"simultaneous open created multiple session IDs: {opens}")
        self.check(all(response.get("ok") is True for response in opens), "one simultaneous open failed")
        open_pids = {int(_find(response, "pid")) for response in opens if _find(response, "pid") is not None}
        self.check(len(open_pids) == 1,
                   f"simultaneous opens did not attach to one viewer process: {opens}")
        first_pid = next(iter(open_pids))
        self.check(first_pid > 0, f"open returned an invalid viewer PID: {opens}")

        # Repeating open after the race must attach to the same session.
        repeated = self.open(self.paths["project"], self.session_a)
        self.check(repeated.get("session") in (None, next(iter(session_ids))), f"repeat open changed session: {repeated}")
        repeated_pid = _find(repeated, "pid")
        self.check(repeated_pid is not None and int(repeated_pid) == first_pid,
                   f"repeating open changed the viewer process: {repeated}")

        initial_revision = self.revision(self.session_a)
        initial_wait = self.wait(self.session_a, initial_revision)
        initial_displayed_revision = _find(initial_wait, "displayedRevision") or initial_wait.get("revision")
        self.check(isinstance(initial_displayed_revision, str) and bool(initial_displayed_revision),
                   f"successful wait omitted the displayed graph revision: {initial_wait}")
        self.check(_find(initial_wait, "requestedRevision") == initial_revision,
                   f"wait did not acknowledge the requested view revision: {initial_wait}")
        initial_state = self.state(self.session_a)
        self._assert_revision(initial_state, initial_displayed_revision, "initial successful load")
        initial_snapshot = self.snapshot(self.session_a)
        self._assert_revision(initial_snapshot, initial_displayed_revision, "initial semantic snapshot")
        self.check(_same_path(_find(initial_state, "projectPath"), self.paths["config"]),
                   f"state did not preserve the exact Unicode project path: {initial_state}")
        initial_parts = _part_map(initial_snapshot)
        self.check({"wall", "base", "cap"}.issubset(initial_parts), f"assembly snapshot lost semantic IDs: {initial_parts}")
        self.check(any("Piastra" in json.dumps(part, ensure_ascii=False) for part in initial_parts.values()),
                   "authored accented names were not preserved in snapshot")
        wall = initial_parts["wall"]
        self.check(_find(wall, "exportable") is False,
                   f"external reference did not retain its authored export exclusion: {wall}")
        self.check(_find(wall, "bounds") is not None,
                   f"semantic part snapshot omitted bounds: {wall}")
        self.check(_find(initial_parts["base"], "group") == ["Oggetto progettato", "Supporto"],
                   f"semantic snapshot changed authored group labels: {initial_parts['base']}")
        self.check(_find(initial_snapshot, "annotations") is not None,
                   f"semantic snapshot omitted authored dimensions: {initial_snapshot}")
        dependencies = _find(initial_snapshot, "dependencies")
        self.check(isinstance(dependencies, list) and any("shape-library.js" in str(path) for path in dependencies),
                   f"snapshot did not report the imported model module: {dependencies}")

        # A standalone model opens without project conversion. A second active
        # project makes implicit session selection ambiguous.
        self.open(self.paths["ornament"], self.session_b)
        ornament_revision = self.revision(self.session_b)
        ornament_wait = self.wait(self.session_b, ornament_revision)
        ornament_displayed_revision = _find(ornament_wait, "displayedRevision") or ornament_wait.get("revision")
        self.check(isinstance(ornament_displayed_revision, str) and bool(ornament_displayed_revision),
                   f"standalone scene wait omitted the displayed graph revision: {ornament_wait}")
        ornament_snapshot = self.snapshot(self.session_b)
        self.check({"foot", "stem", "bead"}.issubset(self._part_ids(ornament_snapshot)),
                   f"standalone scene did not expose its named parts: {ornament_snapshot}")
        sessions = self.client.call("sessions")
        session_records = _find(sessions, "sessions")
        self.check(isinstance(session_records, list) and len(session_records) >= 2,
                   f"two projects were not visible as two sessions: {sessions}")
        self.client.call("snapshot", expected_code=EXIT_CODES["ambiguous_session"])
        targeted = self.snapshot(self.session_a)
        self.check({"wall", "base", "cap"}.issubset(self._part_ids(targeted)),
                   "named session addressed the wrong active project")

        # SC05: views, semantic state, highlights, framing and screenshots.
        active_view = _find(targeted, "activeView", "view")
        self.check(active_view in ("assembly", None), f"wrong default project view: {active_view!r}")
        self.client.call("view", "inspection", "--session", self.session_a)
        inspect_revision = self.revision(self.session_a)
        inspect_wait = self.wait(self.session_a, inspect_revision)
        inspect_displayed_revision = _find(inspect_wait, "displayedRevision") or inspect_wait.get("revision")
        self.check(isinstance(inspect_displayed_revision, str) and bool(inspect_displayed_revision),
                   f"inspection wait omitted the displayed graph revision: {inspect_wait}")
        inspection = self.snapshot(self.session_a)
        self.check({"body", "pin"}.issubset(self._part_ids(inspection)),
                   f"switching views did not load inspection model: {inspection}")
        self.check(_find(inspection, "view") == "inspection",
                   f"active view was not reported after switching: {inspection}")
        self.client.call("view", "assembly", "--session", self.session_a)
        assembly_revision = self.revision(self.session_a)
        assembly_wait = self.wait(self.session_a, assembly_revision)
        assembly_displayed_revision = _find(assembly_wait, "displayedRevision") or assembly_wait.get("revision")
        self.check(isinstance(assembly_displayed_revision, str) and bool(assembly_displayed_revision),
                   f"assembly wait omitted the displayed graph revision: {assembly_wait}")
        before_selection = self.client.call("selection", "--session", self.session_a)
        before_snapshot = self.snapshot(self.session_a)
        before_parts = _part_map(before_snapshot)
        before_export = {
            part_id: _find(part, "exportable", "exportEnabled")
            for part_id, part in before_parts.items()
        }

        self.client.call("frame", "base", "cap", "--session", self.session_a)
        stale_token = "0" * 64
        self.snapshot(self.session_a, "--expect-revision", stale_token,
                      expected_code=EXIT_CODES["stale_revision"])
        self.client.call("highlight", "base", "--session", self.session_a, "--frame")
        highlighted = self.snapshot(self.session_a)
        highlights = _find(highlighted, "highlights", "agentHighlights")
        self.check("base" in json.dumps(highlights, ensure_ascii=False),
                   f"highlight command did not expose the highlighted ID: {highlighted}")
        after_selection = self.client.call("selection", "--session", self.session_a)
        after_parts = _part_map(highlighted)
        after_export = {
            part_id: _find(part, "exportable", "exportEnabled")
            for part_id, part in after_parts.items()
        }
        self.check(before_selection.get("data") == after_selection.get("data"),
                   f"agent highlight changed user selection: {before_selection} -> {after_selection}")
        self.check(before_export == after_export,
                   f"agent highlight changed export flags: {before_export} -> {after_export}")
        self.client.call("highlight", "missing-fixture-id", "--session", self.session_a,
                         "--expect-revision", assembly_displayed_revision,
                         expected_code=EXIT_CODES["not_found"])
        self.client.call("highlight", "base", "--session", self.session_a,
                         "--expect-revision", stale_token,
                         expected_code=EXIT_CODES["stale_revision"])
        self.client.call("highlight", "--clear", "--session", self.session_a)
        cleared = self.snapshot(self.session_a)
        cleared_highlights = _find(cleared, "highlights", "agentHighlights")
        self.check(not cleared_highlights, f"clear left agent highlights behind: {cleared_highlights}")
        camera_before_reload = _find(cleared, "camera")

        screenshot = self.paths["screenshot"]
        self.client.call("screenshot", str(screenshot), "--session", self.session_a)
        self.check(screenshot.is_file() and screenshot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"),
                   f"screenshot was not a PNG at the requested Unicode path: {screenshot}")
        self.client.call("screenshot", str(screenshot), "--session", self.session_a,
                         expected_code=EXIT_CODES["io_error"])
        self.client.call("screenshot", str(screenshot), "--session", self.session_a, "--replace")

        # SC01/SC04: project errors leave their source untouched; a change in
        # an imported module changes the requested revision and load outcome.
        bad_project = self.paths["bad_project"]
        bad_project.mkdir()
        malformed = bad_project / "synthcad.json"
        malformed_bytes = b'{"schemaVersion":1,"defaultView":"missing","views":{}}\n'
        malformed.write_bytes(malformed_bytes)
        self.client.call("open", str(bad_project), "--session", self.test_prefix + "-malformed",
                         "--hidden", expected_code=EXIT_CODES["invalid_argument"])
        self.check(malformed.read_bytes() == malformed_bytes,
                   "opening malformed project metadata rewrote the source")

        module = self.paths["module"]
        valid_module = module.read_text(encoding="utf-8")
        self.check("size:[24,24,5]" in valid_module, "fixture module changed unexpectedly")
        changed_module = valid_module.replace("size:[24,24,5]", "size:[25,24,5]", 1)
        module.write_text(changed_module, encoding="utf-8", newline="\n")
        imported_revision = self.revision(self.session_a)
        self.check(imported_revision != assembly_revision,
                   "an imported module edit did not change the desired revision")
        imported_wait = self.wait(self.session_a, imported_revision)
        imported_displayed_revision = _find(imported_wait, "displayedRevision") or imported_wait.get("revision")
        self.check(isinstance(imported_displayed_revision, str) and bool(imported_displayed_revision),
                   f"imported-module wait omitted the displayed graph revision: {imported_wait}")
        imported_state = self.state(self.session_a)
        self._assert_revision(imported_state, imported_displayed_revision, "imported module reload")
        imported_snapshot = self.snapshot(self.session_a)
        self.check(_find(imported_snapshot, "camera") == camera_before_reload,
                   "hot reload changed the review camera")

        # Hold the temporary fixture's JS evaluation for a bounded interval so
        # wait can be shown to time out on a real, known revision before it is
        # later acknowledged. This edit exists only in the temporary project.
        slow_module = (
            changed_module
            + "\nconst agentAcceptancePauseUntil = Date.now() + 1500;\n"
            + "while (Date.now() < agentAcceptancePauseUntil) {}\n"
        )
        module.write_text(slow_module, encoding="utf-8", newline="\n")
        slow_revision = self.revision(self.session_a)
        self.check(slow_revision != imported_revision,
                   "the delayed imported-module edit did not change the desired revision")
        timed_wait = self.client.call(
            "wait", "--session", self.session_a, "--revision", slow_revision,
            "--timeout", "1", expected_code=EXIT_CODES["timeout"], timeout=5,
        )
        self.check(self._error_code(timed_wait) == "timeout",
                   f"bounded wait returned the wrong outcome: {timed_wait}")
        slow_wait = self.wait(self.session_a, slow_revision)
        slow_displayed_revision = _find(slow_wait, "displayedRevision") or slow_wait.get("revision")
        self.check(isinstance(slow_displayed_revision, str) and bool(slow_displayed_revision),
                   f"slow-module wait omitted the displayed graph revision: {slow_wait}")
        slow_state = self.state(self.session_a)
        self._assert_revision(slow_state, slow_displayed_revision, "revision after bounded timeout")

        # An invalid imported edit must fail the requested revision while
        # preserving the last successfully displayed model and token.
        broken_module = slow_module + "\nthis is not valid JavaScript !!!\n"
        module.write_text(broken_module, encoding="utf-8", newline="\n")
        failed_revision = self.revision(self.session_a)
        self.check(failed_revision != imported_revision, "broken edit kept the previous desired revision")
        failure = self.wait(self.session_a, failed_revision, expected_code=EXIT_CODES["load_failed"])
        self.check(self._error_code(failure) in ("load_failed", "loadfailed"),
                   f"broken edit did not return a load failure category: {failure}")
        failed_state = self.state(self.session_a)
        self.check(self._displayed_revision(failed_state) == slow_displayed_revision,
                   f"failed revision replaced the last valid displayed revision: {failed_state}")
        self.check(self._load_state(failed_state) in {"failed", "error", "load_failed", "loadfailed"},
                   f"state did not expose the failed load: {failed_state}")
        self.check(_find(failed_state, "diagnostic", "error", "message") is not None,
                   f"failed state omitted actionable diagnostics: {failed_state}")
        self.check(_find(failed_state, "exportValid") is False,
                   f"failed current scene did not disable export: {failed_state}")
        retained = self.snapshot(self.session_a)
        self.check({"wall", "base", "cap"}.issubset(self._part_ids(retained)),
                   f"failed load discarded the last valid review model: {retained}")
        self._assert_revision(retained, slow_displayed_revision, "retained model after failed load")

        # Recover by changing the imported source again. Then a made-up token
        # must time out rather than acknowledging the unchanged valid snapshot.
        recovered_module = changed_module.replace("size:[25,24,5]", "size:[26,24,5]", 1)
        module.write_text(recovered_module, encoding="utf-8", newline="\n")
        recovered_revision = self.revision(self.session_a)
        self.check(recovered_revision != failed_revision, "recovery did not request a new revision")
        recovered_wait = self.wait(self.session_a, recovered_revision)
        recovered_displayed_revision = _find(recovered_wait, "displayedRevision") or recovered_wait.get("revision")
        self.check(isinstance(recovered_displayed_revision, str) and bool(recovered_displayed_revision),
                   f"recovery wait omitted the displayed graph revision: {recovered_wait}")
        recovered_state = self.state(self.session_a)
        self._assert_revision(recovered_state, recovered_displayed_revision, "recovered load")
        self.check(self._load_state(recovered_state) not in {"failed", "error", "load_failed", "loadfailed"},
                   f"load state stayed failed after recovery: {recovered_state}")
        self.wait(self.session_a, "unknown-revision-token", expected_code=EXIT_CODES["stale_revision"])

        # Standalone scenes are still valid projects and do not need conversion.
        final_ornament = self.snapshot(self.session_b)
        self.check({"foot", "stem", "bead"}.issubset(self._part_ids(final_ornament)),
                   "the second session changed while another project reloaded")

    def cleanup(self) -> list[str]:
        """Terminate only PIDs verified against sessions this run opened."""
        issues: list[str] = []
        if self.owned:
            try:
                listed = self.client.call("sessions")
                records = _find(listed, "sessions")
                if isinstance(records, list):
                    for owned in self.owned:
                        for record in records:
                            if not isinstance(record, dict):
                                continue
                            record_id = _find(record, "session", "sessionId")
                            path_value = _find(record, "projectPath")
                            if record_id != owned["id"] and record_id != owned["name"]:
                                continue
                            if not _same_path(path_value, owned["path"]):
                                continue
                            owned["pids"].extend(pid for pid, _ in _pid_records(record))
            except Exception as error:  # cleanup must not hide the acceptance failure
                issues.append(f"could not read session records during cleanup: {error}")

        current_pid = os.getpid()
        for pid in self._opened_pids():
            if pid == current_pid:
                issues.append(f"refused to terminate the harness process ({pid})")
                continue
            try:
                _terminate_owned_pid(pid)
            except PermissionError:
                issues.append(f"permission denied while stopping test-owned viewer PID {pid}")
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


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cli_path", nargs="?", help="SynthCAD CLI executable (optional if installed or found in out/build)")
    parser.add_argument("viewer_path", nargs="?", help="Optional viewer executable used by a separate CLI launcher")
    parser.add_argument("--cli", dest="cli_option", help="CLI executable; overrides CLI_PATH")
    parser.add_argument("--viewer", dest="viewer_option", help="Viewer executable; overrides VIEWER_PATH")
    parser.add_argument("--timeout", type=int, default=10000, help="milliseconds for bounded CLI waits (default: 10000)")
    parser.add_argument("--keep-temp", action="store_true", help="preserve temporary scenes and raw CLI trace for debugging")
    args = parser.parse_args(argv)
    if args.timeout < 100:
        parser.error("--timeout must be at least 100 ms")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    harness: AgentSessionAcceptance | None = None
    try:
        executable = discover_executable(args.cli_option or args.cli_path)
        viewer_text = args.viewer_option or args.viewer_path
        viewer = discover_executable(viewer_text) if viewer_text else None
        client = Client(executable, viewer, command_timeout=max(args.timeout / 1000 + 10, 20))
        harness = AgentSessionAcceptance(client, args.timeout, args.keep_temp)
        harness.run()
        print("PASS: Batch1 SC01–SC05 agent-session acceptance")
        return_code = 0
    except (AcceptanceFailure, OSError, ValueError, TypeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return_code = 1
    finally:
        if harness is not None:
            cleanup_issues = harness.cleanup()
            for issue in cleanup_issues:
                print(f"CLEANUP: {issue}", file=sys.stderr)
            if cleanup_issues and return_code == 0:
                return_code = 1
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
