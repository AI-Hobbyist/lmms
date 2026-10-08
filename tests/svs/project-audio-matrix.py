"""Real companion/multi-audio conversion coverage in the deployed runtime."""
import hashlib
import importlib.util
import json
import math
import pathlib
import struct
import sys
import wave

sys.dont_write_bytecode = True

root = pathlib.Path(sys.argv[1]).resolve()
runtime = root / "build/Release/svs-project"
spec = importlib.util.spec_from_file_location("format_matrix", pathlib.Path(__file__).with_name("project-format-matrix.py"))
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)
evidence = root / "doc/svs/project/M4/audio"
evidence.mkdir(parents=True, exist_ok=True)
catalog_case = evidence / "catalog"
catalog_case.mkdir(exist_ok=True)
catalog = matrix.invoke(runtime, {"operation": "listFormats"}, catalog_case, "catalog")
report = []
for format_spec in catalog["formats"]:
    if not format_spec["canExport"] or not format_spec["exportPolicy"]["audio"]:
        # None means unlimited, zero means unsupported; handle explicitly below.
        if not format_spec["canExport"] or format_spec["exportPolicy"]["audio"] == 0:
            continue
    case = evidence / format_spec["id"]
    case.mkdir(exist_ok=True)
    project = matrix.fixture()
    assets = []
    for index in range(2):
        name = f"伴奏_日本語_한국어_{index}.wav"
        source = case / name
        with wave.open(str(source), "wb") as audio:
            audio.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
            audio.writeframes(b"".join(struct.pack("<hh", *(int(8000 * math.sin(i * .03 + index)),)*2) for i in range(8820)))
        project["track_list"].append({"type_": "Instrumental", "title": f"伴奏_{index}",
            "audio_file_path": name, "offset": 240 + index * 480, "volume": .75, "pan": -.25,
            "mute": bool(index), "solo": False})
        assets.append({"name": name, "source": str(source)})
    if format_spec["exportPolicy"]["audio"] == 1:
        assets = assets[:1]
    suffix = format_spec["suffixes"][0]
    options = dict(format_spec["outputDefaults"])
    if "down_sample" in options:
        options["down_sample"] = 0
    exported = matrix.invoke(runtime, {"operation": "exportProject", "formatId": format_spec["id"],
        "path": "多伴奏工程." + suffix, "project": project, "assets": assets, "options": options,
        "selection": {"singingTrack": 0, "audioTrack": 2}, "acceptLosses": True}, case, "export")
    entry = {"id": format_spec["id"], "export": exported.get("status"), "exportError": exported.get("error"),
             "losses": exported.get("losses", []), "fileCount": len(exported.get("files", [])),
             "bridgeSha256": hashlib.sha256((runtime / "bridge.py").read_bytes()).hexdigest()}
    report.append(entry)
    if exported.get("status") == "success" and format_spec["canImport"]:
        primary = next(p for p in exported["files"] if pathlib.Path(p).suffix.lstrip(".") == suffix)
        imported = matrix.invoke(runtime, {"operation": "importProject", "formatId": format_spec["id"],
            "path": primary, "options": format_spec["inputDefaults"]}, case, "import")
        entry["import"] = imported.get("status")
        entry["importError"] = imported.get("error")
        entry["resources"] = imported.get("runtimeResourceChecks", [])
        entry["actualAudioTracks"] = [t for t in imported.get("project", {}).get("track_list", []) if t["type_"] == "Instrumental"]
        entry["expectedAudioTracks"] = [t for t in project["track_list"] if t["type_"] == "Instrumental"][:len(assets)]
        entry["sourceAudioHashes"] = [hashlib.sha256(pathlib.Path(a["source"]).read_bytes()).hexdigest() for a in assets]
        entry["offsetsWithinFrozenTolerance"] = len(entry["actualAudioTracks"]) == len(entry["expectedAudioTracks"]) and all(
            abs(a["offset"] - b["offset"]) <= .5 for a, b in zip(entry["actualAudioTracks"], entry["expectedAudioTracks"]))
        entry["resourceHashesEqual"] = [r["sha256"] for r in entry["resources"]] == entry["sourceAudioHashes"]
    (evidence / "audio-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
failures = [e for e in report if e["export"] != "success" or e.get("import") != "success"
            or len(e.get("actualAudioTracks", [])) != len(e.get("expectedAudioTracks", []))
            or not e.get("offsetsWithinFrozenTolerance")
            or not e.get("resourceHashesEqual")
            or any(not r["exists"] for r in e.get("resources", []))]
print(f"Real audio direction cases: {len(report)-len(failures)}/{len(report)}; exact offset/state/resource measurements in audio-report.json", flush=True)
raise SystemExit(bool(failures))
