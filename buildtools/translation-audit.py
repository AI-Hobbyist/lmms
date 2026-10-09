"""Audit only LMMS's four planned UI languages, using a fresh lupdate TS.

Extraction is intentionally external so PowerShell can stream all Qt output.
No network, catalog mutation, GUI launch, or third-party dependency is used.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import html
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = ("zh_CN", "ja", "en", "ko")
PLACEHOLDER = re.compile(r"%L?[1-9][0-9]*|%n|%Ln")


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=ROOT, text=True, encoding="utf-8"
    ).strip()


def key(context, message):
    return (
        context,
        message.findtext("source", ""),
        message.findtext("comment", ""),
        message.get("numerus", "no"),
    )


def catalog(path):
    result = {}
    for context in ET.parse(path).getroot().findall("context"):
        for message in context.findall("message"):
            translation = message.find("translation")
            if translation is not None and translation.get("type") in (
                "obsolete", "vanished"
            ):
                continue
            result[key(context.findtext("name", ""), message)] = message
    return result


def write_report(path, report):
    """Keep each complete key/decision on one line for useful review diffs."""
    fields = []
    for name, value in report.items():
        label = json.dumps(name)
        if isinstance(value, list):
            records = ",\n".join(
                "    " + json.dumps(row, ensure_ascii=False) for row in value
            )
            fields.append(f"  {label}: [\n{records}\n  ]")
        else:
            fields.append(f"  {label}: " + json.dumps(value, ensure_ascii=False))
    path.write_text("{\n" + ",\n".join(fields) + "\n}\n", encoding="utf-8")


def locations(message, extraction):
    result = []
    for location in message.findall("location"):
        path = Path(location.get("filename", ""))
        if not path.is_absolute():
            path = extraction.parent / path
        try:
            filename = path.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            filename = path.as_posix()
        result.append({"file": filename, "line": location.get("line", "")})
    return result


def stage_for(paths, source):
    names = " ".join(paths).lower()
    if "svc" in names or re.search(r"\bSVC\b", source):
        return "M3"
    if any(token in names for token in (
        "svs", "notelabeldisplay", "jianpu", "diffsinger"
    )) or re.search(r"\bSVS\b", source) or "简谱" in source or source.startswith(("Numbered notation", "Default C4; other pitches")):
        return "M2"
    if "vst" in names:
        return "M4"
    if any(path.startswith("plugins/") for path in paths):
        return "M5"
    return "M4"


def status(language, translation_key, message):
    source = translation_key[1]
    english_fallback = language == "en" and not re.search(r"[\u3400-\u9fff]", source)
    if message is None:
        return "source-unlisted" if english_fallback else "missing"
    if english_fallback:
        return "source"
    translation = message.find("translation")
    if translation is None:
        return "empty"
    forms = translation.findall("numerusform")
    texts = ["".join(form.itertext()) for form in forms] if forms else [
        "".join(translation.itertext())
    ]
    if not texts or any(not text.strip() for text in texts):
        return "empty"
    if translation.get("type") == "unfinished":
        return "unfinished"
    if translation_key[3] == "yes" and not forms:
        return "plural-error"
    if forms and len(forms) != 1:
        # All four target languages use one Qt plural form.
        return "plural-error"
    if any(Counter(PLACEHOLDER.findall(text)) != Counter(PLACEHOLDER.findall(source))
           for text in texts):
        return "placeholder-error"
    return "same-source" if all(text == source for text in texts) else "translated"


def audit(extraction, output):
    current = catalog(extraction)
    languages = {language: catalog(ROOT / "data/locale" / f"{language}.ts")
                 for language in LANGUAGES}
    rows = []
    summary = {language: Counter() for language in LANGUAGES}
    for translation_key, message in sorted(current.items()):
        source_locations = locations(message, extraction)
        states = {language: status(language, translation_key,
                                  languages[language].get(translation_key))
                  for language in LANGUAGES}
        for language, state in states.items():
            summary[language][state] += 1
        rows.append({
            "id": hashlib.sha256(json.dumps(translation_key, ensure_ascii=False)
                                 .encode()).hexdigest()[:16],
            "key": list(translation_key),
            "stage": ("M2" if translation_key[0] == "NativeSVS" else
                      "M3" if translation_key[0] == "NativeRVC" else
                      stage_for([item["file"] for item in source_locations],
                                translation_key[1])),
            "locations": source_locations,
            "status": states,
        })
    groups = defaultdict(list)
    for row in rows:
        if row["stage"] == "M5":
            paths = [item["file"] for item in row["locations"]
                     if item["file"].startswith("plugins/")]
            plugin = sorted(paths)[0].split("/")[1] if paths else row["key"][0]
            groups[plugin].append(row)
    batch = 1
    size = 0
    for plugin, members in sorted(groups.items()):
        if size and size + len(members) > 200:
            batch += 1
            size = 0
        for row in members:
            row["batch"] = f"M5-{batch:02d}"
            row["plugin"] = plugin
        size += len(members)
    report = {
        "source_commit": git("rev-parse", "HEAD"),
        "working_diff_sha256": hashlib.sha256(
            git("diff", "HEAD", "--", "src", "include", "plugins").encode()
        ).hexdigest(),
        "extraction": extraction.relative_to(ROOT).as_posix(),
        "input_count": len((output / "sources.txt").read_text(encoding="utf-8-sig")
                           .splitlines()),
        "key_count": len(rows),
        "summary": summary,
        "keys": rows,
    }
    output.mkdir(parents=True, exist_ok=True)
    write_report(output / "current-audit.json", report)
    print(json.dumps({name: value for name, value in report.items() if name != "keys"},
                     ensure_ascii=False, indent=2))


def worklist(output):
    report = json.loads((output / "current-audit.json").read_text(encoding="utf-8"))
    document = (ROOT / "词条缺失表.md").read_text(encoding="utf-8")
    appendix = document.split("## 新增硬编码文本候选附录", 1)[1].split("## 后续", 1)[0]
    candidates = []
    for line in appendix.splitlines():
        if not line.startswith("| ["):
            continue
        columns = line.split(" | ")
        location = re.search(r"\[([^]]+)\]", columns[0]).group(1)
        filename, number = location.rsplit(":", 1)
        source = html.unescape(columns[1]).replace("<br>", "\n")
        candidates.append({
            "location": location,
            "source": source,
            "initial_class": columns[2].strip(" |"),
            "classification": "待确认",
            "stage": stage_for([filename], source),
            "investigation": f"M1: CodeGraph 查询 {filename}，从第 {number} 行所在符号追踪到显示/日志调用方；确认后接入或按理由关闭。",
            "state": "OPEN",
        })
    declarations = []
    appendix = document.split("## 原生示例插件 JSON 显示词条", 1)[1].split("## 新增硬编码", 1)[0]
    for line in appendix.splitlines():
        if line.startswith("| ["):
            columns = line.split(" | ")
            declarations.append({
                "file": re.search(r"\[([^]]+)\]", columns[0]).group(1),
                "json_path": columns[1],
                "source": columns[2],
                "classification": "用户可见固定文案",
                "entry_stage": "M1",
                "translation_stage": "M2",
                "state": "OPEN",
            })
    reviews = []
    retained = {
        "LMMS", "PortAudio", "PulseAudio", "JACK", "ASIO", "MIDI", "VST", "VST2", "VST3",
        "Hz", "kHz", "dB", "ms", "s", "BPM", "CPU", "LFO", "ADSR", "RVC", "DiffSinger",
        "C", "D", "E", "F", "G", "A", "B", "C#", "D#", "F#", "G#", "A#",
    }
    for row in report["keys"]:
        for language, state in row["status"].items():
            if state != "same-source":
                continue
            source = row["key"][1]
            keep = source in retained or source.startswith("https://")
            reviews.append({
                "id": row["id"], "key": row["key"], "language": language,
                "stage": row.get("batch", row["stage"]),
                "decision": "retain" if keep else "translate-or-review",
                "reason": "品牌、标准缩写、单位或音名" if keep else "所属阶段逐项核对语义；通用操作与说明补译，其他专有名称须记录保留理由。",
                "state": "CLOSED" if keep else "OPEN",
            })
    ledger = {
        "source_commit": report["source_commit"],
        "hardcoded_candidates": candidates,
        "declarations": declarations,
        "same_source_reviews": reviews,
    }
    write_report(output / "review-ledger.json", ledger)
    print(f"Hardcoded candidates: {len(candidates)}; fixed declaration fields: {len(declarations)}")
    print(f"Same-source keys: {len({row['id'] for row in reviews})}; language reviews: {len(reviews)}")
    print("Stage/batch keys:")
    print(json.dumps(Counter(row.get("batch", row["stage"]) for row in report["keys"]), indent=2))


def check_stage(output, stage):
    report = json.loads((output / "current-audit.json").read_text(encoding="utf-8"))
    ledger = json.loads((output / "review-ledger.json").read_text(encoding="utf-8"))
    retained = {(item["id"], item["language"]) for item in ledger["same_source_reviews"]
                if item["state"] == "CLOSED" and item["decision"] == "retain"}
    rows = [row for row in report["keys"]
            if stage == "all" or row.get("batch", row["stage"]) == stage]
    if not rows:
        raise SystemExit(f"No keys assigned to {stage}")
    failures = []
    for language in LANGUAGES:
        messages = catalog(ROOT / "data/locale" / f"{language}.ts")
        for row in rows:
            state = row["status"][language]
            if state not in ("translated", "source", "same-source"):
                failures.append((language, row["key"], state))
                continue
            if state == "same-source" and (row["id"], language) not in retained:
                failures.append((language, row["key"], "same-source review missing"))
            if state == "source":
                continue
            source = row["key"][1]
            translation = messages[tuple(row["key"])].find("translation")
            forms = translation.findall("numerusform")
            for node in forms or [translation]:
                text = "".join(node.itertext())
                if source.count("\n") != text.count("\n"):
                    failures.append((language, row["key"], "newline count"))
                tags = lambda value: Counter(re.findall(r"</?[A-Za-z][^>]*>", value))
                if tags(source) != tags(text):
                    failures.append((language, row["key"], "HTML tags"))
                mnemonic = lambda value: len(re.findall(r"&(?!&)", value.replace("&&", "")))
                if mnemonic(source) != mnemonic(text):
                    failures.append((language, row["key"], "mnemonic count"))
    for failure in failures:
        print(json.dumps(failure, ensure_ascii=False))
    if failures:
        raise SystemExit(f"FAIL: {len(failures)} issues in {stage}")
    print(f"PASS: {stage}, {len(rows)} keys × four languages; coverage, placeholders, "
          "plural forms, same-source decisions, HTML, newlines and mnemonics")


def selfcheck():
    ordinary = ("Test", "Value %1", "", "no")
    assert status("en", ordinary, None) == "source-unlisted"
    assert status("ja", ordinary, None) == "missing"
    assert status("en", ("Test", "音高", "", "no"), None) == "missing"
    assert status("ja", ordinary, ET.fromstring("<message><translation/></message>")) == "empty"
    assert status("ja", ordinary, ET.fromstring(
        '<message><translation type="unfinished">値 %1</translation></message>'
    )) == "unfinished"
    assert status("ja", ordinary, ET.fromstring(
        "<message><translation>値 %2</translation></message>"
    )) == "placeholder-error"
    assert status("ja", ordinary, ET.fromstring(
        "<message><translation>値 %1</translation></message>"
    )) == "translated"
    assert status("ja", ("Test", "%n notes", "", "yes"), ET.fromstring(
        "<message><translation><numerusform>%n 音</numerusform><numerusform/></translation></message>"
    )) == "empty"
    assert key("Test", ET.fromstring('<message numerus="yes"><source>%n</source><comment>x</comment></message>')) == (
        "Test", "%n", "x", "yes"
    )
    print("PASS: fallback, missing, empty, unfinished, placeholder, plural and disambiguation checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("inputs", "audit", "worklist", "selfcheck", "check"))
    parser.add_argument("--stage", default="all")
    parser.add_argument("--output", type=Path, default=ROOT / "doc/translation")
    parser.add_argument("--extraction", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if args.command == "inputs":
        files = git("ls-files", "src", "include", "plugins").splitlines()
        files = [path for path in files
                 if Path(path).suffix.lower() in (".cpp", ".cc", ".cxx", ".h", ".hpp", ".ui")]
        output.mkdir(parents=True, exist_ok=True)
        (output / "sources.txt").write_text(
            "\n".join((ROOT / path).as_posix() for path in files) + "\n",
            encoding="utf-8"
        )
        print(f"Extraction inputs: {len(files)} tracked files")
    elif args.command == "audit":
        audit((args.extraction or output / "current.ts").resolve(), output)
    elif args.command == "worklist":
        worklist(output)
    elif args.command == "check":
        check_stage(output, args.stage)
    else:
        selfcheck()


if __name__ == "__main__":
    main()
