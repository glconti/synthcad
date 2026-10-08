#!/usr/bin/env python3
"""Public CLI export/history acceptance; stdlib only, no GUI input or test hooks.

Copies the public plate-review fixture into a unique Unicode temporary project,
uses its own session directory, and terminates only PIDs reported by its opens.
On Linux run under an X display (Xvfb is sufficient). --keep-temp saves evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


session = module("export_session", "test-agent-session.py")
core = module("export_core", "test-three-mf-export.py")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-export-"))
    print(f"Test evidence directory: {folder}", flush=True)
    project = folder / "Prøva Δ città 日本"
    shutil.copytree(session.FIXTURES / "plate-review", project)
    source, design, manifest = (project / name for name in ("parts.js", "design.js", "synthcad.json"))
    baseline_source, baseline_design = source.read_text(encoding="utf-8"), design.read_text(encoding="utf-8")
    manifest_hash = digest(manifest)
    source_key = source.resolve().as_posix()
    if os.name == "nt":
        source_key = source_key.lower()
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 40)
    client.session_dir, client.trace_path = folder / "sessions", folder / "cli-trace.jsonl"
    name, owned = "export-workflow", []

    def call(*cmd, **kwargs):
        return client.call(*cmd, "-s", name, **kwargs)

    def stop(pid):
        try:
            session._terminate_owned_pid(pid)
        except OSError:
            stat = Path(f"/proc/{pid}/stat")
            if os.name == "nt" or not stat.exists() or stat.read_text().split()[2] != "Z":
                raise

    def ready(previous=None, code=0):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            response = call("revision", expected_code=(0, 11))
            if response["ok"]:
                token = session._revision(response)
                if previous is None or token != previous:
                    return call("wait", "--revision", token, "--timeout", "25000", expected_code=code)
            time.sleep(.1)
        raise RuntimeError("Source revision did not settle")

    def edit(path, text, code=0):
        previous = session._revision(call("revision"))
        path.write_text(text, encoding="utf-8")
        ready(previous, code)

    def view(view_id):
        call("view", view_id)
        ready()
        require(call("snapshot")["data"]["view"] == view_id, "Named view did not load")

    def open_viewer():
        response = call("open", str(project), "--hidden")
        pids = [pid for pid, _ in session._pid_records(response)]
        require(len(set(pids)) == 1, "Open did not report exactly one owned viewer PID")
        owned.extend(pids)
        ready()
        view("plate-a")
        return pids[0]

    def history():
        result = call("export-history")["data"]
        require(isinstance(result.get("records"), list) and isinstance(result.get("diagnostics"), list), "History cache schema missing")
        return result

    def saved(*cmd):
        result = call("export", *cmd)["data"]
        require(result["code"] == "saved" and result["history"]["saved"] is True, "Export or history persistence failed")
        record = result["record"]
        path = Path(record["path"])
        require(path.is_file() and digest(path) == record["sha256"] and path.stat().st_size == record["sizeBytes"], "Receipt does not describe committed bytes")
        require(json.loads(Path(result["history"]["path"]).read_text(encoding="utf-8")) == record, "Persisted metadata differs from exact export receipt")
        require(record["basis"]["revision"] == call("snapshot")["data"]["displayedRevision"], "Receipt displayed revision mismatch")
        for key in ("modelRevision", "sourceRevision", "layoutRevision"):
            require(record["basis"][key], "Receipt revision basis missing")
        require(record["dependencies"][source_key] == digest(source), "Receipt imported-source digest mismatch")
        require(digest(manifest) == manifest_hash, "Export changed authored manifest metadata")
        return record

    try:
        capabilities = client.call("capabilities")["data"]
        require(capabilities["export"] is True and capabilities["exportHistory"] is True
                and set(capabilities["exportFormats"]) == {"3mf", "stl"}, "Export discovery missing")
        pid = open_viewer()
        path = folder / "Piatto città 日本.3mf"
        before = call("snapshot")["data"]
        revision = before["displayedRevision"]
        review = call("export", str(path), "--dry-run", "--expect-revision", revision)["data"]
        require(review["code"] == "dry_run" and review["review"]["format"] == "3mf" and not path.exists(), "Dry run wrote a destination or omitted review")
        require(review["review"]["hasWarnings"] is True, "Uncomputed physical checks must remain review risks")
        require(set(review["review"]["partIds"]) == {"panel-a", "panel-b", "pin-1"}, "Aliases changed the physical export set")
        require(history()["records"] == [], "Dry run created a history record")
        failed = call("export", str(path), expected_code=14)
        require(failed["error"]["code"] == "warnings_present" and not path.exists(), "Warnings did not block unacknowledged export")
        record = saved(str(path), "--allow-warnings", "--expect-revision", revision)
        require(record["quantities"] == [{"sourcePartId": "panel", "count": 2}, {"sourcePartId": "pin", "count": 1}], "Receipt physical quantities wrong")
        validated = core.inspect(path)
        require(len(validated["buildInstances"]) == 3 and validated["meshResources"] == 2, "3MF source reuse or copy count incorrect")
        (folder / "3mf-validation.json").write_text(json.dumps(validated, ensure_ascii=False, indent=2), encoding="utf-8")
        original_hash = digest(path)
        call("export", str(path), "--allow-warnings", expected_code=15)
        require(digest(path) == original_hash, "Unconfirmed overwrite changed existing bytes")
        replacement = saved(str(path), "--allow-warnings", "--replace", "--format", "3mf")
        require(replacement["id"] != record["id"], "Distinct export receipts reused identity")
        original_hash = digest(path)
        stl = folder / "current-plate.stl"
        stl_record = saved(str(stl), "--allow-warnings", "--visible-only")
        binary = stl.read_bytes()
        require(stl_record["format"] == "stl" and len(binary) >= 84, "STL inference failed")
        triangles = struct.unpack_from("<I", binary, 80)[0]
        require(triangles > 24 and len(binary) == 84 + 50 * triangles, "STL triangle count/length incorrect")
        require(stl_record["partIds"] == record["partIds"], "Visible-only export changed an entirely visible set")
        stale_path = folder / "stale.3mf"
        call("export", str(stale_path), "--allow-warnings", "--expect-revision", "0" * 64, expected_code=7)
        require(not stale_path.exists(), "Stale guarded request wrote a file")
        after = call("snapshot")["data"]
        require(before["camera"] == after["camera"] and before["selection"] == after["selection"] and before["parts"] == after["parts"], "Export changed camera, selection or part flags")
        view("assembly")
        assembly = call("export", str(folder / "assembly.3mf"), "--dry-run")["data"]["review"]
        require(set(assembly["partIds"]) == {"panel-a", "panel-b", "pin-1", "spacer-1"}, "Non-exportable external reference leaked into export set")
        view("plate-a")
        stop(pid)
        new_pid = open_viewer()
        require(new_pid != pid, "Restart reused a dead viewer PID")
        records = {item["id"]: item for item in history()["records"]}
        for receipt in (record, replacement, stl_record):
            require(receipt["id"] in records and records[receipt["id"]]["sha256"] == receipt["sha256"] and records[receipt["id"]]["freshness"] == "current", "Export history failed to survive viewer restart")
        edit(source, baseline_source.replace("const panelWidth = 20;", "const panelWidth = 21;"))
        changed = {item["id"]: item for item in history()["records"]}
        require(changed[record["id"]]["freshness"] == "stale" and digest(path) == original_hash, "Source edit did not stale history independently of saved artifact")
        edit(source, baseline_source + "\nthis is invalid javascript !!!\n", 5)
        require(call("state")["data"]["status"] == "failed", "Malformed source did not fail current load")
        blocked = folder / "invalid-scene.3mf"
        call("export", str(blocked), "--allow-warnings", expected_code=7)
        require(not blocked.exists() and digest(path) == original_hash, "Retained geometry exported after current source failure")
        edit(source, baseline_source)
        recovered = folder / "recovered.3mf"
        saved(str(recovered), "--allow-warnings")
        empty_design = baseline_design.replace("{id:'panel', name:", "{id:'panel', exportable:false, name:").replace("{id:'pin', name:", "{id:'pin', exportable:false, name:")
        require(empty_design != baseline_design, "Empty export fixture marker missing")
        edit(design, empty_design)
        empty_path = folder / "empty.3mf"
        call("export", str(empty_path), "--allow-warnings", expected_code=16)
        require(not empty_path.exists(), "Empty export wrote a file")
        edit(design, baseline_design)
        require(call("state")["data"]["exportValid"] is True, "Recovery did not restore export validity")
        (folder / "final-history.json").write_text(json.dumps(history(), ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS public CLI export dry-run/warnings/3MF/STL/overwrite/revisions/empty/recovery/persistent history", flush=True)
    finally:
        for pid in dict.fromkeys(owned):
            stop(pid)
        if not args.keep_temp:
            shutil.rmtree(folder)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    parser.add_argument("--viewer")
    parser.add_argument("--keep-temp", action="store_true")
    run(parser.parse_args())
