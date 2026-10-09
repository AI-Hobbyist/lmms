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
SOURCE_PROJECT_PATH = None
sys.dont_write_bytecode = True


def contained(path, root):
    return pathlib.Path(path).resolve().is_relative_to(root.resolve())


def guard_writes(root):
    """Converters can write only their task; never extract over source/user files."""

    def audit(event, args):
        candidates = []
        if event == "open" and not isinstance(args[0], int):
            mode, flags = args[1:3]
            if (mode and any(c in mode for c in "wax+")) or (
                flags and flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC)
            ):
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
            if (
                name.startswith("/")
                or ".." in parts
                or ":" in name
                or stat.S_ISLNK(entry.external_attr >> 16)
            ):
                raise ValueError("Unsafe archive resource path: " + name)


def fixed_catalog():
    manifest = json.loads(
        (pathlib.Path(__file__).parent / "formats.json").read_text(encoding="utf-8")
    )
    policy = json.loads(
        (pathlib.Path(__file__).parent / "export-policy.json").read_text(encoding="utf-8")
    )
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
        if (converter._alias_, converter._version_, read, write) != (
            spec["id"],
            spec["version"],
            spec["canImport"],
            spec["canExport"],
        ):
            raise ValueError("Converter differs from frozen contract: " + spec["id"])
        item = {
            k: spec[k]
            for k in ("id", "name", "version", "suffixes", "canImport", "canExport", "capabilities")
        }
        if write:
            item["exportPolicy"] = policy["formats"][spec["id"]]
        for prefix, enabled, cls in (
            ("input", read, converter.input_option_cls),
            ("output", write, converter.output_option_cls),
        ):
            if enabled:
                item[prefix + "Defaults"] = cls().model_dump(mode="json")
                item[prefix + "Schema"] = cls.model_json_schema()
        for key in ("import_pitch", "use_edited_pitch"):
            if read and key in item.get("inputDefaults", {}):
                item["inputDefaults"][key] = True
        if read and "extract_audio" in item["inputDefaults"]:
            item["inputDefaults"]["extract_audio"] = True
        if spec["id"] == "svp":
            item["inputDefaults"]["pitch"] = "full"
        converters[spec["id"]] = converter
        descriptions.append(item)
    install_fixed_adapters()
    return converters, descriptions


