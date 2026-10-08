"""Single-request LibreSVIP worker. stdout is the versioned protocol only."""
import argparse
import contextlib
import importlib
import json
import os
import pathlib
import shutil
import stat
import sys
import traceback
import zipfile

MAX_JSON = 64 * 1024 * 1024
MAX_INPUT = 256 * 1024 * 1024
MAX_UNPACK = 1024 * 1024 * 1024
sys.dont_write_bytecode = True


def contained(path, root):
    return pathlib.Path(path).resolve().is_relative_to(root.resolve())


def guard_writes(root):
    """Converters can write only their task; never extract over source/user files."""
    def audit(event, args):
        candidates = []
        if event == "open" and not isinstance(args[0], int):
            mode, flags = args[1:3]
            if (mode and any(c in mode for c in "wax+")) or (flags and flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC)):
                candidates = [args[0]]
        elif event in {"os.remove", "os.rmdir", "os.mkdir", "os.chmod", "os.truncate"}:
            candidates = [args[0]]
        elif event in {"os.rename", "os.link", "os.symlink"}:
            candidates = list(args[:2])
        elif event in {"subprocess.Popen", "os.system", "socket.connect"}:
            raise PermissionError("Converter cannot launch processes or access the network")
        if any(not contained(p, root) for p in candidates):
            raise PermissionError("Converter attempted to write outside its task directory")
    sys.addaudithook(audit)


def check_archive(path):
    if not zipfile.is_zipfile(path):
        return
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > 10000 or sum(i.file_size for i in entries) > MAX_UNPACK:
            raise ValueError("Archive exceeds unpack limits")
        for entry in entries:
            name = entry.filename.replace("\\", "/")
            parts = pathlib.PurePosixPath(name).parts
            if name.startswith("/") or ".." in parts or ":" in name or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Unsafe archive resource path: " + name)


def fixed_catalog():
    manifest = json.loads((pathlib.Path(__file__).parent / "formats.json").read_text(encoding="utf-8"))
    from libresvip.extension.base import ReadOnlyConverterMixin, WriteOnlyConverterMixin
    converters, descriptions = {}, []
    for spec in manifest["formats"]:
        converter = getattr(importlib.import_module(spec["module"]), spec["class"])
        read = not issubclass(converter, WriteOnlyConverterMixin)
        write = not issubclass(converter, ReadOnlyConverterMixin)
        if (converter._alias_, converter._version_, read, write) != (spec["id"], spec["version"], spec["canImport"], spec["canExport"]):
            raise ValueError("Converter differs from frozen contract: " + spec["id"])
        item = {k: spec[k] for k in ("id", "name", "version", "suffixes", "canImport", "canExport", "capabilities")}
        for prefix, enabled, cls in (("input", read, converter.input_option_cls), ("output", write, converter.output_option_cls)):
            if enabled:
                item[prefix + "Defaults"] = cls().model_dump(mode="json")
                item[prefix + "Schema"] = cls.model_json_schema()
        if read and "import_pitch" in item.get("inputDefaults", {}):
            item["inputDefaults"]["import_pitch"] = True
        if spec["id"] == "svp":
            item["inputDefaults"]["pitch"] = "full"
        converters[spec["id"]] = converter
        descriptions.append(item)
    return converters, descriptions


