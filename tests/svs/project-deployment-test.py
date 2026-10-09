"""Verify existing Windows deployment artifacts and the frozen isolated runtime."""

import hashlib
import importlib.metadata
import json
import pathlib
import sys
import zipfile

root = pathlib.Path(sys.argv[1]).resolve()
runtime = root / "build/Release/svs-project"
assert pathlib.Path(sys.executable).resolve() == (runtime / "python/python.exe").resolve()
assert sys.version_info[:3] == (3, 13, 12)
report = {"python": sys.version, "dlls": [], "dependencies": [], "licenseDirectories": []}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for artifact in json.loads(
    (root / "doc/svs/project/M4/deployed-dll-paths.json").read_text(encoding="utf-8-sig")
):
    path = pathlib.Path(artifact["Path"])
    assert path.is_file() and path.stat().st_size > 0, str(path)
    report["dlls"].append(
        {
            "target": artifact["Target"],
            "path": str(path),
            "bytes": path.stat().st_size,
            "sha256": sha(path),
        }
    )
for name in (
    "lmms.exe",
    "platforms/qwindows.dll",
    "svs/SVSExample/SVSExample.dll",
    "svs/SVSDiffSinger/SVSDiffSinger.dll",
):
    path = root / "build/Release" / name
    assert path.is_file() and path.stat().st_size, str(path)
    report.setdefault("applicationArtifacts", []).append({"path": str(path), "sha256": sha(path)})
for name in ("formats.json", "export-policy.json", "runtime-lock.json"):
    assert sha(runtime / name) == sha(root / "doc/svs/project" / name), name
assert sha(runtime / "bridge.py") == sha(root / "tools/svs-project/bridge.py")
report["bridgeSha256"] = sha(runtime / "bridge.py")
manifest = json.loads((runtime / "formats.json").read_text(encoding="utf-8"))
site = runtime / "python/Lib/site-packages"
lock = json.loads((runtime / "runtime-lock.json").read_text(encoding="utf-8"))
report["wheelArtifacts"] = []
for wheel in lock["wheels"]:
    path = root / "build/svs-project-wheels" / wheel["file"]
    assert sha(path) == wheel["sha256"], wheel["file"]
    checked = 0
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            if (
                member.is_dir()
                or member.filename.endswith(".dist-info/RECORD")
                or ".data/" in member.filename
            ):
                continue
            deployed = site / member.filename
            assert deployed.is_file(), f"Missing wheel member: {member.filename}"
            assert deployed.read_bytes() == archive.read(
                member
            ), f"Changed wheel member: {member.filename}"
            checked += 1
    report["wheelArtifacts"].append(
        {"file": wheel["file"], "sha256": wheel["sha256"], "checkedMembers": checked}
    )
for line in (
    (root / "doc/svs/project/requirements-win313.txt").read_text(encoding="utf-8").splitlines()
):
    if not line.strip() or line.startswith("#"):
        continue
    name, version = line.split("==")
    installed = importlib.metadata.version(name)
    assert installed == version, (name, installed, version)
    report["dependencies"].append({"name": name, "version": installed})
for name in ("CPython-LICENSE.txt", "LibreSVIP-LICENSE"):
    assert (runtime / "licenses" / name).is_file() and (runtime / "licenses" / name).stat().st_size
report["licenseDirectories"] = [
    p.name for p in sorted((runtime / "licenses").iterdir()) if p.is_dir()
]
report["dependenciesWithoutLicenseDirectory"] = [
    d["name"]
    for d in report["dependencies"]
    if not any(
        pathlib.Path(importlib.metadata.distribution(d["name"])._path).name == name
        for name in report["licenseDirectories"]
    )
]
report["licenseAudit"] = []
supplement = json.loads(
    (root / "tools/svs-project/licenses/sources.json").read_text(encoding="utf-8")
)
report["licenseSupplement"] = supplement
for record in supplement:
    for file in record["files"]:
        deployed = runtime / "licenses" / record["directory"] / file["path"]
        assert (
            deployed.is_file()
            and hashlib.sha256(deployed.read_bytes()).hexdigest() == file["sha256"]
        )
report["licenseReviewPending"] = [
    r["name"] for r in supplement if r["status"] != "upstream-notice-preserved"
]
for name in report["dependenciesWithoutLicenseDirectory"]:
    distribution = importlib.metadata.distribution(name)
    report["licenseAudit"].append(
        {
            "name": name,
            "version": distribution.version,
            "license": distribution.metadata.get("License"),
            "licenseExpression": distribution.metadata.get("License-Expression"),
            "projectUrls": distribution.metadata.get_all("Project-URL"),
            "homepage": distribution.metadata.get("Home-page"),
        }
    )
report["disabledPlugins"] = []
for name in ("sid", "gigplayer"):
    assert not (
        root / "build/Release/plugins" / (name + ".dll")
    ).exists(), f"Disabled plugin still loadable: {name}"
    report["disabledPlugins"].append(
        {
            "name": name,
            "retiredInPlace": (root / "build/Release/plugins" / (name + ".dll.disabled")).exists(),
        }
    )
(root / "doc/svs/project/M4/deployment-report.json").write_text(
    json.dumps(report, indent=2), encoding="utf-8"
)
print(
    f"Verified {len(report['dlls'])} enabled DLL artifacts, native Qt platform, {len(report['wheelArtifacts'])} frozen wheels and {len(report['dependencies'])} exact dependency versions."
)
print("License directory audit:", report["dependenciesWithoutLicenseDirectory"])
print("Upstream notice/declaration review MANUAL/PENDING:", report["licenseReviewPending"])
print(json.dumps(report["licenseAudit"], indent=2, ensure_ascii=False))