def install_fixed_adapters():
    """Small integration fixes for the frozen converters; vendor files stay intact."""
    from libresvip.plugins.vxf.vx_beta_generator import VxBetaGenerator
    from libresvip.plugins.vsq.vsq_generator import VsqGenerator

    if not getattr(VxBetaGenerator.generate_track, "_lmms_utf8_title", False):
        original_vxf = VxBetaGenerator.generate_track

        def generate_vxf_track(self, track):
            # Upstream slices UTF-8 bytes at arbitrary 12-byte boundaries before
            # decoding each UMP name packet. Retain its note/pitch generation and
            # replace only name packets with complete characters fitting 12 bytes.
            result = original_vxf(self, track.model_copy(update={"title": ""}))
            if result is None:
                return None
            chunks, current = [], ""
            for character in track.title:
                if len((current + character).encode("utf-8")) > 12:
                    chunks.append(current)
                    current = ""
                current += character
            if current:
                chunks.append(current)
            result["title_parts"] = [
                {
                    "name": chunk,
                    "seq_stat": (
                        0x10
                        if len(chunks) == 1
                        else 0x50 if index == 0 else 0xD0 if index == len(chunks) - 1 else 0x90
                    ),
                }
                for index, chunk in enumerate(chunks)
            ]
            return result

        generate_vxf_track._lmms_utf8_title = True
        VxBetaGenerator.generate_track = generate_vxf_track

    if not getattr(VsqGenerator.generate_track_text, "_lmms_unique_keys", False):
        original_vsq = VsqGenerator.generate_track_text

        def generate_vsq_text(self, *args):
            # VSQ's INI control lists contain one value per tick. Multiple emitted
            # controls at the same tick use the final value, never duplicate keys.
            lines, previous, section = [], {}, ""
            for line in original_vsq(self, *args).splitlines(keepends=True):
                stripped = line.strip()
                if stripped.startswith("[") and stripped.endswith("]"):
                    section = stripped
                key, separator, _ = stripped.partition("=")
                if separator:
                    identity = (section, key.strip().casefold())
                    if identity in previous:
                        lines[previous[identity]] = ""
                    previous[identity] = len(lines)
                lines.append(line)
            return "".join(lines)

        generate_vsq_text._lmms_unique_keys = True
        VsqGenerator.generate_track_text = generate_vsq_text

    # These two generators subtract the pre-measure, although their native files
    # and parsers use absolute tempo ticks including it. Keep the actual timeline.
    from libresvip.plugins.vsqx.vsq3_generator import Vsq3Generator, Vsq3Tempo, BPM_RATE as V3_RATE
    from libresvip.plugins.vsqx.vsq4_generator import Vsq4Generator, Vsq4Tempo, BPM_RATE as V4_RATE
    from libresvip.plugins.xvsq.cadencii_generator import (
        CadenciiGenerator,
        TempoTableEntry,
        bpm2tempo,
    )
    from libresvip.plugins.xvsq.cadencii_parser import (
        CadenciiParser,
        SongTempo,
        tempo2bpm,
        skip_tempo_list,
    )
    from libresvip.core.tick_counter import shift_tempo_list

    def vsq3_tempos(self, tempos, tick_prefix):
        return [Vsq3Tempo(pos_tick=t.position, bpm=int(t.bpm * V3_RATE)) for t in tempos]

    def vsq4_tempos(self, tempos, tick_prefix):
        return [Vsq4Tempo(pos_tick=t.position, bpm=int(t.bpm * V4_RATE)) for t in tempos]

    def xvsq_tempos(self, tempos, tick_prefix):
        return [
            TempoTableEntry(
                clock=t.position,
                tempo=bpm2tempo(t.bpm),
                time=round(t.position * 2 / self.first_bar_length),
            )
            for t in tempos
        ]

    def parse_xvsq_tempos(self, entries, tick_prefix):
        # Clock is the absolute tick coordinate; Time is unrelated metadata.
        return skip_tempo_list(
            [SongTempo(position=t.clock, bpm=tempo2bpm(t.tempo)) for t in entries], tick_prefix
        )

    Vsq3Generator.generate_tempos = vsq3_tempos
    Vsq4Generator.generate_tempos = vsq4_tempos
    CadenciiGenerator.generate_tempos = xvsq_tempos
    CadenciiParser.parse_tempos = parse_xvsq_tempos

    if not getattr(CadenciiGenerator.generate_project, "_lmms_audio_header", False):
        original_xvsq = CadenciiGenerator.generate_project

        def xvsq_project(self, project):
            from libresvip.model.base import InstrumentalTrack, SingingTrack

            # Upstream dereferences note_list before its singing-track filter.
            # Retain its writer and attach the existing native BGM generator.
            result = original_xvsq(
                self,
                project.model_copy(
                    update={
                        "track_list": [t for t in project.track_list if isinstance(t, SingingTrack)]
                    }
                ),
            )
            result.bgm_files.bgm_file.extend(
                self.generate_instrumental_tracks(
                    [t for t in project.track_list if isinstance(t, InstrumentalTrack)]
                )
            )
            return result

        xvsq_project._lmms_audio_header = True
        CadenciiGenerator.generate_project = xvsq_project

    from libresvip.plugins.ace.ace_parser import AceMobileParser
    from libresvip.plugins.vsqx.vsqx_parser import VsqxParser
    from libresvip.plugins.dv.dv_parser import DeepVocalParser

    if not getattr(AceMobileParser.find_audio_path, "_lmms_source_resources", False):
        original_ace_path = AceMobileParser.find_audio_path

        def ace_audio_path(self, track):
            path = original_ace_path(self, track)
            if path is not None or SOURCE_PROJECT_PATH is None:
                return path
            old_path, self.path = self.path, SOURCE_PROJECT_PATH
            try:
                return original_ace_path(self, track)
            finally:
                self.path = old_path

        ace_audio_path._lmms_source_resources = True
        AceMobileParser.find_audio_path = ace_audio_path

    if not getattr(VsqxParser.parse_instrumental_tracks, "_lmms_source_resources", False):
        original_vsqx_audio = VsqxParser.parse_instrumental_tracks

        def vsqx_audio(self, *args):
            old_path = self.src_path
            if SOURCE_PROJECT_PATH is not None:
                self.src_path = SOURCE_PROJECT_PATH
            try:
                return original_vsqx_audio(self, *args)
            finally:
                self.src_path = old_path

        vsqx_audio._lmms_source_resources = True
        VsqxParser.parse_instrumental_tracks = vsqx_audio

    if not getattr(DeepVocalParser.parse_instrumental_tracks, "_lmms_audio_prefix", False):
        original_dv_audio = DeepVocalParser.parse_instrumental_tracks

        def dv_audio(self, *args):
            tracks = original_dv_audio(self, *args)
            for track in tracks:
                track.offset -= 2 * self.tick_prefix
            return tracks

        dv_audio._lmms_audio_prefix = True
        DeepVocalParser.parse_instrumental_tracks = dv_audio

    from libresvip.plugins.ps_project.pocket_singer_parser import PocketSingerParser
    from libresvip.plugins.vfp.vox_factory_parser import (
        VOXFactoryParser,
        VOXFactoryAudioTrack,
        InstrumentalTrack,
    )

    if not getattr(PocketSingerParser.parse_instrumental_tracks, "_lmms_embedded_audio", False):
        original_pocket_audio = PocketSingerParser.parse_instrumental_tracks

        def pocket_audio(self):
            # Files with BGM in ps_structure.ps need extraction even when the
            # optional config.json BGM metadata is absent (including this writer).
            if self.options.extract_audio and self.options.import_instrumental_track:
                for track in self.project.bgm_info.tracks:
                    name = f"{track.file_name}.{track.file_type}"
                    target = self.path.parent / name
                    if not target.exists():
                        target.write_bytes(self.archive_file.read(name))
            tracks = original_pocket_audio(self)
            for track in tracks:
                track.volume = self.project.bgm_info.bgm_volume
            return tracks

        pocket_audio._lmms_embedded_audio = True
        PocketSingerParser.parse_instrumental_tracks = pocket_audio

    if not getattr(VOXFactoryParser.parse_tracks, "_lmms_audio_paths", False):
        original_vox_tracks = VOXFactoryParser.parse_tracks

        def vox_tracks(self, project):
            tracks = original_vox_tracks(self, project)
            paths = []
            if self.options.import_instrumental_track:
                for _, track in sorted(
                    project.track_bank.items(), key=lambda x: project.track_order.index(x[0])
                ):
                    if isinstance(track, VOXFactoryAudioTrack):
                        for _, clip in sorted(
                            track.clip_bank.items(), key=lambda x: track.clip_order.index(x[0])
                        ):
                            if clip.source_audio_data_key:
                                path = (self.path.parent / clip.name).with_suffix(
                                    pathlib.Path(clip.source_audio_data_key).suffix
                                )
                                if path.is_file():
                                    paths.append(path)
            audio = [t for t in tracks if isinstance(t, InstrumentalTrack)]
            if len(audio) != len(paths):
                raise ValueError("VFP native audio track/resource count differs")
            for track, path in zip(audio, paths):
                track.audio_file_path = str(path)
            return tracks

        vox_tracks._lmms_audio_paths = True
        VOXFactoryParser.parse_tracks = vox_tracks

    from libresvip.plugins.mtp.muta_generator import MutaGenerator, MutaPoint

    if not getattr(MutaGenerator.generate_singing_tracks, "_lmms_zero_based_part", False):
        original_muta = MutaGenerator.generate_singing_tracks

        def muta_singing_tracks(self, tracks):
            generated = original_muta(self, tracks)
            for source, target in zip(tracks, generated):
                part = target.song_track_data[0]
                # The original part begins one bar late with negative note ticks,
                # then clamps early pitch controls to -1, losing their timing.
                # A zero-based native part preserves both notes and control ticks.
                part.start = 0
                part.length -= self.first_bar_length
                for note in part.notes:
                    note.start += self.first_bar_length
                part.params.pitch_data = [
                    MutaPoint(
                        time=max(p.x - self.first_bar_length, -1),
                        value=12900 if p.y < 0 else p.y - 1200,
                    )
                    for p in source.edited_params.pitch.points.root
                ]
            return generated

        muta_singing_tracks._lmms_zero_based_part = True
        MutaGenerator.generate_singing_tracks = muta_singing_tracks

    from libresvip.plugins.s5p.synthv_editor_generator import SynthVEditorGenerator, TICK_RATE
    from libresvip.plugins.s5p.synthv_editor_parser import SynthVEditorParser
    from libresvip.model.point import Point

    if not getattr(SynthVEditorParser.parse_tempos, "_lmms_zero_based_sync", False):
        original_s5p_tempos = SynthVEditorParser.parse_tempos

        def s5p_tempos(self, tempos):
            parsed = original_s5p_tempos(self, tempos)
            # Normalize before the parser constructs its pitch simulator and
            # audio synchronizer, rather than after their results are produced.
            return [
                (
                    t.model_copy(update={"position": t.position - self.first_bar_length})
                    if index
                    else t
                )
                for index, t in enumerate(parsed)
            ]

        s5p_tempos._lmms_zero_based_sync = True
        SynthVEditorParser.parse_tempos = s5p_tempos
    if not getattr(SynthVEditorGenerator.generate_parameters, "_lmms_pitch_grid", False):
        original_s5p_parameters = SynthVEditorGenerator.generate_parameters
        original_s5p_delta = SynthVEditorGenerator.generate_pitch_delta

        def s5p_delta(self, pitch, simulator, interval):
            # Native S5P declares its sample interval. At the upstream 3.75-tick
            # interval, isolated controls round onto different ticks and the
            # parser fills intervening samples from automatic pitch instead.
            # Fill only existing voiced segments on an exact one-tick grid.
            dense, previous = [], None
            for point in pitch:
                if point.y == -100:
                    previous = None
                    continue
                if previous is not None and point.x > previous.x:
                    if len(dense) + point.x - previous.x > 2_000_000:
                        raise ValueError("S5P edited pitch exceeds bounded sample count")
                    dense.extend(
                        Point(
                            x,
                            previous.y
                            + (point.y - previous.y) * (x - previous.x) / (point.x - previous.x),
                        )
                        for x in range(previous.x + 1, point.x)
                    )
                dense.append(point)
                previous = point
            return original_s5p_delta(self, dense, simulator, TICK_RATE)

        def s5p_parameters(self, *args):
            result = original_s5p_parameters(self, *args)
            result.interval = TICK_RATE
            return result

        s5p_parameters._lmms_pitch_grid = True
        SynthVEditorGenerator.generate_pitch_delta = s5p_delta
        SynthVEditorGenerator.generate_parameters = s5p_parameters

    from libresvip.plugins.ufdata.ufdata_parser import UFDataParser

    if not getattr(UFDataParser.parse_pitch, "_lmms_absolute_silence", False):
        original_uf_pitch = UFDataParser.parse_pitch

        def uf_pitch(self, pitch, *args):
            curve = original_uf_pitch(self, pitch, *args)
            if pitch.is_absolute:
                # Absolute UFData uses zero as the unvoiced marker. The fixed
                # parser also emits it as a real zero-cent point at transitions.
                curve.points.root = [
                    Point(p.x, -100 if p.y == 0 else p.y) for p in curve.points.root
                ]
            return curve

        uf_pitch._lmms_absolute_silence = True
        UFDataParser.parse_pitch = uf_pitch

    import libresvip.plugins.ccs.cevio_parser as cevio_parser

    if not getattr(cevio_parser.pitch_from_cevio_track, "_lmms_pitch_tempo_basis", False):
        import dataclasses

        original_cevio_pitch = cevio_parser.pitch_from_cevio_track

        def cevio_pitch(data):
            # CCS score tempos are zero-based. Its shared pitch decoder expects
            # tempos including the pitch pre-measure (as VoiSona already uses).
            # Normalize only the pitch decoder's input, keeping score and audio
            # events on the original native timeline.
            return original_cevio_pitch(
                dataclasses.replace(data, tempos=shift_tempo_list(data.tempos, data.tick_prefix))
            )

        cevio_pitch._lmms_pitch_tempo_basis = True
        cevio_parser.pitch_from_cevio_track = cevio_pitch

    from libresvip.model.techno_speech_pitch import TechnoSpeechParamEvent
    from libresvip.plugins.tssln.voisona_parser import VoiSonaParser
    from libresvip.plugins.tsmsln.voisona_mobile_parser import VoiSonaMobileParser

    def techno_param(data):
        if data.value is not None:
            # Index zero is a real first frame, not an omitted relative index.
            return TechnoSpeechParamEvent(data.index, data.repeat or None, float(data.value))

    for parser in (cevio_parser.CeVIOParser, VoiSonaParser, VoiSonaMobileParser):
        parser.parse_param_data = staticmethod(techno_param)

    import math
    from libresvip.plugins.nn.niaoniao_generator import NiaoniaoGenerator, NNPoints

    def nn_pitch(self, simulator, note):
        # NN stores 100 relative values in the range 0..100, with 50 at the
        # note key and sensitivity in semitones. The original writer divided
        # absolute MIDI pitch by sensitivity and omitted the 50-unit scale.
        relative = []
        for index in range(100):
            tick = note.start_pos + round(note.length * index / 99)
            value = simulator.pitch_at_ticks(tick)
            relative.append(0.0 if value is None else value / 100 - note.key_number)
        sensitivity = max(1, min(12, math.ceil(max(map(abs, relative)))))
        points = [max(0, min(100, 50 + round(value * 50 / sensitivity))) for value in relative]
        return NNPoints(points=points), sensitivity - 1

    NiaoniaoGenerator.generate_pitch = nn_pitch

    import libresvip.plugins.ds.diffsinger_converter as ds_converter
    from libresvip.model.reset_time_axis import reset_time_axis
    from libresvip.model.base import SingingTrack, Project, Params, ParamCurve, Points
    from libresvip.plugins.ds.utils.pitch_param_utils import PitchParamUtils, DsParamNode
    from libresvip.utils.music_math import midi2hz

    def ds_millisecond_axis(project):
        # The DS parameter writer indexes milliseconds, not 480-tick beats.
        return reset_time_axis(project, tempo=125)

    def ds_segments(project, min_interval=400, min_length=5000, track_index=-1):
        project = ds_millisecond_axis(project)
        track = (
            project.track_list[track_index]
            if track_index >= 0
            else next(
                (t for t in project.track_list if isinstance(t, SingingTrack) and t.note_list), None
            )
        )
        if not isinstance(track, SingingTrack) or not track.note_list:
            return
        buffer = [track.note_list[0]]
        interval = track.note_list[0].start_pos
        segment_start = max(interval - 600, int(interval * 0.8))

        def segment(notes, preceding_gap, trailing):
            prepare = min(600, int(preceding_gap * 0.8))
            origin = notes[0].start_pos - prepare

            def curve(source):
                return ParamCurve(
                    points=Points(
                        root=[
                            Point.start_point(),
                            *(
                                Point(p.x - origin, p.y)
                                for p in source.points.root
                                if 0 <= p.x - 1920 <= notes[-1].end_pos + 50
                            ),
                            Point.end_point(),
                        ]
                    )
                )

            result = track.model_copy(
                update={
                    "note_list": [
                        n.model_copy(update={"start_pos": n.start_pos - origin}) for n in notes
                    ],
                    "edited_params": Params(
                        pitch=curve(track.edited_params.pitch),
                        gender=curve(track.edited_params.gender),
                    ),
                }
            )
            return origin / 1000, project.model_copy(update={"track_list": [result]}), trailing

        for note in track.note_list[1:]:
            gap = note.start_pos - buffer[-1].end_pos
            if gap >= min_interval and note.start_pos - gap * 0.8 - segment_start >= min_length:
                yield segment(buffer, interval, min(400, int(interval * 0.2)) / 1000)
                segment_start = note.start_pos - min(600, int(gap * 0.8))
                interval = gap
                buffer = []
            buffer.append(note)
        yield segment(buffer, interval, 0.5)

    def ds_pitch_points(cls, points, end):
        valid = [p for p in points.root if 1920 <= p.x < end and p.y >= 0]
        if valid:
            return [
                DsParamNode(time=(x - 1920) / 1000, value=midi2hz(cls.value_at(valid, x) / 100))
                for x in range(1920, end, 5)
            ]

    ds_converter.reset_time_axis = ds_millisecond_axis
    ds_converter.split_into_segments = ds_segments
    PitchParamUtils.encode_point_list = classmethod(ds_pitch_points)

    from libresvip.plugins.musicxml.musicxml_converter import MusicXMLConverter

    if not getattr(MusicXMLConverter.dump, "_lmms_event_positions", False):
        original_musicxml_dump = MusicXMLConverter.dump

        def musicxml_dump(path, project, options):
            import xml.etree.ElementTree as ET
            from decimal import Decimal

            original_musicxml_dump(path, project, options)
            original = path.read_bytes()
            root = ET.fromstring(original)
            tempos = iter(project.song_tempo_list)
            # The fixed serializer groups all directions after the notes. A
            # direction offset is relative to that cursor, in native divisions.
            for part_index, part in enumerate(root.findall("part")):
                measure_start = Decimal(0)
                divisions = Decimal(960)
                for measure in part.findall("measure"):
                    attributes = measure.findall("attributes")
                    if attributes:
                        first = attributes[0]
                        for extra in attributes[1:]:
                            first.extend(list(extra))
                            measure.remove(extra)
                        measure.remove(first)
                        measure.insert(0, first)
                        if first.findtext("divisions") is not None:
                            divisions = Decimal(first.findtext("divisions"))
                    duration = sum(
                        (
                            Decimal(note.findtext("duration", "0"))
                            for note in measure.findall("note")
                            if note.find("chord") is None
                        ),
                        Decimal(0),
                    )
                    for direction in measure.findall("direction"):
                        tempo = next(tempos, None) if part_index == 0 else None
                        if tempo is None:
                            raise ValueError("MusicXML native tempo/direction count differs")
                        position = Decimal(tempo.position) * divisions / 480 - measure_start
                        if not 0 <= position <= duration:
                            raise ValueError("MusicXML tempo is outside its native measure")
                        ET.SubElement(direction, "offset").text = str(position - duration)
                        ET.SubElement(direction, "sound", tempo=str(tempo.bpm))
                    for sound in measure.findall("sound"):
                        if "tempo" in sound.attrib:
                            measure.remove(sound)
                    measure_start += duration
            if next(tempos, None) is not None:
                raise ValueError("MusicXML native writer omitted tempo events")
            # Retain the native writer's XML declaration and MusicXML doctype.
            content = original[: original.index(b"<score-partwise")] + ET.tostring(
                root, encoding="utf-8"
            )
            if path.suffix.lower() == ".mxl":
                container = (
                    b'<?xml version="1.0" encoding="UTF-8"?>'
                    b'<container><rootfiles><rootfile full-path="score.musicxml" '
                    b'media-type="application/vnd.recordare.musicxml+xml"/></rootfiles></container>'
                )
                with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                    archive.writestr("META-INF/container.xml", container)
                    archive.writestr("score.musicxml", content)
            else:
                path.write_bytes(content)

        musicxml_dump._lmms_event_positions = True
        MusicXMLConverter.dump = staticmethod(musicxml_dump)

    from libresvip.plugins.mid.midi_parser import MidiParser
    import libresvip.plugins.mid.midi_parser as midi_parser
    from libresvip.model.relative_pitch_curve import RelativePitchCurve

    class MidiRelativePitchCurve(RelativePitchCurve):
        def to_absolute(self, points, simulator):
            # MIDI zero means a voiced, centered wheel. The shared converter
            # treats exact zero as an interruption; a sub-cent nonzero input
            # keeps continuity and still rounds to exactly the same cents.
            return super().to_absolute(
                [p._replace(y=1e-9) if p.y == 0 else p for p in points], simulator
            )

    midi_parser.RelativePitchCurve = MidiRelativePitchCurve
    if not getattr(MidiParser.parse_track, "_lmms_pitch_cents", False):
        original_midi_track = MidiParser.parse_track

        def midi_track(self, track_index, track):
            bends = [
                event.detail.data
                for event in track
                if event.detail.type == "channel" and event.detail.data.type == "pitchwheel"
            ]
            # The fixed parser multiplies the wheel by sensitivity in semitones,
            # but RelativePitchCurve expects cents. Scale only its parser input;
            # keep the original native 14-bit event unchanged on return/failure.
            for bend in bends:
                bend.pitch *= 100
            try:
                return original_midi_track(self, track_index, track)
            finally:
                for bend in bends:
                    bend.pitch //= 100

        midi_track._lmms_pitch_cents = True
        MidiParser.parse_track = midi_track

    import libresvip.plugins.ust.ust_converter as ust_converter

    if not getattr(ust_converter.render_ust, "_lmms_all_tracks", False):
        original_ust_render = ust_converter.render_ust

        def ust_render(project, path, encoding="utf-8"):
            if len(project.track) <= 1:
                return original_ust_render(project, path, encoding)
            # Keep the native note renderer. Its template only renders track[0],
            # although the frozen grammar supports repeated TRACKEND sections.
            bodies = []
            header = None
            for track in project.track:
                original_ust_render(
                    project.model_copy(
                        update={"track": [track], "track_count": len(project.track)}
                    ),
                    path,
                    encoding,
                )
                text = path.read_bytes().decode(encoding)
                boundary = text.index("\n[#", text.index("Mode2="))
                if header is None:
                    header = text[:boundary]
                bodies.append(text[boundary:].rstrip())
            path.write_bytes((header + "".join(bodies) + "\n").encode(encoding))

        ust_render._lmms_all_tracks = True
        ust_converter.render_ust = ust_render


