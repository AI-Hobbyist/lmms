"""Read native MSBuild compile items and track successfully written object files."""

import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

NAMESPACE = {"msbuild": "http://schemas.microsoft.com/developer/msbuild/2003"}


def applies(element, configuration):
    condition = element.get("Condition", "")
    return not condition or f"'{configuration}|x64'" in condition


def tracked_inputs(path):
    """Native compiler tracking records the source and headers actually read."""
    dependencies = {}
    if not path.is_file():
        return dependencies
    sources = []
    for line in path.read_text(encoding="utf-16", errors="replace").splitlines():
        if line.startswith("^"):
            sources = [os.path.normcase(source) for source in line[1:].split("|")]
            for source in sources:
                dependencies.setdefault(source, [])
        elif line:
            for source in sources:
                dependencies[source].append(line)
    return dependencies


def modification_time(path):
    try:
        return path.stat().st_mtime_ns
    except FileNotFoundError:
        return 0


def compile_items(projects, configuration):
    timestamps = {}
    units = []
    for project_name in sorted(projects):
        project = Path(project_name)
        tree = ET.parse(project)
        intermediate = next(
            item.text
            for item in tree.findall(".//msbuild:IntDir", NAMESPACE)
            if applies(item, configuration)
        )
        intermediate = intermediate.replace("$(Configuration)", configuration)
        directory = project.parent / intermediate
        tracking = directory / f"{project.stem}.tlog" / "CL.read.1.tlog"
        dependencies = tracked_inputs(tracking)
        for item in tree.findall(".//msbuild:ClCompile", NAMESPACE):
            if not item.get("Include") or not applies(item, configuration):
                continue
            excluded = any(
                child.text == "true" and applies(child, configuration)
                for child in item.findall("msbuild:ExcludedFromBuild", NAMESPACE)
            )
            if excluded:
                continue
            source = (project.parent / item.attrib["Include"]).resolve()
            object_name = next(
                (
                    child.text
                    for child in item.findall("msbuild:ObjectFileName", NAMESPACE)
                    if applies(child, configuration)
                ),
                "$(IntDir)",
            )
            object_name = object_name.replace("$(IntDir)", intermediate)
            object_name = object_name.replace("%(Filename)", source.stem)
            if not object_name.lower().endswith(".obj"):
                object_name = str(Path(object_name) / (source.stem + ".obj"))
            output = (project.parent / object_name).resolve()
            before = modification_time(output)
            required = not before or not source.is_file()
            inputs = [str(source)] + dependencies.get(os.path.normcase(str(source)), [])
            for dependency in inputs:
                if dependency not in timestamps:
                    timestamps[dependency] = modification_time(Path(dependency))
                if not timestamps[dependency] or timestamps[dependency] > before:
                    required = True
            if modification_time(project) > before:
                required = True
            units.append(
                {
                    "source": str(source),
                    "object": str(output),
                    "before": before,
                    "required": required,
                }
            )
    return units


def save_plan(path, projects, configuration, first_build=False):
    units = compile_items(projects, configuration)
    if first_build:
        # Recover an empty-build plan when attaching a monitor mid-build.
        for unit in units:
            unit["before"] = 0
            unit["required"] = True
    path.write_text(json.dumps(units), encoding="utf-8")
    return units


def source_progress(units):
    completed = 0
    total = 0
    for unit in units:
        output = Path(unit["object"])
        try:
            output_state = output.stat()
        except FileNotFoundError:
            output_state = None
        written = output_state is not None and output_state.st_mtime_ns > unit["before"]
        # Generated sources or changed compiler commands can add work after planning.
        if unit["required"] or written:
            total += 1
            if written and output_state.st_size > 0:
                completed += 1
    return completed, total
