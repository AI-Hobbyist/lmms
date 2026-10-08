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
    policy = json.loads((pathlib.Path(__file__).parent / "export-policy.json").read_text(encoding="utf-8"))
    if policy["schemaVersion"] != 1 or policy["referenceCommit"] != manifest["reference"]["commit"]:
        raise ValueError("Export policy differs from frozen reference")
    if set(policy["formats"]) != {s["id"] for s in manifest["formats"] if s["canExport"]}:
        raise ValueError("Export policy does not cover the complete fixed export catalog")
    from libresvip.extension.base import ReadOnlyConverterMixin, WriteOnlyConverterMixin
    converters, descriptions = {}, []
    for spec in manifest["formats"]:
        converter = getattr(importlib.import_module(spec["module"]), spec["class"])
        read = not issubclass(converter, WriteOnlyConverterMixin)
        write = not issubclass(converter, ReadOnlyConverterMixin)
        if (converter._alias_, converter._version_, read, write) != (spec["id"], spec["version"], spec["canImport"], spec["canExport"]):
            raise ValueError("Converter differs from frozen contract: " + spec["id"])
        item = {k: spec[k] for k in ("id", "name", "version", "suffixes", "canImport", "canExport", "capabilities")}
        if write:
            item["exportPolicy"] = policy["formats"][spec["id"]]
        for prefix, enabled, cls in (("input", read, converter.input_option_cls), ("output", write, converter.output_option_cls)):
            if enabled:
                item[prefix + "Defaults"] = cls().model_dump(mode="json")
                item[prefix + "Schema"] = cls.model_json_schema()
        for key in ("import_pitch", "use_edited_pitch"):
            if read and key in item.get("inputDefaults", {}):
                item["inputDefaults"][key] = True
        if spec["id"] == "svp":
            item["inputDefaults"]["pitch"] = "full"
        converters[spec["id"]] = converter
        descriptions.append(item)
    return converters, descriptions