PREMEASURE_TEMPO_EXPORT = {
    "acep",
    "s5p",
    "svip3",
    "svp",
    "tlp",
    "tlpx",
    "ustx",
    "vsq",
    "vsqx",
    "vspx",
    "xvsq",
    "mtp",
    "ufdata",
}
PREMEASURE_TEMPO_IMPORT = {
    "acep",
    "svip3",
    "svp",
    "tlp",
    "tlpx",
    "ustx",
    "vspx",
    "mtp",
    "tssln",
    "tsmsln",
}


def converter_tempo_basis(project, format_id, exporting):
    """Map native zero-based tempo events to each fixed converter's pre-measure.

    Notes remain zero-based and pitch retains its documented first-bar offset.
    This is an API boundary conversion, never a change to the native song.
    """
    affected = PREMEASURE_TEMPO_EXPORT if exporting else PREMEASURE_TEMPO_IMPORT
    if format_id not in affected:
        return project
    bar = round(project.time_signature_list[0].bar_length())
    # USTX's fixed parser adds a literal 1920, independent of its first meter.
    if not exporting and format_id in {"ustx", "tssln", "tsmsln"}:
        bar = 1920
    tempos = [
        (
            t.model_copy(update={"position": t.position + (bar if exporting else -bar)})
            if index or (not exporting and format_id == "mtp")
            else t.model_copy()
        )
        for index, t in enumerate(project.song_tempo_list)
    ]
    update = {"song_tempo_list": tempos}
    if format_id == "svp":
        # Its fixed generator/parser also skip/add one pre-measure in meter
        # indices. Keep native bar-zero and later meter events where they belong.
        update["time_signature_list"] = [
            (
                meter.model_copy(update={"bar_index": meter.bar_index + (1 if exporting else -1)})
                if index
                else meter.model_copy()
            )
            for index, meter in enumerate(project.time_signature_list)
        ]
    return project.model_copy(update=update)


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
        losses.append(
            {
                "formatId": spec["id"],
                "track": track.title or "未命名轨道",
                "field": field,
                "reason": reason,
            }
        )

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
            raise ValueError(
                f"格式 {spec['id']} 只支持一条{'歌声' if key == 'singingTrack' else '伴奏'}轨，必须显式选择有效轨道。"
            )
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
            loss(
                track,
                "track" if singing else "audio",
                "目标格式不支持此轨道，或此次未选择该轨道；将省略。",
            )
            continue
        if not singing:
            source = pathlib.Path(asset_paths.get(track.audio_file_path, track.audio_file_path))
            if not source.is_absolute() or not source.is_file():
                raise ValueError(f"伴奏 {track.title} 的资源不可用：{source}")
            info = audio_track_info(
                source,
                only_wav=bool(policy.get("audioWavOnly") or policy.get("audioWav44100Pcm16")),
            )
            if info is None:
                raise ValueError(
                    f"伴奏 {track.title} 的音频元数据不可读，或目标格式要求 WAV：{source}"
                )
            if policy.get("audioWav44100Pcm16") and (
                info.sample_rate != 44100 or info.bit_depth != 16
            ):
                raise ValueError(
                    f"格式 {spec['id']} 的伴奏要求 44100 Hz / 16 bit PCM WAV：{track.title}"
                )
            if (
                policy.get("audioExtensions")
                and source.suffix.lstrip(".").lower() not in policy["audioExtensions"]
            ):
                raise ValueError(f"格式 {spec['id']} 不支持伴奏扩展名：{source.suffix}")
        kind = policy["kind"]
        if kind != "project":
            descriptions = {
                "lyrics": "仅导出歌词和时间，不包含可编辑音符、音高曲线及轨道混音状态。",
                "notation": "导出乐谱音符；原始编辑音高曲线及轨道混音状态不保留。",
                "graphic": "仅导出可视化图形，不是可编辑歌声工程。",
                "parameters": "导出合成参数文件，不是完整可编辑工程；轨道混音状态不保留。",
            }
            loss(track, "project", descriptions[kind])
        fields = set(
            policy.get("fieldsLost", [])
            + policy.get("readbackFieldsLost", [])
            + (
                policy.get("singingFieldsLost", [])
                if singing
                else policy.get("audioReadbackFieldsLost", [])
            )
        )
        for field in fields:
            value = getattr(track, field)
            if field == "title" or value != {
                "volume": 1.0,
                "pan": 0.0,
                "mute": False,
                "solo": False,
            }.get(field):
                loss(
                    track,
                    field,
                    "固定版本转换链不能完整往返该轨道字段；写出或读回将使用目标默认值或重建值。",
                )
        if singing:
            if policy.get("pitchReadbackReason") and any(
                p.y != -100 for p in track.edited_params.pitch.points.root
            ):
                loss(track, "pitch", policy["pitchReadbackReason"])
            if policy.get("pronunciationReadbackReason") and any(
                n.pronunciation for n in track.note_list
            ):
                loss(track, "pronunciation", policy["pronunciationReadbackReason"])
            if policy.get("lyricsReadbackReason") and track.note_list:
                loss(track, "lyric", policy["lyricsReadbackReason"])
        grid = policy.get("noteTickGrid", 1)
        if spec["id"] == "nn":
            grid = 60 if effective.get("version") == 19 else 30
        if (
            singing
            and grid > 1
            and any(n.start_pos % grid or n.length % grid for n in track.note_list)
        ):
            loss(track, "noteTime", f"目标格式音符时间按 {grid} LibreSVIP tick 网格量化。")
        tracks.append(track)
    if not tracks:
        raise ValueError("所选格式没有可导出的轨道")
    if policy.get("firstTempoOnly") and len({t.bpm for t in project.song_tempo_list}) > 1:
        losses.append(
            {
                "formatId": spec["id"],
                "track": "全局",
                "field": "tempo",
                "reason": "目标格式只记录一个 BPM；原工程的完整变速事件不能保留。",
            }
        )
    if policy.get("firstMeterOnly") and len(project.time_signature_list) > 1:
        losses.append(
            {
                "formatId": spec["id"],
                "track": "全局",
                "field": "meter",
                "reason": "固定转换器只写入首个拍号；后续拍号变化不保留。",
            }
        )
    if policy.get("fixedMeter") and any(
        t.numerator != 4 or t.denominator != 4 for t in project.time_signature_list
    ):
        losses.append(
            {
                "formatId": spec["id"],
                "track": "全局",
                "field": "meter",
                "reason": "固定转换链使用 4/4 拍号；原拍号结构不保留。",
            }
        )
    if policy.get("maxBars"):
        limit = project.time_signature_list[0].bar_length() * policy["maxBars"]
        if any(
            n.end_pos > limit for t in tracks if isinstance(t, SingingTrack) for n in t.note_list
        ):
            raise ValueError(f"格式 {spec['id']} 最多 {policy['maxBars']} 小节；拒绝静默截断音符。")
    if policy.get("sharedAudioMix") and len(keep_audio) > 1:
        audio = [project.track_list[i] for i in sorted(keep_audio)]
        for track in audio[1:]:
            if any(getattr(track, f) != getattr(audio[0], f) for f in ("volume", "mute", "solo")):
                loss(track, "audioMix", "目标格式全部伴奏共享首条伴奏的音量、静音及独奏状态。")
    if singing_limit == 1 and keep_singing and "track_index" in effective:
        effective["track_index"] = next(
            i for i, t in enumerate(tracks) if isinstance(t, SingingTrack)
        )
    projected = project.model_copy(update={"track_list": tracks})
    return projected, effective, losses