def process(request, workspace):
    if request.get("protocol") != 1 or not isinstance(request.get("requestId"), str) or not request["requestId"]:
        raise ValueError("Unsupported protocol or missing request ID")
    converters, catalog = fixed_catalog()
    operation = request.get("operation")
    if operation == "listFormats":
        return {"formats": catalog}
    format_id = request.get("formatId")
    if format_id not in converters:
        raise ValueError("Unknown format ID")
    spec = next(s for s in catalog if s["id"] == format_id)
    converter = converters[format_id]
    options = request.get("options", {})
    if not isinstance(options, dict):
        raise ValueError("Options must be an object")
    path = pathlib.Path(request.get("path", ""))
    if not path.is_absolute():
        raise ValueError("Project path must be absolute")
    from libresvip.core.warning_types import CatchWarnings
    from libresvip.model.base import InstrumentalTrack, Project
    with CatchWarnings() as caught:
        if operation == "importProject":
            if not spec["canImport"]:
                raise ValueError("Format is export-only")
            if not path.is_file() or path.stat().st_size > MAX_INPUT:
                raise ValueError("Source missing or exceeds input size limit")
            check_archive(path)
            source = workspace / "source" / path.name
            source.parent.mkdir()
            shutil.copy2(path, source)
            effective = {**spec["inputDefaults"], **options}
            if "import_pitch" in effective:
                effective["import_pitch"] = True
            project = converter.load(source, effective)
            resources = []
            for track in project.track_list:
                if isinstance(track, InstrumentalTrack):
                    audio = pathlib.Path(track.audio_file_path)
                    if not audio.is_absolute():
                        extracted = source.parent / audio
                        audio = extracted if extracted.is_file() else path.parent / audio
                    elif not audio.is_file() and contained(audio, workspace):
                        audio = path.parent / audio.relative_to(source.parent)
                    track.audio_file_path = str(audio.resolve())
                    resources.append({"path": track.audio_file_path, "temporary": contained(audio, workspace), "exists": audio.is_file()})
            result = {"project": project.model_dump(mode="json", by_alias=False), "resources": resources}
        elif operation == "exportProject":
            if not spec["canExport"]:
                raise ValueError("Format is import-only")
            if not contained(path, workspace):
                raise ValueError("Export path must be staged in the task directory")
            project = Project.model_validate(request["project"])
            path.parent.mkdir(parents=True, exist_ok=True)
            converter.dump(path, project, {**spec["outputDefaults"], **options})
            files = sorted(p for p in path.parent.rglob("*") if p.is_file())
            if not files or not all(contained(p, workspace) and not p.is_symlink() for p in files):
                raise ValueError("Converter produced no valid output files")
            for file in files:
                if file.stat().st_size > MAX_UNPACK:
                    raise ValueError("Output exceeds size limit")
            result = {"files": [str(p.resolve()) for p in files]}
        else:
            raise ValueError("Unknown operation")
    result["warnings"] = [{"formatId": format_id, "message": message} for message in caught.output.splitlines()]
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True, type=pathlib.Path)
    args = parser.parse_args()
    workspace = args.workspace.resolve(strict=True)
    if not workspace.is_dir() or any(workspace.iterdir()):
        parser.error("workspace must be an existing empty task directory")
    os.chdir(workspace)
    os.environ["TMP"] = os.environ["TEMP"] = str(workspace)
    guard_writes(workspace)
    request = {}
    response = {"protocol": 1, "dataVersion": 1, "status": "error", "warnings": [], "losses": []}
    try:
        raw = sys.stdin.buffer.read(MAX_JSON + 1)
        if len(raw) > MAX_JSON:
            raise ValueError("Request exceeds JSON limit")
        request = json.loads(raw)
        if not isinstance(request, dict):
            request = {}
            raise ValueError("Request must be an object")
        # Third-party diagnostics never contaminate JSON stdout.
        with contextlib.redirect_stdout(sys.stderr):
            response.update(process(request, workspace))
        response["status"] = "success"
    except Exception as exc:
        traceback.print_exc(file=sys.stderr)
        response["error"] = {"code": "conversionFailed", "message": str(exc), "detail": type(exc).__name__}
    response.update({"requestId": request.get("requestId", ""), "formatId": request.get("formatId", "")})
    output = json.dumps(response, ensure_ascii=False, allow_nan=False).encode("utf-8")
    if len(output) > MAX_JSON:
        response = {"protocol": 1, "dataVersion": 1, "requestId": request.get("requestId", ""), "formatId": request.get("formatId", ""), "status": "error", "warnings": [], "losses": [], "error": {"code": "resultTooLarge", "message": "Result exceeds JSON limit"}}
        output = json.dumps(response).encode("utf-8")
    sys.stdout.buffer.write(output)
    sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
