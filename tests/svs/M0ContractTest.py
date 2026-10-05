"""Static M0 compatibility baseline. Run from any directory; no reference runtime."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def enum_names(file):
    text = (ROOT / file).read_text(encoding="utf-8-sig")
    body = re.search(r"enum class Type\s*\{([^}]+)\}", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [v.strip().split("=")[0].strip() for v in body.split(",") if v.strip()]


def main():
    tracks = enum_names("include/Track.h")
    plugins = enum_names("include/Plugin.h")
    assert tracks[:7] == ["Instrument", "Pattern", "Sample", "Event", "Video", "Automation", "HiddenAutomation"]
    assert tracks[-1] == "Count"
    assert plugins[:7] == ["Instrument", "Effect", "ImportFilter", "ExportFilter", "Tool", "Library", "Other"]
    assert re.search(r"Undefined\s*=\s*255", (ROOT / "include/Plugin.h").read_text())
    required = {
        "src/core/Track.cpp": ["Track::create", "saveTrack", "loadTrack"],
        "src/gui/PluginBrowser.cpp": ["addPlugins", "Plugin::Type::Instrument"],
        "src/gui/editors/TrackContainerView.cpp": ["dropEvent", 'type == "instrument"'],
        "src/core/ProjectRenderer.cpp": ["startProcessing", "startExport", "abortProcessing"],
        "src/gui/LmmsStyle.cpp": ["resources:style.css", "setStyleSheet"],
        "src/CMakeLists.txt": ["ADD_LIBRARY(lmmsobjs OBJECT", "INSTALL(TARGETS lmms"],
    }
    for name, symbols in required.items():
        source = (ROOT / name).read_text(encoding="utf-8-sig")
        assert all(s in source for s in symbols), name
    contract = (ROOT / "doc/svs/M0-contract.md").read_text(encoding="utf-8")
    for term in ["major=1", "minor=0", "contentOffset", "release_result", "generation", "revision", "requestId", "MANUAL/PENDING", "50 ms", "Pronunciation"]:
        assert term in contract, term
    print("M0 PASS: legacy enum values, integration entry points, ABI/time/ownership contract")


if __name__ == "__main__":
    main()