def separate_unvoiced_controller_gaps(project, format_id):
    """Keep native continuous controllers from becoming voiced gap curves.

    Relative controller formats reconstruct an absolute curve over an entire
    part, including the silence between notes. Intersect that result with the
    union of sounding note intervals; preserve existing interruptions and every
    original point inside notes. Never manufacture a missing pitch trajectory.
    """
    if format_id not in {"ds", "dv", "mid", "svp", "ustx", "vpr", "vspx", "vsq", "vsqx", "xvsq"}:
        return
    import bisect
    from libresvip.model.base import SingingTrack
    from libresvip.model.point import Point

    prefix = round(project.time_signature_list[0].bar_length())
    for track in project.track_list:
        if not isinstance(track, SingingTrack):
            continue
        intervals = []
        for note in sorted(track.note_list, key=lambda n: n.start_pos):
            start, end = note.start_pos + prefix, note.end_pos + prefix
            if intervals and start <= intervals[-1][1]:
                intervals[-1][1] = max(end, intervals[-1][1])
            else:
                intervals.append([start, end])
        segments, current = [], []
        for point in track.edited_params.pitch.points.root:
            if point.y == -100:
                if current:
                    segments.append(sorted(current, key=lambda p: p.x))
                    current = []
            else:
                current.append(point)
        if current:
            segments.append(sorted(current, key=lambda p: p.x))
        clipped = []
        for segment in segments:
            xs = [p.x for p in segment]

            def at(x):
                index = bisect.bisect_right(xs, x) - 1
                left = segment[index]
                if left.x == x or index == len(segment) - 1:
                    return Point(x, left.y)
                right = segment[index + 1]
                return Point(
                    x, round(left.y + (right.y - left.y) * (x - left.x) / (right.x - left.x))
                )

            for note_start, note_end in intervals:
                start, end = max(note_start, xs[0]), min(note_end, xs[-1])
                if start > end:
                    continue
                points = [at(start)]
                points.extend(segment[bisect.bisect_right(xs, start) : bisect.bisect_left(xs, end)])
                if end > start:
                    points.append(at(end))
                clipped.append([Point(start, -100), *points, Point(end, -100)])
        if clipped:
            clipped.sort(key=lambda points: points[0].x)
            track.edited_params.pitch.points.root = [
                Point.start_point(),
                *(point for segment in clipped for point in segment),
                Point.end_point(),
            ]


