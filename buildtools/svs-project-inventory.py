"""Freeze the local LibreSVIP source contract without importing user plugins.

This is the M0 source audit, not a claim that converters passed runtime tests.
Run from the LMMS root. Runtime tests consume the frozen manifest in M1/M4.
"""

import ast
import configparser
import hashlib
import json
import pathlib
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "refs/LibreSVIP"
DEST = ROOT / "doc/svs/project"
EXPECTED = set(
    "ace acep aisp ass ccs ds dspx dv json lrc mid mtp musicxml nn ppsf ps_project s5p srt svip svip3 svp svg tlp tlpx tsmsln tssln ufdata ust ustx vfp vog vpr vsq vsqx vshp vspx vvproj vxf xvsq y77".split()
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ref(path, node):
    return f"{path.relative_to(SOURCE).as_posix()}:{node.lineno}"


def option_fields(path, name, seen=None):
    seen = set() if seen is None else seen
    if (path, name) in seen:
        return {}
    seen.add((path, name))
    tree = ast.parse(path.read_text(encoding="utf-8"))
    cls = next((n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == name), None)
    if cls is None:
        return {}
    fields = {}
    for base in cls.bases:
        base_name = ast.unparse(base)
        for node in tree.body:
            if isinstance(node, ast.ImportFrom) and any(a.name == base_name for a in node.names):
                if node.module == "libresvip.model.option_mixins":
                    fields.update(
                        option_fields(SOURCE / "libresvip/model/option_mixins.py", base_name, seen)
                    )
        fields.update(option_fields(path, base_name, seen))
    for node in cls.body:
        if not isinstance(node, ast.AnnAssign) or not isinstance(node.target, ast.Name):
            continue
        default = node.value
        if isinstance(default, ast.Call) and ast.unparse(default.func) == "Field":
            default = next(
                (k.value for k in default.keywords if k.arg == "default"),
                default.args[0] if default.args else None,
            )
        fields[node.target.id] = {
            "type": ast.unparse(node.annotation),
            "defaultExpression": ast.unparse(default) if default else None,
            "source": ref(path, node),
        }
    return fields


def inventory():
    plugins = SOURCE / "libresvip/plugins"
    folders = {p.name for p in plugins.iterdir() if p.is_dir() and not p.name.startswith("_")}
    assert folders == EXPECTED, f"Format baseline changed: {folders ^ EXPECTED}"
    formats = []
    for folder in sorted(plugins.iterdir()):
        if folder.name not in EXPECTED:
            continue
        (metadata,) = folder.glob("*.yapsy-plugin")
        cp = configparser.RawConfigParser()
        cp.read(metadata, encoding="utf-8")
        converter = None
        for path in sorted(folder.glob("*.py")):
            tree = ast.parse(path.read_text(encoding="utf-8"))
            for cls in (n for n in tree.body if isinstance(n, ast.ClassDef)):
                bases = [ast.unparse(b) for b in cls.bases]
                if not any(b.endswith("SVSConverter") for b in bases):
                    continue
                assert converter is None, folder
                assignments = {
                    ast.unparse(t): n.value
                    for n in cls.body
                    if isinstance(n, ast.Assign)
                    for t in n.targets
                }
                methods = {n.name: ref(path, n) for n in cls.body if isinstance(n, ast.FunctionDef)}
                read = not any(b.endswith("WriteOnlyConverterMixin") for b in bases)
                write = not any(b.endswith("ReadOnlyConverterMixin") for b in bases)
                assert not read or "load" in methods, folder
                assert not write or "dump" in methods, folder
                converter = {
                    "id": ast.literal_eval(assignments["_alias_"]),
                    "version": ast.literal_eval(assignments["_version_"]),
                    "class": cls.name,
                    "module": path.relative_to(SOURCE).with_suffix("").as_posix().replace("/", "."),
                    "source": ref(path, cls),
                    "canImport": read,
                    "canExport": write,
                    "directionEvidence": {"bases": bases, **methods},
                }
        assert converter is not None, folder
        options = folder / "options.py"
        converter.update(
            {
                "name": cp.get("Documentation", "Format"),
                "suffixes": [
                    s.strip().removeprefix(".")
                    for s in cp.get("Documentation", "Suffix").split(",")
                ],
                "metadata": metadata.relative_to(SOURCE).as_posix(),
                "inputOptions": (
                    option_fields(options, "InputOptions") if read and options.exists() else {}
                ),
                "outputOptions": (
                    option_fields(options, "OutputOptions") if write and options.exists() else {}
                ),
            }
        )
        sources = {p: p.read_text(encoding="utf-8") for p in folder.glob("*.py")}

        def evidence(needle):
            return [
                f"{p.relative_to(SOURCE).as_posix()}:{i}"
                for p, s in sources.items()
                for i, line in enumerate(s.splitlines(), 1)
                if needle in line
            ]

        converter["capabilityEvidence"] = {
            "audio": evidence("InstrumentalTrack"),
            "pitch": evidence(".pitch"),
            "multiTrackSelection": evidence("track_index"),
            "tempo": evidence("song_tempo_list"),
            "meter": evidence("time_signature_list"),
        }
        converter["capabilities"] = {}
        for direction, keyword, enabled in (
            ("import", "parser", read),
            ("export", "generator", write),
        ):
            if not enabled:
                continue

            def directional(needle):
                return [r for r in evidence(needle) if keyword in r.split(":")[0]]

            converter["capabilities"][direction] = {
                "audio": (
                    "project-model passthrough"
                    if folder.name == "json"
                    else (
                        "implemented"
                        if directional("InstrumentalTrack")
                        else "not implemented; report loss"
                    )
                ),
                "pitch": (
                    "project-model passthrough"
                    if folder.name == "json"
                    else (
                        "implemented"
                        if directional("edited_params.pitch")
                        or directional("edited_params=Params")
                        or directional("pitch=ParamCurve")
                        else "inspect source evidence; sample required before claiming fidelity"
                    )
                ),
                "multiTrack": (
                    "selection/splitting options; require explicit selection"
                    if directional("track_index")
                    else "track list; sample required to confirm cardinality"
                ),
                "audioEvidence": directional("InstrumentalTrack"),
                "pitchEvidence": directional("pitch"),
                "multiTrackEvidence": directional("track_index"),
                "validation": "PENDING: source audit only",
            }
        converter["pitchImportOptions"] = {
            k: v for k, v in converter["inputOptions"].items() if "pitch" in k or "vibrato" in k
        }
        converter["sourcePitchStatus"] = (
            "parser/generator references; verify sample in M4"
            if converter["capabilityEvidence"]["pitch"]
            else "no unified pitch reference; source pitch must be assessed and loss reported"
        )
        converter["dependencies"] = sorted(
            {
                n.module.split(".")[0]
                for p in sources
                for n in ast.walk(ast.parse(sources[p]))
                if isinstance(n, ast.ImportFrom)
                and n.module
                and n.level == 0
                and n.module.split(".")[0] != "libresvip"
            }
            | {
                a.name.split(".")[0]
                for p in sources
                for n in ast.walk(ast.parse(sources[p]))
                if isinstance(n, ast.Import)
                for a in n.names
            }
        )
        converter["samples"] = {
            direction: {
                "status": "PENDING",
                "fixture": f"M4/{folder.name}/{direction}",
                "requirement": (
                    "real format input, pitch when representable"
                    if direction == "import"
                    else "real serialization; read back when canImport; aliases and sidecars"
                ),
            }
            for direction, enabled in (("import", read), ("export", write))
            if enabled
        }
        formats.append(converter)
    assert len(formats) == 40
    assert sum(len(f["suffixes"]) for f in formats) == 45
    files = sorted(
        p
        for p in (SOURCE / "libresvip").rglob("*")
        if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"
    )
    digest = hashlib.sha256()
    for p in files:
        digest.update(p.relative_to(SOURCE).as_posix().encode() + b"\0" + bytes.fromhex(sha(p)))
    project = tomllib.loads((SOURCE / "pyproject.toml").read_text(encoding="utf-8"))
    return {
        "schemaVersion": 1,
        "reference": {
            "commit": subprocess.check_output(
                ["git", "-C", str(SOURCE), "rev-parse", "HEAD"], text=True
            ).strip(),
            "sourceSha256": digest.hexdigest(),
            "uvLockSha256": sha(SOURCE / "uv.lock"),
            "pyprojectSha256": sha(SOURCE / "pyproject.toml"),
            "pythonRange": project["project"]["requires-python"],
            "dependencies": project["project"]["dependencies"],
            "extras": {
                k: v
                for k, v in project["project"]["optional-dependencies"].items()
                if k in {"crypto", "lxml"}
            },
        },
        "formats": formats,
    }


if __name__ == "__main__":
    result = inventory()
    target = DEST / "formats.json"
    if "--check" in sys.argv:
        assert (
            json.loads(target.read_text(encoding="utf-8")) == result
        ), "Frozen source contract changed"
        assert sum(len(f["samples"]) for f in result["formats"]) == 75
        for f in result["formats"]:
            if "import_pitch" in f["inputOptions"]:
                assert f["inputOptions"]["import_pitch"]["defaultExpression"] == "True", f["id"]
        examples = json.loads((DEST / "protocol-examples.json").read_text(encoding="utf-8"))
        for operation in ("list", "import", "export"):
            request = examples[operation + "Request"]
            assert request["protocol"] == 1 and request["requestId"]
        assert examples["importRequest"]["options"]["import_pitch"] is True
    else:
        DEST.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"M0 source audit PASS: {len(result['formats'])} plugins, 45 suffixes, "
        f"{sum(f['canImport'] for f in result['formats'])} import / "
        f"{sum(f['canExport'] for f in result['formats'])} export directions."
    )
