"""Deterministic native VSPD fixture built from the frozen binary specification.

This is a generated format sample, not an externally authored VocalShifter song.
It contains two notes, a real silence gap and edited non-flat pitch controls.
"""
import pathlib
import sys


def write_fixture(path):
    sys.dont_write_bytecode = True
    from libresvip.plugins.vshp.model import (
        VocalShifterProjectData, VocalShifterPatternHeader,
        VocalShifterProjectMetadata, VocalShifterTrackMetadata,
        VocalShifterPatternMetadata,
    )
    header = dict.fromkeys(("sample_count sample_rate channels points_per_second points_count time_count "
                           "analyze_params analyze_prm1 analyze_prm2 analyze_prm3 synth_mode attack_threshold "
                           "fix_threshold fix_threshold_dyn synth_option fade_in_sample fade_out_sample "
                           "spectrum_key_min spectrum_key_scale spectrum_points_density spectrum_points_per_group "
                           "spectrum_points_count spectrum_unknown notes_count label_count").split(), 0)
    header.update(size=VocalShifterPatternHeader.sizeof() - 8, sample_count=66150, sample_rate=44100,
                  channels=1, pattern_type="MIDI", points_per_second=100, points_count=151, notes_count=2,
                  eq1=[0]*16, eq2=[0]*16, heq=[0]*16)
    for number, size in enumerate((10, 2, 2, 56, 2, 8), 1):
        header["padding" + str(number)] = bytes(size)
    points = []
    for index in range(151):
        value = (6000 + index % 11 if index < 50 else
                 6200 + index % 17 if 75 <= index < 125 else 0)
        point = dict.fromkeys(("fix_org fix_edit ori_pit pit frm bre brightness clearness ori_dyn dyn vol pan "
                               "sdyn dsdyn unknown heq").split(), 0)
        point.update(size=96, pit_analyze=[0]*4, wave=[0]*4, pit=value,
                     ori_pit=6000 if value else 0, dyn=1.0, ori_dyn=1.0, vol=1.0, padding=bytes(12))
        points.append(point)
    project = {
        "version": b"1.000\0\0\0", "size": 0,
        "project_metadata": dict(size=VocalShifterProjectMetadata.sizeof()-8, track_count=1, pattern_count=1,
            save_option=0, vslib_version=0, sample_rate=44100, numerator=4, denominator=4,
            padding1=bytes(4), tempo=120.0, master_volume=1.0, is_float=0, bit_depth=16, channels=1, reserved=bytes(196)),
        "track_metadatas": [dict(size=VocalShifterTrackMetadata.sizeof()-8, name=b"native fixture",
            volume=.75, pan=-.25, mute=0, solo=1, sel_flg=0, color="Blue", invert_flg=0,
            morphing_group=0, option=0, reserved=bytes(148))],
        "pattern_metadatas": [dict(size=VocalShifterPatternMetadata.sizeof()-8, path_and_ext=b"fixture.mid"+bytes(245),
            id=1, sel_flg=0, track_num=0, offset_add=0, offset=0.0, meas_offset=0.0, freq_a4=440.0,
            key=0, tune=0, user_tune=bytes(12), base_freq_key=0, base_freq_scale=0, base_freq=440.0,
            key_type=0, padding=bytes(3), option=0, reserved=bytes(176))],
        "pattern_datas": [dict(size=0, header=header, points=points,
            start_time=dict(size=16, original_time=0.0, time=0.0),
            end_time=dict(size=16, original_time=1.5, time=1.5), spectrum=None,
            notes=dict(size=64, notes=[dict(start_tick=0, length=100, pitch=6000, flag=0, padding=bytes(21)),
                                      dict(start_tick=150, length=100, pitch=6200, flag=0, padding=bytes(21))]), labels=None)],
    }
    payload = VocalShifterProjectData.build(project)
    project["size"] = len(payload)-16
    project["pattern_datas"][0]["size"] = len(payload)-16-VocalShifterProjectMetadata.sizeof()-VocalShifterTrackMetadata.sizeof()-VocalShifterPatternMetadata.sizeof()-8
    pathlib.Path(path).write_bytes(VocalShifterProjectData.build(project))