def prepare_export(project, spec, options, selection, assets):
    """Project only explicit supported tracks; disclose every known omission.

    Runs both for UI inspection and immediately before dump, so callers cannot
    accidentally bypass single-track selection or silently lose accompaniment.
    Does not mutate the validated source model or write any files.
    """
    from libresvip.model.base import InstrumentalTrack, SingingTrack
    from libresvip.utils.audio import audio_track_info

    policy = spec["exportPolicy"]
    effective = {**spec["outputDefaults"], **options}
    if not project.song_tempo_list or not project.time_signature_list:
        raise ValueError("导出快照缺少全局速度或拍号")
    if not isinstance(selection, dict):
        raise ValueError("Export selection must be an object")
    if not isinstance(assets, list) or any(not isinstance(a, dict) for a in assets):
        raise ValueError("Invalid companion audio list")
    asset_paths = {a.get("name"): a.get("source") for a in assets}
    losses = []

    def loss(track, field, reason):
        losses.append({"formatId": spec["id"], "track": track.title or "未命名轨道",
                       "field": field, "reason": reason})

    def chosen(track_type, limit, key):
        candidates = [i for i, t in enumerate(project.track_list) if isinstance(t, track_type)]
        if limit == 0:
            return set()
        if limit is None:
            return set(candidates)
        index = selection.get(key)
        if index is None and key == "singingTrack" and effective.get("track_index", -1) >= 0:
            index = effective["track_index"]
        if index is None and len(candidates) <= limit:
            return set(candidates)
        if type(index) is not int or index not in candidates:
            raise ValueError(f"格式 {spec['id']} 只支持一条{'歌声' if key == 'singingTrack' else '伴奏'}轨，必须显式选择有效轨道。")
        return {index}

    singing_limit = policy["singing"]
    if spec["id"] == "ust" and float(effective.get("version", 2)) < 2:
        singing_limit = policy["legacySinging"]
    keep_singing = chosen(SingingTrack, singing_limit, "singingTrack")
    keep_audio = chosen(InstrumentalTrack, policy["audio"], "audioTrack")
    tracks = []
    for index, track in enumerate(project.track_list):
        singing = isinstance(track, SingingTrack)
        if index not in (keep_singing if singing else keep_audio):
            loss(track, "track" if singing else "audio", "目标格式不支持此轨道，或此次未选择该轨道；将省略。")
            continue
        if not singing:
            source = pathlib.Path(asset_paths.get(track.audio_file_path, track.audio_file_path))
            if not source.is_absolute() or not source.is_file():
                raise ValueError(f"伴奏 {track.title} 的资源不可用：{source}")
            info = audio_track_info(source, only_wav=bool(policy.get("audioWavOnly") or policy.get("audioWav44100Pcm16")))
            if info is None:
                raise ValueError(f"伴奏 {track.title} 的音频元数据不可读，或目标格式要求 WAV：{source}")
            if policy.get("audioWav44100Pcm16") and (info.sample_rate != 44100 or info.bit_depth != 16):
                raise ValueError(f"格式 {spec['id']} 的伴奏要求 44100 Hz / 16 bit PCM WAV：{track.title}")
            if policy.get("audioExtensions") and source.suffix.lstrip(".").lower() not in policy["audioExtensions"]:
                raise ValueError(f"格式 {spec['id']} 不支持伴奏扩展名：{source.suffix}")
        kind = policy["kind"]
        if kind != "project":
            descriptions = {"lyrics": "仅导出歌词和时间，不包含可编辑音符、音高曲线及轨道混音状态。",
                            "notation": "导出乐谱音符；原始编辑音高曲线及轨道混音状态不保留。",
                            "graphic": "仅导出可视化图形，不是可编辑歌声工程。",
                            "parameters": "导出合成参数文件，不是完整可编辑工程；轨道混音状态不保留。"}
            loss(track, "project", descriptions[kind])
        fields = policy.get("fieldsLost", []) + (policy.get("singingFieldsLost", []) if singing else [])
        for field in fields:
            value = getattr(track, field)
            if field == "title" or value != {"volume": 1.0, "pan": 0.0, "mute": False, "solo": False}.get(field):
                loss(track, field, "固定版本转换器没有写入该轨道字段；将使用目标格式默认值。")
        grid = policy.get("noteTickGrid", 1)
        if singing and grid > 1 and any(n.start_pos % grid or n.length % grid for n in track.note_list):
            loss(track, "noteTime", f"目标格式音符时间按 {grid} LibreSVIP tick 网格量化。")
        tracks.append(track)
    if not tracks:
        raise ValueError("所选格式没有可导出的轨道")
    if policy.get("firstTempoOnly") and len({t.bpm for t in project.song_tempo_list}) > 1:
        losses.append({"formatId": spec["id"], "track": "全局", "field": "tempo",
                       "reason": "目标格式只记录一个 BPM；原工程的完整变速事件不能保留。"})
    if policy.get("maxBars"):
        limit = project.time_signature_list[0].bar_length() * policy["maxBars"]
        if any(n.end_pos > limit for t in tracks if isinstance(t, SingingTrack) for n in t.note_list):
            raise ValueError(f"格式 {spec['id']} 最多 {policy['maxBars']} 小节；拒绝静默截断音符。")
    if policy.get("sharedAudioMix") and len(keep_audio) > 1:
        audio = [project.track_list[i] for i in sorted(keep_audio)]
        for track in audio[1:]:
            if any(getattr(track, f) != getattr(audio[0], f) for f in ("volume", "mute", "solo")):
                loss(track, "audioMix", "目标格式全部伴奏共享首条伴奏的音量、静音及独奏状态。")
    if singing_limit == 1 and keep_singing and "track_index" in effective:
        effective["track_index"] = next(i for i, t in enumerate(tracks) if isinstance(t, SingingTrack))
    projected = project.model_copy(update={"track_list": tracks})
    return projected, effective, losses


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
    from libresvip.model.base import InstrumentalTrack, SingingTrack, Project
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
            for key in ("import_pitch", "use_edited_pitch"):
                if key in effective:
                    effective[key] = True
            if format_id == "svp":
                effective["pitch"] = "full"
            project = converter.load(source, effective)
            resources = []
            losses = []
            for index, track in enumerate(project.track_list):
                if isinstance(track, SingingTrack) and track.note_list and not any(
                    point.y != -100 and -192000 < point.x < 1073741823
                    for point in track.edited_params.pitch.points.root
                ):
                    losses.append({
                        "formatId": format_id, "track": track.title or str(index + 1),
                        "field": "pitch",
                        "reason": "解析器未提供原工程的有效音高曲线；如源工程含弯音或颤音，无法确认其已保留。仅音符导入不计为音高保真。",
                    })
                if isinstance(track, InstrumentalTrack):
                    audio = pathlib.Path(track.audio_file_path)
                    if not audio.is_absolute():
                        extracted = source.parent / audio
                        audio = extracted if extracted.is_file() else path.parent / audio
                    elif not audio.is_file() and contained(audio, workspace):
                        audio = path.parent / audio.relative_to(source.parent)
                    track.audio_file_path = str(audio.resolve())
                    resources.append({"path": track.audio_file_path, "temporary": contained(audio, workspace), "exists": audio.is_file()})
            result = {"project": project.model_dump(mode="json", by_alias=False), "resources": resources, "losses": losses}
        elif operation in {"inspectExport", "exportProject"}:
            if not spec["canExport"]:
                raise ValueError("Format is import-only")
            if operation == "exportProject" and not contained(path, workspace):
                raise ValueError("Export path must be staged in the task directory")
            project = Project.model_validate(request["project"])
            project, effective, losses = prepare_export(project, spec, options, request.get("selection", {}), request.get("assets", []))
            if operation == "inspectExport":
                return {"project": project.model_dump(mode="json", by_alias=False), "options": effective, "losses": losses, "warnings": []}
            if losses and request.get("acceptLosses") is not True:
                raise ValueError("有损导出尚未确认；请先检查 inspectExport 的逐项损失。")
            path.parent.mkdir(parents=True, exist_ok=True)
            # Companions are referenced by basename so their links survive moving
            # the complete output group from the isolated task to its destination.
            assets = request.get("assets", [])
            if not isinstance(assets, list) or len(assets) > 10000:
                raise ValueError("Invalid companion audio list")
            total = 0
            names = set()
            for asset in assets:
                name = asset.get("name", "")
                if (not isinstance(name, str) or not name or name in {".", ".."}
                    or pathlib.Path(name).name != name
                    or pathlib.PureWindowsPath(name).name != name or ":" in name
                    or name.casefold() == path.name.casefold() or name.casefold() in names):
                    raise ValueError("Invalid companion audio filename")
                source = pathlib.Path(asset.get("source", ""))
                if not source.is_absolute() or not source.is_file() or source.stat().st_size > MAX_INPUT:
                    raise ValueError("Companion audio missing or exceeds size limit")
                total += source.stat().st_size
                if total > MAX_UNPACK:
                    raise ValueError("Companion audio group exceeds size limit")
                shutil.copy2(source, path.parent / name)
                names.add(name.casefold())
            os.chdir(path.parent)
            converter.dump(path, project, effective)
            files = sorted(p for p in path.parent.rglob("*") if p.is_file())
            if not files or not all(contained(p, workspace) and not p.is_symlink() for p in files):
                raise ValueError("Converter produced no valid output files")
            if not any(p.parent != path.parent or p.name.casefold() not in names for p in files):
                raise ValueError("Converter produced companions but no project output")
            for file in files:
                if file.stat().st_size > MAX_UNPACK:
                    raise ValueError("Output exceeds size limit")
            result = {"files": [str(p.resolve()) for p in files], "losses": losses}
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
