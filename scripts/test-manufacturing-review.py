#!/usr/bin/env python3
"""SC13–14 public-file manufacturing review acceptance (standard library only).

Copies plate-review into an isolated Unicode temporary project. All edits are
made to that copy. Uses public checks/state/view/highlight commands and owns
only viewer PIDs reported by its own open command. --keep-temp retains evidence.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("overview_tests", ROOT / "scripts/test-project-overview.py")
overview_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overview_tests)
session = overview_tests.session
require = overview_tests.require
FIXTURE = session.FIXTURES / "plate-review"


def run(args):
    folder = Path(tempfile.mkdtemp(prefix="synthcad-manufacturing-"))
    print(f"Test evidence directory: {folder}", flush=True)
    project = folder / "Prøva Δ" / "Piatto città 日本"
    project.parent.mkdir()
    shutil.copytree(FIXTURE, project)
    config = project / "synthcad.json"
    entry = project / "design.js"
    source = project / "parts.js"
    baseline_design = entry.read_text(encoding="utf-8")
    baseline_source = source.read_text(encoding="utf-8")
    baseline_metadata = json.loads(config.read_text(encoding="utf-8"))
    metadata = json.loads(json.dumps(baseline_metadata))
    client = session.Client(session.discover_executable(args.cli), Path(args.viewer).resolve() if args.viewer else None, 35)
    client.session_dir, client.trace_path = folder / 'projects', folder / "cli-trace.jsonl"
    pids = []
    name = "manufacturing-test"
    def call(*cmd, **kwargs):
        return client.call(*cmd, '--project', name, **kwargs)
    def revision():
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            response = call('project', 'revision', expected_code=(0, 11))
            if response["ok"]:
                return session._revision(response)
            time.sleep(.1)
        raise RuntimeError("Revision stayed busy")
    def settle(previous=None, error=0):
        deadline = time.monotonic() + 25
        while True:
            current = revision()
            if previous is None or current != previous:
                return call('project', 'wait', "--revision", current, "--timeout", "25000", expected_code=error)
            require(time.monotonic() < deadline, "Source edit did not change revision")
            time.sleep(.1)
    def edit(path, text, error=0):
        previous = revision()
        path.write_text(text, encoding="utf-8")
        settle(previous, error)
    def manifest():
        edit(config, json.dumps(metadata, ensure_ascii=False, indent=2))
    def view(view_id):
        call('review', 'view', view_id)
        settle()
        report = checks()
        require(report["view"] == view_id, "Checks returned a different named view")
        return report
    def snapshot():
        return call('project', 'inspect')["data"]
    def checks():
        response = call('print', 'checks')
        report = response["data"]
        require(report["schemaVersion"] == 1 and isinstance(report["checks"], list), "Manufacturing report contract missing")
        for check in report["checks"]:
            require(check["result"] in {"passed", "warning", "failed", "not-checked"}, "Unknown engine result")
            require(check["scope"] in {"geometry", "heuristic", "sliced", "physical"}, "Unknown evidence scope")
            require(check["basis"] == report["basis"] and check["method"] and isinstance(check["evidence"], dict)
                    and isinstance(check["nextActions"], list) and isinstance(check["partIds"], list), "Check evidence/basis incomplete")
        return report
    def check(report, check_id, expected):
        found = next((item for item in report["checks"] if item["id"] == check_id), None)
        require(found is not None and found["result"] == expected,
                f"Expected {check_id}={expected}; received {found}")
        return found
    def pose(instance, translation, rotation=(0, 0, 0)):
        old_rotation = [0, 0, 90] if instance == "panel-b" else [0, 0, 0]
        old_translation = {"panel-a": [20, 20, 0], "panel-b": [70, 20, 0], "pin-1": [20, 55, 0]}[instance]
        def numbers(values):
            return ",".join(map(str, values))
        old = f"'{instance}':{{rotate:[{numbers(old_rotation)}],translate:[{numbers(old_translation)}]}}"
        new = f"'{instance}':{{rotate:[{numbers(rotation)}],translate:[{numbers(translation)}]}}"
        require(old in baseline_design, "Fixture placement marker missing")
        edit(entry, baseline_design.replace(old, new))
        return checks()
    def restore_design():
        if entry.read_text(encoding="utf-8") != baseline_design:
            edit(entry, baseline_design)
    def screenshot(label):
        path = folder / (label + ".png")
        call('review', 'screenshot', str(path))
        require(path.read_bytes().startswith(b"\x89PNG"), "Screenshot missing")
    try:
        opened = client.call('project', 'open', str(project), "--hidden", '--name', name)
        pids.extend(pid for pid, _ in session._pid_records(opened))
        require(pids, "Open did not report owned viewer PID")
        settle()
        native = (overview_tests.selection.WindowsInput(pids[0]) if os.name == "nt"
                  else overview_tests.selection.X11Input(pids[0]))
        if args.expected_scale is not None:
            require(abs(native.scale - args.expected_scale) < .02, "Requested DPI scale was not applied")
        print(f"Owned window PID {pids[0]}: {native.width}x{native.height}, UI scale {native.scale}", flush=True)
        assembly = checks()
        require(assembly["view"] == "assembly" and assembly["kind"] == "assembly", "Assembly check context wrong")
        check(assembly, "plate-bounds", "not-checked")
        assembly_overlap = baseline_design.replace("transform:{translate:[30,0,0]}", "transform:{translate:[5,0,0]}")
        edit(entry, assembly_overlap)
        check(checks(), "part-overlap", "warning")
        restore_design()
        plate = view("plate-a")
        require(plate["kind"] == "plate" and plate["profileStatus"] == "complete", "Plate/profile role missing")
        require(plate["current"] is True and plate["displayedRevision"] == snapshot()["displayedRevision"]
                and all(plate["basis"].get(key) for key in ("view", "modelRevision", "sourceRevision", "profileRevision")),
                "Current manufacturing revision basis incomplete")
        require(plate["bed"]["size"] == [100, 100, 80], "Profile bed dimensions changed")
        require({p["id"] for p in plate["parts"]} == {"panel-a", "panel-b", "pin-1"}, "Alias groups created duplicate physical parts")
        panel_b = next(p for p in plate["parts"] if p["id"] == "panel-b")
        require(panel_b["sourcePartId"] == "panel" and panel_b["rotation"] == [0, 0, 90]
                and panel_b["translation"] == [70, 20, 0], "Actual placed-part transforms missing")
        require(abs(panel_b["bounds"]["min"][0] - 58) < 1e-5 and abs(panel_b["bounds"]["max"][1] - 40) < 1e-5,
                "Runtime bounds ignored print rotation")
        for check_id in ("geometry-validity", "geometry-empty", "geometry-connectivity", "plate-bounds", "plate-height",
                         "plate-bed-contact", "plate-exclusions", "part-overlap", "plate-clearance", "plate-allowance-bounds",
                         "plate-quantities", "quantity-scope"):
            check(plate, check_id, "passed")
        for check_id in ("minimum-thickness", "strength", "support-removal", "layer-direction", "toolpath"):
            check(plate, check_id, "not-checked")
        quantities = {row["sourcePartId"]: row for row in plate["quantities"]}
        require(quantities["panel"]["expected"] == 2 and quantities["panel"]["authoredInstances"] == 2
                and quantities["panel"]["platePlacements"] == 2 and quantities["panel"]["placedInstances"] == 2,
                "Distinct intentional copies or group aliases counted incorrectly")
        require(quantities["spacer"]["plates"] == [{"instanceId": "spacer-1", "view": "plate-b"}], "Inactive plate quantity omitted")
        guarded = call('print', 'checks', "--expect-revision", "0" * 64, expected_code=7)
        require(not guarded["ok"], "Checks stale revision guard ignored")
        before = snapshot()
        checks()
        after = snapshot()
        require(before["selection"] == after["selection"] and before["camera"] == after["camera"]
                and before["parts"] == after["parts"], "Reading checks mutated selection, visibility, exports or camera")
        call('review', 'frame')
        camera_before_panel = snapshot()["camera"]
        screenshot("plate-good")
        overview_tests.selection.click(native, [242, 77])
        require(snapshot()["camera"] == camera_before_panel, "Opening Checks panel moved camera")
        screenshot("checks-panel")
        camera = snapshot()["camera"]
        overview_tests.wheel(native, -4)
        require(snapshot()["camera"] == camera, "Checks panel scrolling moved camera")
        overview_tests.wheel(native, 20)
        overview_tests.selection.click(native, [484, 40])

        negative = pose("panel-a", [-2, 20, 0])
        check(negative, "plate-bounds", "failed")
        require("panel-a" in check(negative, "plate-bounds", "failed")["evidence"]["outsideParts"], "Negative bounds evidence missing part")
        check(pose("panel-a", [95, 20, 0]), "plate-bounds", "failed")
        check(pose("panel-a", [20, 20, 2]), "plate-bed-contact", "failed")
        below = check(pose("panel-a", [20, 20, -2]), "plate-bed-contact", "failed")
        require(below["evidence"]["belowBedParts"], "Below-bed geometry not distinguished")
        check(pose("panel-a", [20, 20, 79]), "plate-height", "failed")
        exclusions = pose("panel-a", [2, 2, 0])
        warning = check(exclusions, "plate-exclusions", "warning")
        require(warning["scope"] == "heuristic" and warning["evidence"]["possibleConflicts"] and warning["nextActions"], "Exclusion warning lacks useful evidence/actions")
        require(call('project', 'inspect')["data"]["exportValid"] is True, "A manufacturing warning disabled routine export")
        selection_before = snapshot()["selection"]
        call('review', 'highlight', *warning["partIds"])
        require(snapshot()["selection"] == selection_before, "Warning highlight changed selection")
        screenshot("exclusion-warning-highlight")
        call('review', 'highlight', "--clear")
        overview_tests.selection.click(native, [242, 77])
        overview_tests.wheel(native, -20)
        highlighted = False
        export_flags = {p["id"]: p["exportable"] for p in snapshot()["parts"]}
        for _ in range(8):
            for y in range(100, 510, 32):
                overview_tests.selection.click(native, [180, y])
                if snapshot().get("highlights"):
                    highlighted = True
                    break
            if highlighted:
                break
            overview_tests.wheel(native, -4)
        require(highlighted, "Native check action did not highlight affected parts")
        require(snapshot()["selection"] == selection_before and
                {p["id"]: p["exportable"] for p in snapshot()["parts"]} == export_flags,
                "Native check highlight/frame changed selection or export flags")
        screenshot("native-check-highlight")
        overview_tests.wheel(native, -100)
        overview_tests.wheel(native, 12)
        screenshot("warning-check-details")
        overview_tests.wheel(native, 8)
        screenshot("heuristic-warning-evidence")
        overview_tests.selection.click(native, [484, 40])
        overlaps = pose("panel-b", [25, 20, 0])
        overlap = check(overlaps, "part-overlap", "failed")
        require(overlap["evidence"]["overlaps"][0]["intersectionVolume"] > 0, "Overlap result is only an AABB guess")
        screenshot("overlap")
        clearance = pose("panel-b", [41, 20, 0])
        check(clearance, "part-overlap", "passed")
        check(clearance, "plate-clearance", "warning")
        restore_design()

        metadata["plateSettings"]["plate-a"].pop("support")
        manifest()
        missing_allowance = checks()
        require(missing_allowance["settings"]["support"]["value"] is None, "Unknown support allowance silently defaulted")
        require(missing_allowance["basis"]["modelRevision"] == plate["basis"]["modelRevision"],
                "Allowance metadata edit changed the shared model identity")
        check(missing_allowance, "plate-clearance", "not-checked")
        metadata["plateSettings"]["plate-a"]["support"] = 0
        metadata["plateSettings"]["plate-a"].pop("contactTolerance")
        manifest()
        require(checks()["settings"]["contactTolerance"] == {"value": .0001, "origin": "numeric-default", "unit": "mm"}, "Numeric contact default presented as print-fit setting")
        metadata["plateSettings"]["plate-a"]["partGap"] = -1
        manifest()
        require(checks()["settings"]["errors"], "Negative allowance not diagnosed")
        check(checks(), "plate-clearance", "not-checked")
        metadata = json.loads(json.dumps(baseline_metadata))
        metadata["plateSettings"]["plate-a"]["brim"] = 25
        manifest()
        check(checks(), "plate-bounds", "passed")
        check(checks(), "plate-allowance-bounds", "warning")
        metadata = json.loads(json.dumps(baseline_metadata))
        metadata["plateSettings"]["plate-a"]["contactTolerance"] = 100
        manifest()
        check(pose("panel-a", [95, 20, 0]), "plate-bounds", "failed")
        restore_design()
        metadata = json.loads(json.dumps(baseline_metadata))
        (project / "independent.js").write_text("export {design} from './design.js';\n", encoding="utf-8")
        metadata["views"]["inspection"] = "independent.js"
        manifest()
        check(checks(), "quantity-scope", "not-checked")
        metadata = json.loads(json.dumps(baseline_metadata))
        metadata["profiles"]["fixture-machine"]["buildVolume"] = [1e100, 100, 100]
        manifest()
        extreme = checks()
        require(extreme["current"] is True and extreme["profileStatus"] == "complete"
                and extreme["bed"]["size"] == [1e100, 100, 100], "Finite extreme numeric profile metadata was discarded")
        call('review', 'frame')
        camera = snapshot()["camera"]
        camera_values = [camera["fovy"], *camera["position"], *camera["target"], *camera["up"]]
        require(all(isinstance(value, (int, float)) and math.isfinite(value) for value in camera_values),
                "Non-float-safe bed metadata produced an invalid camera")
        require(call('project', 'inspect')["data"]["status"] == "ready", "Non-renderable bed metadata crashed scene review")
        overview_tests.selection.click(native, [242, 77])
        overview_tests.wheel(native, 100)
        screenshot("overflow-bed-numeric-context")
        overview_tests.selection.click(native, [484, 40])
        metadata = json.loads(json.dumps(baseline_metadata))
        manifest()
        require(checks()["current"] is True and checks()["bed"]["size"] == [100, 100, 80],
                "Extreme bed metadata recovery failed")
        metadata.pop("activeProfile"); metadata.pop("profiles")
        manifest()
        missing_profile = checks()
        require(missing_profile["bed"] is None and missing_profile["profileStatus"] == "incomplete", "Missing profile inherited bed data")
        require(missing_profile["basis"]["profileRevision"] is None, "Missing profile retained previous profile revision")
        check(missing_profile, "plate-bounds", "not-checked")
        metadata = json.loads(json.dumps(baseline_metadata))
        metadata["profiles"]["fixture-machine"]["buildVolume"] = [-1, 100, 80]
        manifest()
        invalid_profile = checks()
        require(invalid_profile["profileStatus"] == "invalid", "Invalid profile silently accepted")
        check(invalid_profile, "plate-bounds", "not-checked")
        metadata = json.loads(json.dumps(baseline_metadata))
        metadata["profiles"]["fixture-machine"]["provisional"] = ["buildVolume"]
        manifest()
        provisional = checks()
        require(provisional["profileStatus"] == "incomplete", "Provisional profile presented as complete")
        check(provisional, "plate-bounds", "warning")
        metadata = json.loads(json.dumps(baseline_metadata))
        manifest()

        missing = baseline_design.replace("members:[{instance:'spacer-1'}]", "members:[]")
        missing = missing.replace("placements:{'spacer-1':{rotate:[0,0,0],translate:[20,20,0]}}", "placements:{}")
        require(missing != baseline_design, "Missing quantity fixture marker absent")
        edit(entry, missing)
        require("spacer-1" in check(checks(), "plate-quantities", "failed")["evidence"]["missingInstances"], "Missing physical instance not diagnosed")
        duplicate = baseline_design.replace("members:[{instance:'spacer-1'}]", "members:[{instance:'spacer-1'},{instance:'pin-1'}]")
        edit(entry, duplicate)
        require(check(checks(), "plate-quantities", "failed")["evidence"]["instancesOnMultiplePlates"], "One physical instance duplicated across plates silently accepted")
        wrong_quantity = baseline_design.replace("color:'#628bb5', quantity:2", "color:'#628bb5', quantity:3")
        edit(entry, wrong_quantity)
        check(checks(), "plate-quantities", "failed")
        restore_design()
        check(view("plate-b"), "plate-quantities", "passed")
        require({p["id"] for p in checks()["parts"]} == {"spacer-1"}, "Plate view contains another plate's geometry")
        view("plate-a")
        source_basis = checks()["basis"]
        edit(source, baseline_source.replace("panelWidth = 20", "panelWidth = 22"))
        changed = checks()
        require(changed["basis"]["modelRevision"] != source_basis["modelRevision"]
                and changed["basis"]["sourceRevision"] != source_basis["sourceRevision"], "Changed shared source retained old check basis")
        last_good_revision = revision()
        edit(source, "export const invalid = ;\n", error=5)
        failed = call('project', 'inspect')["data"]
        require(failed["status"] == "failed" and failed["exportValid"] is False, "Failed reload left current export enabled")
        retained = checks()
        require(retained["current"] is False and retained["diagnostic"], "Failed desired source retained a current passing report")
        call('print', 'checks', "--expect-revision", last_good_revision, expected_code=7)
        overview_tests.selection.click(native, [242, 77])
        overview_tests.wheel(native, 100)
        screenshot("failed-source")
        overview_tests.selection.click(native, [484, 40])
        edit(source, baseline_source)
        check(checks(), "plate-bounds", "passed")
        require(call('project', 'inspect')["data"]["exportValid"] is True, "Recovery did not restore export")
        print("PASS manufacturing bounds/contact/overlap/allowances/quantities/profile/guards/source recovery", flush=True)
    finally:
        for pid in dict.fromkeys(pids):
            try:
                session._terminate_owned_pid(pid)
            except OSError:
                stat = Path(f"/proc/{pid}/stat")
                if os.name == "nt" or (stat.exists() and stat.read_text().split()[2] != "Z"):
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
