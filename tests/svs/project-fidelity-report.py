"""Audit real matrix results against losses disclosed before serialization.

Strict fidelity failures stay failures; an explicit loss is a separate result.
This never changes numeric tolerances or infers success from a nonflat curve.
"""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
evidence = root / "doc/svs/project/M4"
direction = json.loads((evidence / "direction-report.json").read_text(encoding="utf-8"))
audio = json.loads((evidence / "audio/audio-report.json").read_text(encoding="utf-8"))
current_hash = hashlib.sha256((root / "tools/svs-project/bridge.py").read_bytes()).hexdigest()
failures = []
rows = []


def require(condition, message):
    if not condition:
        failures.append(message)


def fields_for(losses, title):
    return {loss["field"] for loss in losses if loss.get("track") in (title, "全局")}


def equal(a, b):
    if type(a) in (float, int) and type(b) in (float, int):
        return abs(a - b) <= 1e-6
    return a == b


def meter_changes(events):
    # A repeated identical signature has no timing effect. The native mapper
    # accepts it (prepareImport checks values, not the number of events).
    # Retain every actual change and its bar index; never hide a shifted change.
    changes = []
    for event in events or []:
        if not changes or any(event[key] != changes[-1][key] for key in ("numerator", "denominator")):
            changes.append(event)
    return changes


require(len(direction["formats"]) == 40, "Frozen 40-format denominator changed")
for entry in direction["formats"]:
    name = entry["id"]
    require(entry["bridgeSha256"] == current_hash, name + ": stale matrix bridge")
    if not entry["canExport"]:
        require(entry.get("import") == "success", name + ": native input failed")
        rows.append({"id": name, "serialization": "N/A", "parsing": "PASS", "pitch": "native fixture assertions PASS"})
        continue
    for alias in entry["aliases"]:
        label = name + "/" + alias["suffix"]
        require(alias["export"] == "success" and bool(alias["files"]), label + ": serialization failed")
        if not entry["canImport"]:
            require(any(loss["field"] == "project" for loss in alias["losses"]), label + ": output kind undisclosed")
            rows.append({"id": label, "serialization": "PASS", "parsing": "N/A", "pitch": "N/A (declared output kind)"})
            continue
        require(alias.get("import") == "success", label + ": actual readback failed")
        measured = alias.get("measurements", {})
        losses = alias["losses"]
        global_fields = fields_for(losses, "全局")
        if not measured.get("temposEqual"):
            require("tempo" in global_fields, label + ": tempo loss undisclosed")
        measured["metersSemanticallyEqual"] = meter_changes(measured.get("meters", {}).get("expected")) == meter_changes(measured.get("meters", {}).get("actual"))
        if not measured["metersSemanticallyEqual"]:
            require("meter" in global_fields, label + ": meter loss undisclosed")
        require(measured.get("expectedSingingTracks") == measured.get("actualSingingTracks"), label + ": selected tracks lost")
        declared = []
        for track in measured.get("tracks", []):
            title = track["state"]["title"]["expected"]
            fields = fields_for(losses, title)
            declared.extend(sorted(fields))
            require(track["expectedNotes"] == track["actualNotes"], label + ": notes lost")
            for error in track["noteErrors"]:
                require(error["key"] == 0, label + ": note key changed")
                require(error["lyricEqual"] or bool(fields & {"lyric", "project"}), label + ": lyric loss undisclosed")
                require(error["pronunciationEqual"] or bool(fields & {"pronunciation", "project"}), label + ": pronunciation loss undisclosed")
            for error in track["actualTimeNormalizedNoteErrors"]:
                if abs(error["startTicks"]) > .5 or abs(error["lengthTicks"]) > .5:
                    require(bool(fields & {"tempo", "noteTime", "project"}), label + ": note time loss undisclosed")
            for field, state in track["state"].items():
                require(equal(state["expected"], state["actual"]) or field in fields or "project" in fields,
                        label + ": track " + field + " loss undisclosed")
            if not track["pitchWithinFrozenTolerance"]:
                require(bool(fields & {"pitch", "project"}), label + ": pitch fidelity failure undisclosed")
        rows.append({"id": label, "serialization": "PASS", "parsing": "PASS",
                     "pitch": "PASS" if measured.get("pitchFidelity") == "PASS" else "LOSS (full fidelity FAIL)",
                     "declaredFields": sorted(set(declared)), "measurements": measured})

require(len(audio) == 23, "Frozen accompaniment denominator changed")
for entry in audio:
    label = entry["id"] + "/audio"
    require(entry["bridgeSha256"] == current_hash, label + ": stale matrix bridge")
    require(entry.get("resourceHashesEqual") and entry.get("offsetsWithinFrozenTolerance"), label + ": resources/offsets failed")
    require(entry["export"] == "success" and entry.get("import") == "success", label + ": direction failed")
    for wanted, actual in zip(entry["expectedAudioTracks"], entry["actualAudioTracks"]):
        fields = fields_for(entry["losses"], wanted["title"])
        for field in ("title", "volume", "pan", "mute", "solo"):
            covered = field in fields or (field in ("volume", "mute", "solo") and "audioMix" in fields)
            require(equal(wanted[field], actual[field]) or covered, label + ": " + field + " loss undisclosed")

result = {"bridgeSha256": current_hash, "denominator": direction["denominator"], "audioCases": len(audio),
          "rows": rows, "unexplainedFailures": failures,
          "result": "PASS_WITH_DECLARED_LOSSES" if not failures else "FAIL"}
(evidence / "fidelity-validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
lines = ["# M4 real-format validation", "", "Numeric limits remain 0.5 LibreSVIP tick / 0.5 cent. LOSS is not full pitch fidelity PASS.", "",
         "| Format / alias | Real output | Real input | Original edited pitch |", "| --- | --- | --- | --- |"]
lines.extend(f"| {row['id']} | {row['serialization']} | {row['parsing']} | {row['pitch']} |" for row in rows)
lines.extend(["", "Detailed measured errors, declared fields and unexplained failures: `M4/fidelity-validation.json`."])
(root / "doc/svs/project/M4-validation.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
for failure in failures:
    print("FAIL:", failure, flush=True)
print(result["result"], ": all direction, alias, audio and semantic-loss checks; full pitch results remain separate.", flush=True)
raise SystemExit(bool(failures))
