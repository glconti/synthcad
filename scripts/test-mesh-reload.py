#!/usr/bin/env python3
"""Public CLI regression for primary loadMesh source tracking; stdlib only.

Creates an original tetrahedron STL and an isolated viewer session. Requires an
X display on Linux. Only viewer PIDs reported by this harness are terminated.
"""
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
spec = importlib.util.spec_from_file_location("mesh_session", ROOT / "scripts/test-agent-session.py")
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def tetrahedron(path, size):
    points = [(0, 0, 0), (size, 0, 0), (0, 1, 0), (0, 0, 1)]
    faces = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]
    data = bytearray(b"Original mesh reload tetrahedron".ljust(80, b"\0"))
    data.extend(struct.pack("<I", len(faces)))
    for face in faces:
        data.extend(struct.pack("<12fH", 0, 0, 0, *(v for index in face for v in points[index]), 0))
    path.write_bytes(data)


def volume(path):
    data = path.read_bytes()
    triangles = struct.unpack_from("<I", data, 80)[0]
    require(len(data) == 84 + 50*triangles, "Exported STL size is invalid")
    result = 0
    for i in range(triangles):
        values = struct.unpack_from("<12fH", data, 84+50*i)
        a, b, c = values[3:6], values[6:9], values[9:12]
        result += sum(a[j]*(b[(j+1)%3]*c[(j+2)%3]-b[(j+2)%3]*c[(j+1)%3]) for j in range(3))/6
    return result


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-mesh-reload-")).resolve()
    print(f"Test evidence directory: {folder}", flush=True)
    mesh, source = folder / "original.stl", folder / "scene.js"
    mesh_key = mesh.resolve().as_posix()
    if os.name == "nt":
        mesh_key = mesh_key.lower()
    tetrahedron(mesh, 1)
    source.write_text("export const scene=loadMesh("+json.dumps(str(mesh))+ ");\n", encoding="utf-8")
    source_digest = hashlib.sha256(source.read_bytes()).hexdigest()
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 40)
    client.session_dir, client.trace_path = folder / 'projects', folder / "cli-trace.jsonl"
    name, owned = "mesh-reload", []

    def call(*commands, **kwargs):
        return client.call(*commands, '--project', name, **kwargs)

    def ready(previous=None, expected=0):
        deadline = time.monotonic()+30
        while time.monotonic() < deadline:
            response = call('project', 'revision', expected_code=(0, 11))
            if response["ok"]:
                token = session._revision(response)
                if previous is None or token != previous:
                    return call('project', 'wait', "--revision", token, "--timeout", "25000", expected_code=expected)
            time.sleep(.1)
        raise RuntimeError("Mesh source revision did not settle")

    def export(filename):
        path = folder / filename
        result = call('print', 'export', str(path), "--allow-warnings")["data"]
        require(result["code"] == "saved" and result["history"]["saved"], "Mesh export/history failed")
        record = result["record"]
        require(record["dependencies"][mesh_key] == hashlib.sha256(mesh.read_bytes()).hexdigest(), "Primary mesh digest missing/wrong in export receipt")
        return path, record

    try:
        opened = client.call('project', 'open', str(source), '--name', name, '--hidden')
        owned.extend(pid for pid, _ in session._pid_records(opened))
        require(len(set(owned)) == 1, "Open must report one owned viewer PID")
        ready()
        initial = call('project', 'inspect')["data"]
        old_revision = initial["displayedRevision"]
        require(initial["parts"][0]["bounds"]["max"] == [1, 1, 1], "Original imported tetrahedron bounds wrong")
        files = call('project', 'revision')["data"]["files"]
        require(files[mesh_key] == hashlib.sha256(mesh.read_bytes()).hexdigest(), "Primary mesh digest absent from source revision")
        original_export, receipt = export("initial.stl")
        require(abs(volume(original_export)-1/6) < 1e-7, "Original imported tetrahedron volume wrong")
        previous = session._revision(call('project', 'revision'))
        tetrahedron(mesh, 2)
        ready(previous)
        changed = call('project', 'inspect')["data"]
        require(changed["displayedRevision"] != old_revision and changed["parts"][0]["bounds"]["max"] == [2, 1, 1], "Mesh-only edit failed to update displayed revision/bounds")
        changed_export, _ = export("changed.stl")
        require(abs(volume(changed_export)-1/3) < 1e-7, "Mesh-only edit failed to update exported volume")
        blocked = folder / "stale.3mf"
        call('print', 'export', str(blocked), "--allow-warnings", "--expect-revision", old_revision, expected_code=7)
        require(not blocked.exists(), "Old mesh revision guard allowed an export")
        records = {item["id"]: item for item in call('print', 'history')["data"]["records"]}
        require(records[receipt["id"]]["freshness"] == "stale", "Mesh-only edit did not stale prior export history")
        previous = session._revision(call('project', 'revision'))
        mesh.unlink()
        ready(previous, expected=5)
        require(call('project', 'inspect')["data"]["status"] == "failed", "Missing imported mesh did not fail current evaluation")
        require(call('project', 'revision')["data"]["files"][mesh_key] == "missing", "Missing primary mesh dependency was discarded")
        failed_export = folder / "missing.3mf"
        call('print', 'export', str(failed_export), "--allow-warnings", expected_code=7)
        require(not failed_export.exists(), "Missing mesh permitted retained geometry export")
        previous = session._revision(call('project', 'revision'))
        tetrahedron(mesh, 3)
        ready(previous)
        recovered = call('project', 'inspect')["data"]
        require(recovered["exportValid"] and recovered["parts"][0]["bounds"]["max"] == [3, 1, 1], "Restoring primary mesh failed recovery")
        recovered_export, _ = export("recovered.stl")
        require(abs(volume(recovered_export)-.5) < 1e-7, "Recovered mesh export volume wrong")
        require(hashlib.sha256(source.read_bytes()).hexdigest() == source_digest, "Harness changed JS source during mesh-only test")
        (folder / "final-snapshot.json").write_text(json.dumps(recovered, indent=2, ensure_ascii=False), encoding="utf-8")
        print("PASS public primary-mesh digest/reload/volume/stale guard/history/missing recovery", flush=True)
    finally:
        for pid in set(owned):
            try:
                session._terminate_owned_pid(pid)
            except OSError:
                stat = Path(f"/proc/{pid}/stat")
                if os.name == "nt" or not stat.exists() or stat.read_text().split()[2] != "Z":
                    raise
        if not args.keep_temp:
            shutil.rmtree(folder)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli")
    parser.add_argument("--viewer")
    parser.add_argument("--keep-temp", action="store_true")
    run(parser.parse_args())
