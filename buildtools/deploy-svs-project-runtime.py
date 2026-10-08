"""Assemble the fixed Windows conversion runtime inside build/Release.

Invoke in the foreground PowerShell build/log pipeline. The host Python is a
build tool only; the deployed application uses the isolated embedded runtime.
"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPLOY = ROOT / "build/Release/svs-project"
CACHE = ROOT / "build/svs-project-wheels"
VERSION = "3.13.12"


def run(*args):
    subprocess.run(args, check=True)


def main():
    assert sys.version_info[:3] == (3, 13, 12), "Build wheel ABI with CPython 3.13.12"
    run(sys.executable, str(ROOT / "buildtools/svs-project-inventory.py"), "--check")
    DEPLOY.mkdir(parents=True, exist_ok=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / f"python-{VERSION}-embed-amd64.zip"
    if not archive.exists():
        url = f"https://www.python.org/ftp/python/{VERSION}/{archive.name}"
        print(f"Download {url}", flush=True)
        urllib.request.urlretrieve(url, archive)
    frozen_lock = ROOT / "doc/svs/project/runtime-lock.json"
    previous = json.loads(frozen_lock.read_text(encoding="utf-8")) if frozen_lock.exists() else None
    if previous and hashlib.sha256(archive.read_bytes()).hexdigest() != previous["pythonArchiveSha256"]:
        raise ValueError("CPython archive differs from frozen runtime")
    runtime = DEPLOY / "python"
    runtime.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        z.extractall(runtime)
    (runtime / "python313._pth").write_text("python313.zip\n.\nLib/site-packages\n", encoding="utf-8")
    frozen_requirements = ROOT / "doc/svs/project/requirements-win313.txt"
    requirements = CACHE / "requirements.txt"
    if frozen_requirements.exists():
        shutil.copy2(frozen_requirements, requirements)
    else:
        report = json.loads((ROOT / "build/svs-project-dependency-report.json").read_text(encoding="utf-8"))
        frozen_requirements.write_text("\n".join(f"{p['metadata']['name']}=={p['metadata']['version']}" for p in report["install"] if p["metadata"]["name"] != "libresvip") + "\n", encoding="utf-8")
        shutil.copy2(frozen_requirements, requirements)
    run(sys.executable, "-m", "pip", "wheel", "--wheel-dir", str(CACHE), "--no-deps", "-r", str(requirements))
    if not list(CACHE.glob("libresvip-*.whl")):
        run(sys.executable, "-m", "pip", "wheel", "--wheel-dir", str(CACHE), "--no-deps", str(ROOT / "refs/LibreSVIP"))
    if previous:
        actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in CACHE.glob("*.whl")}
        expected = {w["file"]: w["sha256"] for w in previous["wheels"]}
        if actual != expected:
            raise ValueError("Wheel collection differs from frozen runtime; review before deploying")
    site = runtime / "Lib/site-packages"
    run(sys.executable, "-m", "pip", "install", "--no-index", "--no-deps", "--upgrade", "--target", str(site), *map(str, sorted(CACHE.glob("*.whl"))))
    licenses = DEPLOY / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "refs/LibreSVIP/LICENSE", licenses / "LibreSVIP-LICENSE")
    shutil.copy2(runtime / "LICENSE.txt", licenses / "CPython-LICENSE.txt")
    for dist in site.glob("*.dist-info"):
        for source in dist.rglob("*"):
            if source.is_file() and any(word in source.name.lower() for word in ("license", "copying", "notice")):
                target = licenses / dist.name / source.relative_to(dist)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    lock = {"python": VERSION, "pythonArchiveSha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "wheels": [{"file": p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(CACHE.glob("*.whl"))]}
    (ROOT / "doc/svs/project/runtime-lock.json").write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    for source in (ROOT / "doc/svs/project/formats.json", ROOT / "doc/svs/project/export-policy.json", ROOT / "doc/svs/project/runtime-lock.json", ROOT / "tools/svs-project/bridge.py"):
        shutil.copy2(source, DEPLOY / source.name)
    run(str(runtime / "python.exe"), "-c", "import sys; print('Deployed isolated runtime:',sys.version); import libresvip; print('LibreSVIP',libresvip.__version__)")


if __name__ == "__main__":
    main()
