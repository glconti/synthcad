#!/usr/bin/env python3
"""Build canonical CMake tests and run public acceptance harnesses, sequentially."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
NATIVE = (
    "agent-session", "shared-design", "geometric-selection", "guided-pick",
    "project-overview", "manufacturing-review", "export-workflow", "mesh-reload",
    "physical-feedback",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--logs", type=Path, default=ROOT / "out/ci/logs")
    parser.add_argument("--phase", choices=("build", "unit", "guidance", "native", "dpi"), required=True)
    parser.add_argument("--parallel", type=int, default=2)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    logs = args.logs.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    windows = sys.platform == "win32"
    binaries = build / "viewer" / ("Release" if windows else "")
    suffix = ".exe" if windows else ""
    cli, viewer = (binaries / (name + suffix) for name in ("synthcad", "dingcad_viewer"))

    def run(name, command, timeout=300):
        print(f"RUN {name}", flush=True)
        with (logs / (name + ".log")).open("w", encoding="utf-8") as output:
            output.write(json.dumps([str(item) for item in command]) + "\n")
            output.flush()
            try:
                result = subprocess.run([str(item) for item in command], cwd=ROOT,
                                        stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
            except subprocess.TimeoutExpired:
                output.write(f"\nFAIL timeout after {timeout} seconds\n")
                raise
        if result.returncode:
            print((logs / (name + ".log")).read_text(encoding="utf-8", errors="replace")[-16000:])
            raise RuntimeError(f"{name} exited {result.returncode}; see {logs}")
        print(f"PASS {name}", flush=True)

    if args.phase == "build":
        run("build", ["cmake", "--build", build, "--config", "Release", "--parallel", args.parallel,
                      "--target", "dingcad_viewer", "synthcad", "synthcad_tests"], timeout=3600)
    elif args.phase == "unit":
        command = ["ctest", "--test-dir", str(build), "-C", "Release"]
        inventory = json.loads(subprocess.check_output(command + ["--show-only=json-v1"], cwd=ROOT))
        names = {test["name"] for test in inventory["tests"]}
        legacy = {"dingcad_appearance_tests", "dingcad_parts_tests", "dingcad_camera_tests", "dingcad_dimension_tests"}
        if len(names) < 26 or not legacy <= names:
            raise RuntimeError(f"Incomplete CTest registration: {len(names)} suites; missing {legacy - names}")
        (logs / "test-inventory.json").write_text(json.dumps(inventory, indent=2), encoding="utf-8")
        run("ctest", command + ["--output-on-failure", "--verbose", "--timeout", "180"], timeout=1800)
    elif args.phase == "guidance":
        run("agent-guidance", [sys.executable, ROOT / "scripts/test-agent-guidance.py", "--cli", cli])
        if windows:
            run("windows-icon", [sys.executable, ROOT / "scripts/ci-windows-icon.py", viewer])
    elif args.phase == "dpi":
        if windows or not os.environ.get("DISPLAY"):
            raise RuntimeError("DPI acceptance requires a Linux X11 DISPLAY")
        original = subprocess.check_output(["xrdb", "-query"])
        try:
            for dpi, scale in ((144, 1.5), (192, 2.0)):
                subprocess.run(["xrdb", "-merge"], input=f"Xft.dpi: {dpi}\n".encode(), check=True)
                for harness in ("guided-pick", "geometric-selection"):
                    run(f"{harness}-dpi-{dpi}", [sys.executable, ROOT / f"scripts/test-{harness}.py",
                        "--cli", cli, "--viewer", viewer, "--expected-scale", scale], timeout=600)
        finally:
            subprocess.run(["xrdb", "-load"], input=original, check=True)
    else:
        if not windows and not os.environ.get("DISPLAY"):
            raise RuntimeError("Native Linux acceptance requires X11 DISPLAY; run under xvfb-run")
        failures = []
        for harness in NATIVE:
            try:
                run(harness, [sys.executable, ROOT / f"scripts/test-{harness}.py", "--cli", cli, "--viewer", viewer], timeout=600)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(str(error))
        if windows:
            try:
                run("windows-ui", [sys.executable, ROOT / "scripts/test-windows-ui.py", viewer], timeout=300)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(str(error))
        if failures:
            raise RuntimeError("\n".join(failures))


if __name__ == "__main__":
    main()
