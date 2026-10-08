"""Small real-process regressions for fixed-converter integration defects."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import uuid
import zipfile

ROOT = pathlib.Path(sys.argv.pop(1)).resolve()
RUNTIME = ROOT / "build/Release/svs-project"


class AdapterTest(unittest.TestCase):
    def project(self, title="test"):
        return {"song_tempo_list": [{"position": 0, "bpm": 120}],
                "time_signature_list": [{"bar_index": 0, "numerator": 4, "denominator": 4}],
                "track_list": [{"type_": "Singing", "title": title,
                                "note_list": [{"start_pos": 0, "length": 480, "key_number": 60, "lyric": "la"}],
                                "edited_params": {"pitch": {"points": [[-192000, -100], [1920, -100],
                                                                         [1920, 6000], [2160, 6050],
                                                                         [2400, 6000], [2400, -100], [1073741823, -100]]}}}]}

    def call(self, workspace, request, expect_status="success"):
        request = {**request, "protocol": 1, "requestId": uuid.uuid4().hex}
        result = subprocess.run([str(RUNTIME / "python/python.exe"), "-I", str(RUNTIME / "bridge.py"),
                                 "--workspace", str(workspace)], input=json.dumps(request).encode(),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        response = json.loads(result.stdout)
        self.assertEqual(response["status"], expect_status, str(response.get("error")) + result.stderr.decode("utf-8", "replace"))
        return response

    def roundtrip(self, format_id, suffix, project, output_options=None, input_options=None):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            exporting = root / "export-task"
            importing = root / "import-task"
            exporting.mkdir()
            importing.mkdir()
            path = exporting / "output" / ("工程_日本語_한국어." + suffix)
            exported = self.call(exporting, {"operation": "exportProject", "formatId": format_id, "path": str(path),
                                              "project": project, "options": output_options or {}, "acceptLosses": True})
            self.assertTrue(path.is_file())
            if suffix == "acet":
                self.assertTrue(zipfile.is_zipfile(path), "ACET must be its real archive container")
                with zipfile.ZipFile(path) as archive:
                    self.assertTrue(any(name.endswith(".acep") for name in archive.namelist()))
            if suffix == "mxl":
                self.assertTrue(zipfile.is_zipfile(path), "MXL must be its real compressed MusicXML container")
                with zipfile.ZipFile(path) as archive:
                    self.assertIn("META-INF/container.xml", archive.namelist())
            imported = self.call(importing, {"operation": "importProject", "formatId": format_id, "path": str(path),
                                              "options": input_options or {}})
            self.assertTrue(exported["files"])
            self.assertEqual(len(imported["project"]["track_list"][0]["note_list"]), len(project["track_list"][0]["note_list"]))
            return imported["project"]

    def test_acet_actual_archive_roundtrip(self):
        self.roundtrip("acep", "acet", self.project())

    def test_vxf_utf8_chunk_boundary_keeps_title(self):
        title = "AB中文中文"
        result = self.roundtrip("vxf", "vxf", self.project(title))
        self.assertEqual(result["track_list"][0]["title"], title)

    def test_vsq_breakpoints_do_not_create_duplicate_ini_keys(self):
        result = self.roundtrip("vsq", "vsq", self.project())
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        self.assertTrue(any(point[1] > 6000 for point in pitch if point[1] != -100))

    def test_ust_existing_utf8_option_preserves_chinese(self):
        project = self.project("中文")
        project["track_list"][0]["note_list"][0]["lyric"] = "你"
        result = self.roundtrip("ust", "ust", project, {"encoding": "utf-8"}, {"encoding": "utf-8"})
        self.assertEqual(result["track_list"][0]["note_list"][0]["lyric"], "你")

    def test_early_and_late_tempo_events_remain_on_native_timeline(self):
        project = self.project()
        project["song_tempo_list"] = [{"position": 0, "bpm": 120}, {"position": 1440, "bpm": 150},
                                      {"position": 3360, "bpm": 100}]
        for format_id in ("acep", "s5p", "svip3", "svp", "tlp", "ustx", "vsq", "vsqx", "vspx", "xvsq", "mtp", "ufdata", "tssln", "tsmsln"):
            with self.subTest(format=format_id):
                result = self.roundtrip(format_id, format_id, project)
                self.assertEqual(result["song_tempo_list"], project["song_tempo_list"])

    def test_muta_first_note_pitch_remains_at_its_original_tick(self):
        result = self.roundtrip("mtp", "mtp", self.project())
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        self.assertIn([1920, 6000], pitch)
        self.assertIn([2160, 6050], pitch)
        self.assertIn([2400, -100], pitch)

    def test_s5p_sparse_edited_pitch_is_not_discarded(self):
        project = self.project()
        project["track_list"][0]["edited_params"]["pitch"]["points"].insert(3, [1930, 6137])
        result = self.roundtrip("s5p", "s5p", project)
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        actual = next(p[1] for p in pitch if p[0] == 1930 and p[1] != -100)
        self.assertLessEqual(abs(actual - 6137), .5)

    def test_ufdata_silence_marker_is_not_zero_midi_pitch(self):
        result = self.roundtrip("ufdata", "ufdata", self.project())
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        self.assertFalse(any(p[1] == 0 for p in pitch))
        self.assertIn([2400, -100], pitch)

    def test_ccs_pitch_before_early_tempo_change_keeps_first_bar(self):
        project = self.project()
        project["song_tempo_list"].append({"position": 1440, "bpm": 150})
        result = self.roundtrip("ccs", "ccs", project)
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        first = next(p for p in pitch if p[1] != -100)
        self.assertEqual(first, [1920, 6000])

    def test_nn_bend_is_relative_to_note_key(self):
        result = self.roundtrip("nn", "nn", self.project())
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        first = next(p for p in pitch if p[1] != -100)
        self.assertEqual(first, [1920, 6000])
        self.assertTrue(any(p[1] >= 6048 for p in pitch if p[1] != -100))

    def test_midi_pitch_bend_semitones_become_cents(self):
        for suffix in ("mid", "midi"):
            with self.subTest(suffix=suffix):
                result = self.roundtrip("mid", suffix, self.project())
                pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
                self.assertIn([2160, 6050], pitch)

    def test_midi_zero_bend_is_voiced_inside_a_note(self):
        project = self.project()
        points = project["track_list"][0]["edited_params"]["pitch"]["points"]
        for point in points:
            if point[1] != -100:
                point[1] = 6000
        result = self.roundtrip("mid", "mid", project)
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        self.assertFalse(any(1920 < p[0] < 2400 and p[1] == -100 for p in pitch))

    def test_ds_default_segments_keep_first_note_pitch_and_real_time(self):
        project = self.project()
        project["track_list"][0]["note_list"][0]["lyric"] = "你"
        result = self.roundtrip("ds", "ds", project)
        pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
        self.assertTrue(any(p[1] >= 6048 for p in pitch if p[1] != -100))
        self.assertLessEqual(abs(result["track_list"][0]["note_list"][0]["length"] - 480), .5)

    def test_musicxml_native_tempo_meter_and_compressed_alias(self):
        project = self.project()
        project["time_signature_list"][0]["numerator"] = 3
        project["song_tempo_list"].append({"position": 240, "bpm": 150})
        for suffix in ("musicxml", "xml", "mxl"):
            with self.subTest(suffix=suffix):
                result = self.roundtrip("musicxml", suffix, project)
                self.assertEqual(result["song_tempo_list"], project["song_tempo_list"])
                self.assertEqual(result["time_signature_list"], project["time_signature_list"])

    def test_loss_preflight_names_pitch_tempo_meter_and_blocks_unaccepted_write(self):
        project = self.project("中文")
        project["song_tempo_list"].append({"position": 240, "bpm": 150})
        project["time_signature_list"].append({"bar_index": 2, "numerator": 3, "denominator": 4})
        track = project["track_list"][0]
        track["volume"] = .75
        track["note_list"][0]["start_pos"] = 1
        track["note_list"][0]["pronunciation"] = "la"
        with tempfile.TemporaryDirectory() as directory:
            workspace = pathlib.Path(directory)
            path = workspace / "output" / "loss.nn"
            request = {"formatId": "nn", "path": str(path), "project": project}
            inspected = self.call(workspace, {**request, "operation": "inspectExport"})
            fields = {loss["field"] for loss in inspected["losses"]}
            self.assertTrue({"pitch", "tempo", "meter", "noteTime", "pronunciation", "volume"} <= fields)
            self.assertTrue(all(loss["formatId"] == "nn" and loss["reason"] for loss in inspected["losses"]))
            self.assertFalse(path.exists())
            rejected = self.call(workspace, {**request, "operation": "exportProject", "acceptLosses": False}, "error")
            self.assertIn("损失", rejected["error"]["message"])
            self.assertFalse(path.exists())

    def test_supported_meter_changes_keep_bar_indices(self):
        project = self.project()
        project["time_signature_list"].append({"bar_index": 1, "numerator": 3, "denominator": 4})
        project["track_list"][0]["note_list"][0]["start_pos"] = 2400
        points = project["track_list"][0]["edited_params"]["pitch"]["points"]
        for point in points:
            if -192000 < point[0] < 1073741823:
                point[0] += 2400
        for format_id, suffix in (("json", "json"), ("svp", "svp"), ("musicxml", "mxl")):
            with self.subTest(format=format_id):
                result = self.roundtrip(format_id, suffix, project)
                self.assertEqual(result["time_signature_list"], project["time_signature_list"])
                self.assertEqual(result["track_list"][0]["note_list"][0]["start_pos"], 2400)

    def test_existing_ust_two_track_and_vsqx_three_options(self):
        project = self.project("中文")
        project["song_tempo_list"].append({"position": 240, "bpm": 150})
        result = self.roundtrip("vsqx", "vsqx", project, {"vsqx_version": 3})
        self.assertEqual(result["song_tempo_list"], project["song_tempo_list"])
        project["track_list"].append(json.loads(json.dumps(project["track_list"][0])))
        project["track_list"][1]["title"] = "第二轨"
        result = self.roundtrip("ust", "ust", project, {"version": 2, "encoding": "utf-8"}, {"encoding": "utf-8"})
        self.assertEqual(len(result["track_list"]), 2)

    def test_continuous_controllers_do_not_join_real_note_silence(self):
        project = self.project()
        track = project["track_list"][0]
        track["note_list"].append({"start_pos": 720, "length": 480, "key_number": 62, "lyric": "la"})
        track["edited_params"]["pitch"]["points"][-1:-1] = [[2640, -100], [2640, 6200],
                                                               [2880, 6250], [3120, 6200], [3120, -100]]
        for format_id in ("dv", "mid", "svp", "ustx", "vpr", "vspx", "vsq", "vsqx", "xvsq"):
            with self.subTest(format=format_id):
                result = self.roundtrip(format_id, format_id, project)
                pitch = result["track_list"][0]["edited_params"]["pitch"]["points"]
                self.assertIn([2400, -100], pitch)
                self.assertIn([2640, -100], pitch)
                self.assertFalse(any(2400 < p[0] < 2640 and p[1] != -100 for p in pitch))


if __name__ == "__main__":
    unittest.main(verbosity=2)
