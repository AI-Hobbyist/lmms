"""Run the foreground Windows build with tqdm progress and a complete build.log."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

from tqdm import tqdm

from build_progress import save_plan, source_progress

ROOT = Path(__file__).resolve().parent.parent
STATUS = ROOT / "build" / "build-progress.json"
LOG = ROOT / "build.log"
EVENTS = ROOT / "build" / "build-events.tsv"
COMPILE_PLAN = ROOT / "build" / "compile-progress.json"
MSBUILD_NAMESPACE = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}


def target_projects(targets):
    """Read the requested Visual Studio dependency graph, including utility targets."""
    build = ROOT / "build"
    solutions = list(build.glob("*.slnx"))
    if solutions:
        projects = [
            build / item.attrib["Path"]
            for item in ET.parse(solutions[0]).iter("Project")
        ]
    else:
        solutions = list(build.glob("*.sln"))
        if not solutions:
            raise ValueError("A configured Visual Studio solution is required.")
        solution = solutions[0].read_text(encoding="utf-8-sig")
        projects = [
            build / path for path in re.findall(r'"([^"\n]+\.vcxproj)"', solution)
        ]
    by_name = {project.stem: project for project in projects}
    pending = []
    for target in targets:
        if target not in by_name:
            raise ValueError(f"Unknown build target: {target}")
        pending.append(by_name[target])
    selected = set()
    while pending:
        project = pending.pop().resolve()
        if project in selected:
            continue
        selected.add(project)
        tree = ET.parse(project)
        for reference in tree.findall(".//msbuild:ProjectReference", MSBUILD_NAMESPACE):
            include = reference.get("Include")
            if include:
                pending.append(project.parent / include)
    return {os.path.normcase(str(project)) for project in selected}


def build_progress(expected):
    completed = set()
    active = {}
    if EVENTS.is_file():
        for line in EVENTS.read_text(
            encoding="utf-8-sig", errors="replace"
        ).splitlines():
            fields = line.split("\t", 1)
            if len(fields) != 2:
                continue
            event, path = fields
            key = os.path.normcase(str(Path(path).resolve()))
            if key not in expected:
                continue
            if event == "start":
                active[key] = Path(path).stem
            elif event in ("finish", "fail"):
                active.pop(key, None)
                if event == "finish":
                    completed.add(key)
    current = ", ".join(active.values()) or "Preparing build"
    return len(completed), len(expected), current


def write_status(state, completed, total, current, configuration, exit_code=None):
    STATUS.write_text(
        json.dumps(
            {
                "state": state,
                "completed": completed,
                "total": total,
                "current": current,
                "configuration": configuration,
                "exit_code": exit_code,
            }
        ),
        encoding="utf-8",
    )


def watch_progress():
    units = (
        json.loads(COMPILE_PLAN.read_text(encoding="utf-8"))
        if COMPILE_PLAN.is_file()
        else None
    )
    with tqdm(desc="LMMS", unit=" files", ascii=True, dynamic_ncols=True) as progress:
        while True:
            try:
                status = json.loads(STATUS.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                # The foreground writer may be updating the shared status file.
                time.sleep(0.1)
                continue
            progress.set_description(f"LMMS {status['configuration']}")
            completed, total = (
                source_progress(units)
                if units is not None
                else (status["completed"], status["total"])
            )
            progress.total = total
            detail = status["current"]
            if status["state"] == "running" and LOG.is_file():
                lines = LOG.read_text(
                    encoding="utf-8-sig", errors="replace"
                ).splitlines()
                source = next(
                    (
                        line.strip()
                        for line in reversed(lines)
                        if re.fullmatch(r"\s*(?:\d+>)?[^:<>]+\.(?:cpp|c|cxx)\s*", line)
                    ),
                    None,
                )
                detail = f"Compiling: {source}" if source else "Preparing build"
            if total and completed == total and status["state"] == "running":
                detail = "Linking / finishing build"
            progress.set_postfix_str(detail)
            progress.update(completed - progress.n)
            if status["state"] != "running":
                break
            time.sleep(1)
    print(f"Exit code: {status['exit_code']}. Full output: {LOG}")
    print("Closing in 5 seconds.")
    time.sleep(5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--target", nargs="+", default=["lmms"])
    parser.add_argument(
        "--jobs", type=int, choices=range(1, 17), default=2, metavar="1..16"
    )
    parser.add_argument(
        "--test", metavar="REGEX", help="Run matching CTest tests instead of building"
    )
    parser.add_argument(
        "--window", action="store_true", help="Open a separate progress window"
    )
    parser.add_argument("--watch", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.watch:
        watch_progress()
        return 0

    powershell = shutil.which("pwsh")
    if os.name != "nt" or powershell is None:
        parser.error("Native Windows and PowerShell 7 (pwsh) are required.")
    if not (ROOT / "build" / "CMakeCache.txt").is_file():
        parser.error("Configure the existing build directory first.")

    try:
        expected = set() if args.test else target_projects(args.target)
    except (ValueError, OSError, ET.ParseError) as error:
        parser.error(str(error))
    EVENTS.write_text("", encoding="utf-8")
    LOG.write_text("", encoding="utf-8")
    units = None if args.test else save_plan(COMPILE_PLAN, expected, args.config)
    if args.test:
        COMPILE_PLAN.unlink(missing_ok=True)
    initial_total = None if args.test else source_progress(units)[1]
    write_status("running", 0, initial_total, "Starting", args.config)
    if args.window:
        subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), "--watch"],
            cwd=ROOT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )

    command = [
        powershell,
        "-NoProfile",
        "-File",
        str(ROOT / "buildtools" / "Build-Lmms.ps1"),
        "-Configuration",
        args.config,
        "-Targets",
        ",".join(args.target),
        "-Jobs",
        str(args.jobs),
    ]
    if args.test:
        command.extend(["-TestPattern", args.test])
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    startup_output = []
    test_progress = [0, None, "Starting tests"]

    def consume_output():
        for line in process.stdout:
            tqdm.write(line.rstrip())
            if len(startup_output) < 20:
                startup_output.append(line.rstrip())
            match = re.search(r"(\d+)/(\d+) Test\s+#\d+:\s+(\S+)", line)
            if match:
                test_progress[:] = [int(match[1]), int(match[2]), match[3]]

    reader = threading.Thread(target=consume_output)
    reader.start()
    with tqdm(
        desc=f"LMMS {args.config}",
        total=initial_total,
        unit=" tests" if args.test else " files",
        ascii=True,
        mininterval=5,
    ) as progress:
        try:
            while process.poll() is None:
                completed, total, current = (
                    test_progress if args.test else build_progress(expected)
                )
                if units is not None:
                    completed, total = source_progress(units)
                progress.total = total
                progress.set_postfix_str(current, refresh=False)
                progress.update(completed - progress.n)
                write_status("running", completed, total, current, args.config)
                time.sleep(1)
            reader.join()
        except KeyboardInterrupt:
            # Console Ctrl+C also reaches the foreground PowerShell/MSBuild tree.
            process.wait()
            reader.join()
            write_status(
                "cancelled",
                progress.n,
                progress.total,
                "Cancelled",
                args.config,
                process.returncode,
            )
            return process.returncode or 130
        completed, total, current = (
            test_progress if args.test else build_progress(expected)
        )
        if units is not None:
            completed, total = source_progress(units)
        progress.total = total
        progress.set_postfix_str(
            "Passed" if process.returncode == 0 else "Failed", refresh=False
        )
        progress.update(completed - progress.n)
    write_status(
        "finished",
        completed,
        total,
        "Passed" if process.returncode == 0 else "Failed",
        args.config,
        process.returncode,
    )
    print(f"Exit code: {process.returncode}; full output: {LOG}")
    if process.returncode:
        print("\n".join(startup_output))
        if LOG.is_file():
            print(
                "\n".join(
                    LOG.read_text(encoding="utf-8-sig", errors="replace").splitlines()[
                        -70:
                    ]
                )
            )
    print("Exiting in 5 seconds.", flush=True)
    time.sleep(5)
    return process.returncode


if __name__ == "__main__":
    sys.exit(main())
