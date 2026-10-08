"""Preserve captured upstream notices and exact locked-wheel declarations.

This offline step never invents a license grant or resolves conflicting notices.
"""
import base64
import hashlib
import json
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
evidence = root / "doc/svs/project/M4"
destination = root / "tools/svs-project/licenses"
destination.mkdir(parents=True, exist_ok=True)
sources = {}
for filename in ("license-github-responses.json", "license-extra-responses.json"):
    for source in json.loads((evidence / filename).read_text(encoding="utf-8-sig")):
        if source.get("Content"):
            sources[source["Package"]] = source
source = json.loads((evidence / "license-wanakana-release-response.json").read_text(encoding="utf-8-sig"))
sources[source["Package"]] = source
missing = {"ko-pron", "loguru", "protobuf-py-ext", "that-depends", "jyutping", "wanakana-python"}
lock = json.loads((root / "doc/svs/project/runtime-lock.json").read_text(encoding="utf-8"))
report = []
for wheel in lock["wheels"]:
    path = root / "build/svs-project-wheels" / wheel["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == wheel["sha256"]
    with zipfile.ZipFile(path) as archive:
        metadata_path = next(p for p in archive.namelist() if p.endswith(".dist-info/METADATA"))
        metadata = archive.read(metadata_path)
        headers = metadata.decode("utf-8").split("\n\n", 1)[0]
        name = next(line[6:] for line in headers.splitlines() if line.startswith("Name: "))
        if name not in missing:
            continue
        directory = destination / metadata_path.split("/")[0]
        directory.mkdir(exist_ok=True)
        (directory / "METADATA").write_bytes(metadata)
        record = {"name": name, "wheel": wheel, "directory": directory.name,
                  "files": [], "status": "upstream-notice-preserved"}
        if name in sources:
            source = sources[name]
            content = base64.b64decode(source["Content"])
            blob = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
            assert blob == source["GitBlob"], "Captured Git blob differs from upstream response"
            (directory / "UPSTREAM-LICENSE").write_bytes(content)
            record["upstream"] = {key: source[key] for key in ("Repo", "Ref", "Spdx", "GitBlob", "Url")}
        if name == "jyutping":
            record["status"] = "metadata-only-upstream-license-file-absent"
            record["note"] = "Locked release declares MIT. Neither wheel, sdist nor upstream repository supplies a LICENSE notice. No grant or copyright notice has been invented."
        if name == "wanakana-python":
            record["status"] = "conflicting-upstream-declarations-preserved"
            record["note"] = "Locked wheel declares MPL-2.0; upstream pre-release commit supplies MIT. Both declarations are retained without relicensing. Distribution review remains MANUAL/PENDING."
            record["declaredLicenseText"] = {"path": "DECLARED-MPL-2.0.txt", "source": "https://raw.githubusercontent.com/spdx/license-list-data/main/text/MPL-2.0.txt"}
            assert (directory / "DECLARED-MPL-2.0.txt").is_file()
        (directory / "PROVENANCE.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        for file in sorted(directory.iterdir()):
            record["files"].append({"path": file.name, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
        report.append(record)
assert {item["name"] for item in report} == missing
(destination / "sources.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
for record in report:
    print(record["name"], record["status"], flush=True)