def process(request, workspace):
    global SOURCE_PROJECT_PATH
    if (
        request.get("protocol") != 1
        or not isinstance(request.get("requestId"), str)
        or not request["requestId"]
    ):
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
            SOURCE_PROJECT_PATH = path
            try:
                project = converter_tempo_basis(converter.load(source, effective), format_id, False)
                separate_unvoiced_controller_gaps(project, format_id)
            finally:
                SOURCE_PROJECT_PATH = None
            resources = []
            losses = []
            if effective.get("extract_audio") is False:
                losses.append(
                    {
                        "formatId": format_id,
                        "track": "全局",
                        "field": "audio",
                        "reason": "已关闭嵌入伴奏提取；源工程中的嵌入音频不会导入。",
                    }
                )
            for index, track in enumerate(project.track_list):
                policy = spec.get("exportPolicy", {})
                fields = set(
                    policy.get("fieldsLost", [])
                    + policy.get("readbackFieldsLost", [])
                    + (
                        policy.get("singingFieldsLost", [])
                        if isinstance(track, SingingTrack)
                        else policy.get("audioReadbackFieldsLost", [])
                    )
                )
                for field in sorted(fields):
                    losses.append(
                        {
                            "formatId": format_id,
                            "track": track.title or str(index + 1),
                            "field": field,
                            "reason": "固定转换链不能完整往返该字段；解析结果可能是默认值或重建值，无法确认原值已保留。",
                        }
                    )
                if isinstance(track, SingingTrack) and track.note_list:
                    for field, key in (
                        ("pitch", "pitchReadbackReason"),
                        ("pronunciation", "pronunciationReadbackReason"),
                        ("lyric", "lyricsReadbackReason"),
                    ):
                        if policy.get(key):
                            losses.append(
                                {
                                    "formatId": format_id,
                                    "track": track.title or str(index + 1),
                                    "field": field,
                                    "reason": policy[key],
                                }
                            )
                if (
                    isinstance(track, SingingTrack)
                    and track.note_list
                    and not any(
                        point.y != -100 and -192000 < point.x < 1073741823
                        for point in track.edited_params.pitch.points.root
                    )
                ):
                    losses.append(
                        {
                            "formatId": format_id,
                            "track": track.title or str(index + 1),
                            "field": "pitch",
                            "reason": "解析器未提供原工程的有效音高曲线；如源工程含弯音或颤音，无法确认其已保留。仅音符导入不计为音高保真。",
                        }
                    )
                if isinstance(track, InstrumentalTrack):
                    audio = pathlib.Path(track.audio_file_path)
                    if not audio.is_absolute():
                        extracted = source.parent / audio
                        audio = extracted if extracted.is_file() else path.parent / audio
                    elif not audio.is_file() and contained(audio, workspace):
                        audio = path.parent / audio.relative_to(source.parent)
                    track.audio_file_path = str(audio.resolve())
                    resources.append(
                        {
                            "path": track.audio_file_path,
                            "temporary": contained(audio, workspace),
                            "exists": audio.is_file(),
                        }
                    )
            result = {
                "project": project.model_dump(mode="json", by_alias=False),
                "resources": resources,
                "losses": losses,
            }
        elif operation in {"inspectExport", "exportProject"}:
            if not spec["canExport"]:
                raise ValueError("Format is import-only")
            if operation == "exportProject" and not contained(path, workspace):
                raise ValueError("Export path must be staged in the task directory")
            project = Project.model_validate(request["project"])
            project, effective, losses = prepare_export(
                project, spec, options, request.get("selection", {}), request.get("assets", [])
            )
            if operation == "inspectExport":
                return {
                    "project": project.model_dump(mode="json", by_alias=False),
                    "options": effective,
                    "losses": losses,
                    "warnings": [],
                }
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
                if (
                    not isinstance(name, str)
                    or not name
                    or name in {".", ".."}
                    or pathlib.Path(name).name != name
                    or pathlib.PureWindowsPath(name).name != name
                    or ":" in name
                    or name.casefold() == path.name.casefold()
                    or name.casefold() in names
                ):
                    raise ValueError("Invalid companion audio filename")
                source = pathlib.Path(asset.get("source", ""))
                if (
                    not source.is_absolute()
                    or not source.is_file()
                    or source.stat().st_size > MAX_INPUT
                ):
                    raise ValueError("Companion audio missing or exceeds size limit")
                total += source.stat().st_size
                if total > MAX_UNPACK:
                    raise ValueError("Companion audio group exceeds size limit")
                shutil.copy2(source, path.parent / name)
                names.add(name.casefold())
            os.chdir(path.parent)
            project = converter_tempo_basis(project, format_id, True)
            if format_id == "acep" and path.suffix.lower() == ".acet":
                # ACET is an archive containing an ACEP project, not ACEP bytes
                # with another extension. The fixed parser expects this container.
                inner = path.parent / "__lmms-embedded.acep"
                if inner.exists():
                    raise ValueError("Companion conflicts with ACET container staging")
                try:
                    converter.dump(inner, project, effective)
                    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                        archive.write(inner, "project.acep")
                        for name in names:
                            source = next(
                                p for p in path.parent.iterdir() if p.name.casefold() == name
                            )
                            archive.write(source, source.name)
                finally:
                    if inner.exists():
                        inner.unlink()
            else:
                converter.dump(path, project, effective)
            if format_id == "vsqx" and names:
                # Native VSQX resolves relative WAVs in <project>.wavparts.
                wavparts = path.with_suffix(".wavparts")
                wavparts.mkdir(exist_ok=True)
                for name in names:
                    audio = next(
                        p
                        for p in path.parent.iterdir()
                        if p.is_file() and p.name.casefold() == name
                    )
                    audio.rename(wavparts / audio.name)
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
    result["warnings"] = [
        {"formatId": format_id, "message": message} for message in caught.output.splitlines()
    ]
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
        response["error"] = {
            "code": "conversionFailed",
            "message": str(exc),
            "detail": type(exc).__name__,
        }
    response.update(
        {"requestId": request.get("requestId", ""), "formatId": request.get("formatId", "")}
    )
    output = json.dumps(response, ensure_ascii=False, allow_nan=False).encode("utf-8")
    if len(output) > MAX_JSON:
        response = {
            "protocol": 1,
            "dataVersion": 1,
            "requestId": request.get("requestId", ""),
            "formatId": request.get("formatId", ""),
            "status": "error",
            "warnings": [],
            "losses": [],
            "error": {"code": "resultTooLarge", "message": "Result exceeds JSON limit"},
        }
        output = json.dumps(response).encode("utf-8")
    sys.stdout.buffer.write(output)
    sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
