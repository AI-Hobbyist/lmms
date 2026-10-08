"""Protocol/integration checks against the in-place embedded converter runtime."""
import json
import pathlib
import subprocess
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "build/Release/svs-project"


def invoke(operation, workspace, **fields):
    request = {"protocol": 1, "requestId": "test-请求", "operation": operation, **fields}
    result = subprocess.run([str(RUNTIME / "python/python.exe"), "-I", str(RUNTIME / "bridge.py"), "--workspace", str(workspace)], input=json.dumps(request).encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if result.returncode:
        raise AssertionError(result.stderr.decode(errors="replace"))
    response = json.loads(result.stdout)
    if response["status"] != "success":
        print(result.stderr.decode(errors="replace"))
    assert response["requestId"] == request["requestId"]
    assert response["protocol"] == response["dataVersion"] == 1
    return response


class BridgeTest(unittest.TestCase):
    def test_all_formats_and_direction_schemas(self):
        with tempfile.TemporaryDirectory(prefix="SVS项目-") as name:
            result = invoke("listFormats", pathlib.Path(name))
        self.assertEqual(result["status"], "success", result)
        formats = result["formats"]
        self.assertEqual(len(formats), 40)
        self.assertEqual(sum(f["canImport"] for f in formats), 36)
        self.assertEqual(sum(f["canExport"] for f in formats), 39)
        self.assertEqual(sum(len(f["suffixes"]) for f in formats), 45)
        for f in formats:
            self.assertEqual("inputDefaults" in f, f["canImport"])
            self.assertEqual("outputDefaults" in f, f["canExport"])
        (ROOT / "doc/svs/project/M1-runtime-catalog.json").write_text(json.dumps(formats, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def test_representative_roundtrip_unicode(self):
        examples = json.loads((ROOT / "doc/svs/project/protocol-examples.json").read_text(encoding="utf-8"))
        project = examples["exportRequest"]["project"]
        for format_id in ("json", "svp", "ustx", "tlp"):
            with self.subTest(format_id=format_id), tempfile.TemporaryDirectory(prefix="SVS中日韩-歌あ가-") as name:
                work = pathlib.Path(name)
                export = work / "export"
                export.mkdir()
                path = export / "output" / ("歌あ가." + format_id)
                result = invoke("exportProject", export, formatId=format_id, path=str(path), project=project, options={"down_sample": 0})
                self.assertEqual(result["status"], "success", result)
                self.assertTrue(path.is_file())
                read = work / "read"
                read.mkdir()
                result = invoke("importProject", read, formatId=format_id, path=str(path))
                self.assertEqual(result["status"], "success", result)
                tracks = [t for t in result["project"]["track_list"] if t["type_"] == "Singing"]
                self.assertTrue(tracks)
                self.assertEqual(tracks[0]["note_list"][0]["lyric"], "你")
                self.assertEqual(tracks[0]["note_list"][0]["key_number"], 60)
                self.assertTrue(tracks[0]["edited_params"]["pitch"]["points"])

    def test_invalid_protocol_and_paths(self):
        with tempfile.TemporaryDirectory() as name:
            work = pathlib.Path(name)
            bad = work / "bad.svp"
            bad.write_bytes(b"corrupted")
            task = work / "task"
            task.mkdir()
            result = invoke("importProject", task, formatId="svp", path=str(bad))
            self.assertEqual(result["status"], "error")
            self.assertEqual(bad.read_bytes(), b"corrupted")
        with tempfile.TemporaryDirectory() as name:
            result = invoke("exportProject", pathlib.Path(name), formatId="vshp", path=str(pathlib.Path(name) / "out.vshp"), project={})
            self.assertEqual(result["status"], "error")
        with tempfile.TemporaryDirectory() as name:
            work = pathlib.Path(name)
            bad = work / "bad.vpr"
            with zipfile.ZipFile(bad, "w") as z:
                z.writestr("../escape.wav", "unsafe")
            task = work / "task"
            task.mkdir()
            result = invoke("importProject", task, formatId="vpr", path=str(bad))
            self.assertEqual(result["status"], "error")
            self.assertIn("Unsafe archive", result["error"]["message"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
