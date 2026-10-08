"""M3 preflight tests against the deployed fixed embedded runtime."""
import copy
import json
import os
import pathlib
import runpy
import struct
import sys
import tempfile
import unittest
import wave

ROOT = pathlib.Path(sys.argv.pop(1)).resolve()
BRIDGE = runpy.run_path(str(ROOT / "build/Release/svs-project/bridge.py"))
from libresvip.model.base import InstrumentalTrack, Note, Project, SingingTrack, SongTempo, TimeSignature


class ExportPolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.converters, catalog = BRIDGE["fixed_catalog"]()
        cls.catalog = {f["id"]: f for f in catalog}

    def project(self, audio=None):
        tracks = [SingingTrack(title="中文 A", note_list=[Note(start_pos=0, length=480, key_number=60, lyric="你")]),
                  SingingTrack(title="日本語 B", mute=True, solo=True, note_list=[Note(start_pos=480, length=480, key_number=64, lyric="あ")])]
        if audio:
            tracks.insert(0, InstrumentalTrack(title="伴奏一", audio_file_path=str(audio)))
            tracks.append(InstrumentalTrack(title="伴奏二", audio_file_path=str(audio), offset=960))
        return Project(track_list=tracks, song_tempo_list=[SongTempo(position=0, bpm=120)], time_signature_list=[TimeSignature(bar_index=0, numerator=4, denominator=4)])

    def prepare(self, format_id, project, options=None, selection=None, assets=None):
        return BRIDGE["prepare_export"](project, self.catalog[format_id], options or {}, selection or {}, assets or [])

    def wav(self, directory, rate=44100):
        path = pathlib.Path(directory) / "伴奏.wav"
        with wave.open(str(path), "wb") as stream:
            stream.setparams((2, 2, rate, 0, "NONE", "not compressed"))
            stream.writeframes(struct.pack("<hh", 100, -100) * 4410)
        return path

    def test_full_catalog_and_all_singing_cardinalities(self):
        self.assertEqual(len(self.catalog), 40)
        exports = [f for f in self.catalog.values() if f["canExport"]]
        self.assertEqual(len(exports), 39)
        project = self.project()
        original = project.model_dump(mode="json")
        for spec in exports:
            with self.subTest(format=spec["id"]):
                limit = spec["exportPolicy"]["singing"]
                if spec["id"] == "ust" and float(spec["outputDefaults"]["version"]) < 2:
                    limit = 1
                if limit == 1:
                    with self.assertRaisesRegex(ValueError, "必须显式选择"):
                        self.prepare(spec["id"], project)
                    projected, options, losses = self.prepare(spec["id"], project, selection={"singingTrack": 1})
                    self.assertEqual([t.title for t in projected.track_list], ["日本語 B"])
                    self.assertTrue(any(l["track"] == "中文 A" and l["field"] == "track" for l in losses))
                    if "track_index" in options:
                        self.assertEqual(options["track_index"], 0)
                else:
                    projected, _, _ = self.prepare(spec["id"], project)
                    self.assertEqual(len(projected.track_list), 2)
                self.assertEqual(project.model_dump(mode="json"), original)

    def test_audio_selection_preserves_original_index_and_names(self):
        with tempfile.TemporaryDirectory() as directory:
            project = self.project(self.wav(directory))
            original = project.model_dump(mode="json")
            with self.assertRaisesRegex(ValueError, "必须显式选择"):
                self.prepare("vsqx", project)
            result, _, losses = self.prepare("vsqx", project, selection={"audioTrack": 3})
            self.assertEqual([t.title for t in result.track_list], ["中文 A", "日本語 B", "伴奏二"])
            self.assertTrue(any(l["track"] == "伴奏一" and l["field"] == "audio" for l in losses))
            result, options, losses = self.prepare("ds", project, selection={"singingTrack": 2})
            self.assertEqual([t.title for t in result.track_list], ["日本語 B"])
            self.assertEqual(options["track_index"], 0)
            self.assertEqual(sum(l["field"] == "audio" for l in losses), 2)
            for wrong in (-1, 0, 20, True, "2"):
                with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                    self.prepare("ds", project, selection={"singingTrack": wrong})
            self.assertEqual(project.model_dump(mode="json"), original)

    def test_real_audio_constraints_and_relative_companions(self):
        with tempfile.TemporaryDirectory() as directory:
            audio = self.wav(directory, 48000)
            project = self.project(audio)
            with self.assertRaisesRegex(ValueError, "44100 Hz / 16 bit"):
                self.prepare("vpr", project)
            with self.assertRaisesRegex(ValueError, "44100 Hz / 16 bit"):
                self.prepare("vsqx", project, selection={"audioTrack": 0})
            # Both formats that the initial heuristic misclassified retain audio.
            for format_id in ("svp", "ccs", "aisp"):
                result, _, _ = self.prepare(format_id, project)
                self.assertEqual(len(result.track_list), 4)
            for track in project.track_list:
                if isinstance(track, InstrumentalTrack):
                    track.audio_file_path = "audio-1.wav"
            assets = [{"name": "audio-1.wav", "source": str(audio)}]
            result, _, _ = self.prepare("json", project, assets=assets)
            self.assertEqual(result.track_list[0].audio_file_path, "audio-1.wav")
            with self.assertRaisesRegex(ValueError, "资源不可用"):
                self.prepare("json", project)

    def test_legacy_ust_and_named_format_losses(self):
        project = self.project()
        with self.assertRaisesRegex(ValueError, "必须显式选择"):
            self.prepare("ust", project, {"version": 1.2})
        result, options, _ = self.prepare("ust", project, {"version": 1.2}, {"singingTrack": 1})
        self.assertEqual(len(result.track_list), 1)
        self.assertEqual(options["track_index"], 0)
        result, _, _ = self.prepare("ust", project, {"version": 2.0})
        self.assertEqual(len(result.track_list), 2)
        project.track_list[0].pan = 0.5
        _, _, losses = self.prepare("ustx", project)
        self.assertTrue(any(l["track"] == "中文 A" and l["field"] == "pan" for l in losses))
        for format_id in ("lrc", "ass", "srt", "svg", "ds"):
            _, _, losses = self.prepare(format_id, project, selection={"singingTrack": 0})
            self.assertTrue(any(l["field"] == "project" for l in losses))
        project.song_tempo_list.append(SongTempo(position=480, bpm=180))
        _, _, losses = self.prepare("vog", project)
        self.assertTrue(any(l["field"] == "tempo" and l["track"] == "全局" for l in losses))
        project.track_list[0].note_list[0].start_pos = 1920 * 4096
        with self.assertRaisesRegex(ValueError, "拒绝静默截断"):
            self.prepare("vsq", project)

    def test_protocol_preflight_and_unconfirmed_dump_write_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = pathlib.Path(directory)
            project = self.project()
            request = {"protocol": 1, "requestId": "M3-policy", "operation": "inspectExport",
                       "formatId": "lrc", "path": str(workspace / "song.lrc"),
                       "project": project.model_dump(mode="json"), "selection": {"singingTrack": 1}}
            untouched = copy.deepcopy(request)
            result = BRIDGE["process"](request, workspace)
            self.assertEqual(result["project"]["track_list"][0]["title"], "日本語 B")
            self.assertGreater(len(result["losses"]), 0)
            self.assertEqual(request, untouched)
            self.assertEqual(list(workspace.iterdir()), [])
            request["operation"] = "exportProject"
            with self.assertRaisesRegex(ValueError, "尚未确认"):
                BRIDGE["process"](request, workspace)
            self.assertEqual(list(workspace.iterdir()), [])
            request["acceptLosses"] = True
            previous = pathlib.Path.cwd()
            try:
                result = BRIDGE["process"](request, workspace)
            finally:
                os.chdir(previous)
            self.assertTrue((workspace / "song.lrc").is_file())
            self.assertTrue(result["losses"])

    def test_companions_alone_are_not_a_successful_conversion(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as audio_directory:
            workspace = pathlib.Path(directory)
            audio = self.wav(audio_directory)
            project = self.project(audio)
            for track in project.track_list:
                if isinstance(track, InstrumentalTrack):
                    track.audio_file_path = "audio-1.wav"
            class NoOutputConverter:
                @staticmethod
                def dump(*args):
                    pass
            namespace = BRIDGE["process"].__globals__
            previous_catalog = namespace["fixed_catalog"]
            previous_directory = pathlib.Path.cwd()
            namespace["fixed_catalog"] = lambda: ({"json": NoOutputConverter}, list(self.catalog.values()))
            try:
                with self.assertRaisesRegex(ValueError, "no project output"):
                    BRIDGE["process"]({"protocol": 1, "requestId": "empty-dump", "operation": "exportProject",
                                       "formatId": "json", "path": str(workspace / "song.json"),
                                       "project": project.model_dump(mode="json"),
                                       "assets": [{"name": "audio-1.wav", "source": str(audio)}]}, workspace)
            finally:
                namespace["fixed_catalog"] = previous_catalog
                os.chdir(previous_directory)


if __name__ == "__main__":
    unittest.main(verbosity=2)
