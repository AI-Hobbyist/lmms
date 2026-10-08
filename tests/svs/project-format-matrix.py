"""Run real fixed converters in isolated deployed bridge processes.

This records serialization, parsing and measured semantics separately. A parser
returning notes alone does not prove edited-pitch fidelity. Run through the
foreground PowerShell test/log pipeline; never use the system Python runtime.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import shutil
import subprocess
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True


def fixture():
    tracks = []
    for index in range(2):
        notes = []
        points = [[-192000, -100]]
        for number, key in enumerate((60, 62, 64, 65)):
            start = number * 720 + index * 120
            notes.append({"start_pos": start, "length": 480, "key_number": key,
                          "lyric": ("你", "好", "天", "气")[number],
                          "pronunciation": ("ni", "hao", "tian", "qi")[number]})
            points.append([1920 + start, -100])
            for tick in range(0, 481, 10):
                # Bend plus alternating vibrato, with actual silent gaps.
                cents = round(key * 100 + 35 * math.sin(tick / 480 * math.pi)
                              + 15 * math.sin(tick / 80 * 2 * math.pi))
                points.append([1920 + start + tick, cents])
            points.append([1920 + start + 480, -100])
        points.append([1073741823, -100])
        tracks.append({"type_": "Singing", "title": f"歌声_日本語_한국어_{index + 1}",
                       "mute": bool(index), "solo": not index, "volume": 0.75,
                       "pan": -0.25, "note_list": notes,
                       "edited_params": {"pitch": {"points": points}}})
    return {"song_tempo_list": [{"position": 0, "bpm": 120}, {"position": 1440, "bpm": 150}],
            "time_signature_list": [{"bar_index": 0, "numerator": 4, "denominator": 4}],
            "track_list": tracks}


def invoke(runtime, request, case, label):
    with tempfile.TemporaryDirectory(prefix="task-", dir=case) as directory:
        workspace = pathlib.Path(directory)
        request = {**request, "protocol": 1, "requestId": uuid.uuid4().hex}
        if request["operation"] == "exportProject":
            request["path"] = str(workspace / "output" / request["path"])
        (case / (label + "-request.json")).write_text(json.dumps(request, ensure_ascii=False, indent=2), encoding="utf-8")
        completed = subprocess.run([str(runtime / "python/python.exe"), "-I", str(runtime / "bridge.py"),
                                    "--workspace", str(workspace)], input=json.dumps(request, ensure_ascii=False).encode("utf-8"),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        (case / (label + "-stderr.txt")).write_bytes(completed.stderr)
        (case / (label + "-response.json")).write_bytes(completed.stdout)
        try:
            response = json.loads(completed.stdout)
        except Exception as error:
            response = {"status": "error", "error": {"message": f"Invalid protocol: {error}; exit {completed.returncode}"}}
        if completed.returncode or response.get("status") != "success":
            print(f"{request.get('formatId', 'catalog')} {label}: FAIL {response.get('error')}", flush=True)
            if completed.stderr:
                print(completed.stderr.decode("utf-8", errors="replace"), flush=True)
            return response
        if request["operation"] == "exportProject":
            output = case / label
            output.mkdir(exist_ok=True)
            delivered = []
            for name in response["files"]:
                source = pathlib.Path(name)
                relative = source.relative_to(workspace / "output")
                target = output / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                delivered.append(str(target))
            response["files"] = delivered
        elif request["operation"] == "importProject":
            checks = []
            for resource in response.get("resources", []):
                path = pathlib.Path(resource["path"])
                checks.append({"path": str(path), "exists": path.is_file(), "temporary": resource["temporary"],
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None})
            response["runtimeResourceChecks"] = checks
            (case / (label + "-resource-checks.json")).write_text(json.dumps(checks, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"{request.get('formatId', 'catalog')} {label}: serialized/parsed", flush=True)
        return response


def measurements(expected, actual):
    def seconds_at(tick, tempos):
        elapsed, position, bpm = 0.0, 0, tempos[0]["bpm"]
        for tempo in tempos[1:]:
            if tempo["position"] >= tick:
                break
            elapsed += (tempo["position"] - position) * 60 / (480 * bpm)
            position, bpm = tempo["position"], tempo["bpm"]
        return elapsed + (tick - position) * 60 / (480 * bpm)

    def ticks_at(seconds, tempos):
        position, bpm = 0, tempos[0]["bpm"]
        for tempo in tempos[1:]:
            duration = (tempo["position"] - position) * 60 / (480 * bpm)
            if duration >= seconds:
                break
            seconds -= duration
            position, bpm = tempo["position"], tempo["bpm"]
        return position + seconds * 480 * bpm / 60

    def original_tick(tick):
        return ticks_at(seconds_at(tick, actual["song_tempo_list"]), expected["song_tempo_list"])

    def pitch_at(points, tick):
        # -100 splits independent curves; never interpolate across a silence gap.
        previous = None
        for x, y in points:
            if y == -100:
                previous = None
                continue
            if previous is not None and previous[0] <= tick <= x:
                return y if x == previous[0] else previous[1] + (y - previous[1]) * (tick - previous[0]) / (x - previous[0])
            if x == tick:
                return y
            previous = (x, y)
        return None

    wanted = [t for t in expected["track_list"] if t["type_"] == "Singing"]
    received = [t for t in actual.get("track_list", []) if t.get("type_") == "Singing"]
    result = {"expectedSingingTracks": len(wanted), "actualSingingTracks": len(received), "tracks": []}
    for index, (source, target) in enumerate(zip(wanted, received)):
        notes = target.get("note_list", [])
        errors, normalized_errors = [], []
        for a, b in zip(source["note_list"], notes):
            errors.append({"startTicks": b["start_pos"] - a["start_pos"], "lengthTicks": b["length"] - a["length"],
                           "key": b["key_number"] - a["key_number"], "lyricEqual": b["lyric"] == a["lyric"],
                           "pronunciationEqual": b.get("pronunciation") == a.get("pronunciation")})
            normalized_start, normalized_end = original_tick(b["start_pos"]), original_tick(b["start_pos"] + b["length"])
            normalized_errors.append({"startTicks": normalized_start - a["start_pos"],
                                      "lengthTicks": normalized_end - normalized_start - a["length"],
                                      "key": b["key_number"] - a["key_number"]})
        pitch = target.get("edited_params", {}).get("pitch", {}).get("points", [])
        effective = [p for p in pitch if p[1] != -100 and -192000 < p[0] < 1073741823]
        wanted_pitch = source["edited_params"]["pitch"]["points"]
        normalized_pitch = [[original_tick(x - 1920) + 1920, y] if -192000 < x < 1073741823 else [x, y]
                            for x, y in pitch]
        pitch_errors, missing, samples = [], 0, 0
        for note in source["note_list"]:
            for tick in range(note["start_pos"] + 10, note["start_pos"] + note["length"], 10):
                wanted_value, received_value = pitch_at(wanted_pitch, 1920 + tick), pitch_at(normalized_pitch, 1920 + tick)
                if wanted_value is not None:
                    samples += 1
                    if received_value is None:
                        missing += 1
                    else:
                        pitch_errors.append(abs(received_value - wanted_value))
        gaps = [(a["start_pos"] + a["length"] + b["start_pos"]) / 2
                for a, b in zip(source["note_list"], source["note_list"][1:])
                if a["start_pos"] + a["length"] < b["start_pos"]]
        leaks = sum(pitch_at(normalized_pitch, 1920 + tick) is not None for tick in gaps)
        result["tracks"].append({"index": index, "expectedNotes": len(source["note_list"]), "actualNotes": len(notes),
                                 "noteErrors": errors, "effectivePitchPoints": len(effective),
                                 "actualTimeNormalizedNoteErrors": normalized_errors,
                                 "nonflatPitch": len({p[1] for p in effective}) > 1,
                                 "pitchSamples": samples, "missingPitchSamples": missing,
                                 "maxPitchErrorCents": max(pitch_errors) if pitch_errors else None,
                                 "silenceGapSamples": len(gaps), "silenceGapLeaks": leaks,
                                 "pitchWithinFrozenTolerance": bool(samples) and not missing and not leaks and
                                     len(pitch_errors) == samples and max(pitch_errors) <= 0.5,
                                 "state": {key: {"expected": source.get(key), "actual": target.get(key)}
                                           for key in ("title", "mute", "solo", "volume", "pan")}})
    result["tempos"] = {"expected": expected["song_tempo_list"], "actual": actual.get("song_tempo_list")}
    result["meters"] = {"expected": expected["time_signature_list"], "actual": actual.get("time_signature_list")}
    result["temposEqual"] = expected["song_tempo_list"] == actual.get("song_tempo_list")
    result["notesActualTimeWithinFrozenTolerance"] = len(wanted) == len(received) and all(
        t["expectedNotes"] == t["actualNotes"] and all(abs(e["startTicks"]) <= .5 and abs(e["lengthTicks"]) <= .5 and e["key"] == 0
                                                     for e in t["actualTimeNormalizedNoteErrors"]) for t in result["tracks"])
    # Strict frozen half-LibreSVIP-tick / half-cent limits, not widened per failure.
    result["notesWithinFrozenTolerance"] = len(wanted) == len(received) and all(
        t["expectedNotes"] == t["actualNotes"] and all(abs(e["startTicks"]) <= 0.5 and abs(e["lengthTicks"]) <= 0.5 and e["key"] == 0
                                                     for e in t["noteErrors"]) for t in result["tracks"])
    result["pitchFidelity"] = "PASS" if len(wanted) == len(received) and all(
        t["pitchWithinFrozenTolerance"] for t in result["tracks"]) else "FAIL_OR_DECLARED_FORMAT_LOSS"
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("--formats", nargs="*")
    args = parser.parse_args()
    root = args.root.resolve()
    runtime = root / "build/Release/svs-project"
    if pathlib.Path(sys.executable).resolve() != (runtime / "python/python.exe").resolve():
        raise ValueError("Use the deployed embedded interpreter")
    evidence = root / "doc/svs/project/M4"
    evidence.mkdir(exist_ok=True)
    catalog_case = evidence / "catalog"
    catalog_case.mkdir(exist_ok=True)
    catalog = invoke(runtime, {"operation": "listFormats"}, catalog_case, "catalog")
    if catalog.get("status") != "success":
        return 1
    formats = catalog["formats"]
    manifest = json.loads((root / "doc/svs/project/formats.json").read_text(encoding="utf-8"))
    project = fixture()
    (evidence / "source-project.json").write_text(json.dumps(project, ensure_ascii=False, indent=2), encoding="utf-8")
    report_path = evidence / "direction-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8")) if args.formats and report_path.exists() else {
        "reference": manifest["reference"]["commit"], "denominator": {"plugins": 40, "import": 36, "export": 39}, "formats": []}
    bridge_hash = hashlib.sha256((runtime / "bridge.py").read_bytes()).hexdigest()
    for spec in formats:
        if args.formats and spec["id"] not in args.formats:
            continue
        case = evidence / spec["id"]
        case.mkdir(exist_ok=True)
        entry = {"id": spec["id"], "canImport": spec["canImport"], "canExport": spec["canExport"],
                 "bridgeSha256": bridge_hash, "aliases": []}
        report["formats"] = [f for f in report["formats"] if f["id"] != spec["id"]]
        report["formats"].append(entry)
        if spec["canExport"]:
            for suffix in spec["suffixes"]:
                label = "export-" + suffix
                options = dict(spec["outputDefaults"])
                input_options = dict(spec.get("inputDefaults", {}))
                for configured in (options, input_options):
                    if "down_sample" in configured:
                        configured["down_sample"] = 0  # M0 frozen fidelity fixture setting.
                if spec["id"] == "ust":
                    # Explicit existing user option: Shift-JIS cannot represent
                    # this fixture's Chinese lyrics. Do not alter catalog defaults.
                    options["encoding"] = input_options["encoding"] = "utf-8"
                exported = invoke(runtime, {"operation": "exportProject", "formatId": spec["id"],
                                           "path": "工程_日本語_한국어." + suffix, "project": project,
                                           "options": options, "selection": {"singingTrack": 0}, "acceptLosses": True}, case, label)
                alias = {"suffix": suffix, "export": exported.get("status"), "error": exported.get("error"),
                         "losses": exported.get("losses", []), "warnings": exported.get("warnings", []),
                         "options": options, "inputOptions": input_options, "files": []}
                entry["aliases"].append(alias)
                if exported.get("status") == "success":
                    alias["files"] = [{"name": str(pathlib.Path(p).relative_to(case)), "bytes": pathlib.Path(p).stat().st_size,
                                       "sha256": hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()} for p in exported["files"]]
                    if spec["canImport"]:
                        primary = next((p for p in exported["files"] if pathlib.Path(p).suffix.lstrip(".").lower() == suffix), exported["files"][0])
                        imported = invoke(runtime, {"operation": "importProject", "formatId": spec["id"], "path": primary,
                                                   "options": input_options}, case, "import-" + suffix)
                        alias["import"] = imported.get("status")
                        alias["importError"] = imported.get("error")
                        alias["importLosses"] = imported.get("losses", [])
                        if imported.get("status") == "success":
                            expected = project
                            if spec["exportPolicy"]["singing"] == 1 or (spec["id"] == "ust" and float(options["version"]) < 2):
                                expected = {**project, "track_list": project["track_list"][:1]}
                            alias["measurements"] = measurements(expected, imported["project"])
        else:
            if spec["id"] != "vshp":
                raise ValueError("Unexpected import-only format")
            module_spec = importlib.util.spec_from_file_location("vshp_fixture", pathlib.Path(__file__).with_name("vshp-fixture.py"))
            module = importlib.util.module_from_spec(module_spec)
            module_spec.loader.exec_module(module)
            primary = case / "原生样本_日本語_한국어.vshp"
            module.write_fixture(primary)
            imported = invoke(runtime, {"operation": "importProject", "formatId": spec["id"], "path": str(primary),
                                       "options": spec["inputDefaults"]}, case, "import-vshp")
            entry["import"] = imported.get("status")
            entry["fixture"] = {"provenance": "Generated native VSPD binary from frozen construct specification",
                                "sha256": hashlib.sha256(primary.read_bytes()).hexdigest(), "bytes": primary.stat().st_size}
            if entry["import"] == "success":
                tracks = imported["project"]["track_list"]
                assert len(tracks) == 1 and len(tracks[0]["note_list"]) == 2
                assert [(n["start_pos"], n["length"], n["key_number"]) for n in tracks[0]["note_list"]] == [(0, 480, 60), (720, 480, 62)]
                points = tracks[0]["edited_params"]["pitch"]["points"]
                assert [2400, -100] in points and [2640, -100] in points
                assert len({p[1] for p in points if p[1] != -100}) > 2
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    exports = sum(bool(f["aliases"]) and all(a["export"] == "success" for a in f["aliases"]) for f in report["formats"] if f["canExport"])
    imports = sum((bool(f["aliases"]) and all(a.get("import") == "success" for a in f["aliases"])) or f.get("import") == "success"
                  for f in report["formats"] if f["canImport"])
    print(f"Actual serialization: {exports}/39 formats; actual paired parsing: {imports}/36. See separate numerical pitch measurements; parsing is not fidelity acceptance.", flush=True)
    return 0 if exports == 39 and imports == 36 else 1


if __name__ == "__main__":
    raise SystemExit(main())
