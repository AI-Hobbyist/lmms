# Fixed-converter losses

Reference: LibreSVIP `e33dc2824453a3104ee2b0a9cd836dce8452ce31`. Vendor source and locked wheels remain unchanged. Integration adapters are in `tools/svs-project/bridge.py`.

The native LMMS mapper keeps M0's unchanged numeric limits. A target converter's resampling, omitted fields or integer mapping is recorded as a loss, never as a full edited-pitch PASS. The real matrix records exact errors and missing coverage in `M4/direction-report.json`. `M4-validation.md` separates actual input/output success from strict pitch fidelity. `M4/fidelity-validation.json` checks each observed semantic difference against a loss disclosed before writing; it does not enlarge the tolerances.

| Conversion chain | Source evidence in fixed reference | Disclosed pitch restriction |
| --- | --- | --- |
| ACEP / ACET | `acep/ace_studio_generator.py:generate_pitch_curves`, `acep/ace_studio_parser.py:parse_pitch_curve` | One-tick delta samples reconstructed against simulated base pitch and integer cents; small controls can change. |
| AiSingers | `aisp/aisingers_generator.py:generate_pitch`, `aisp/aisingers_parser.py:parse_notes` | 500 samples per note; writer truncates sampled tick positions and reader rounds ticks/cents. Note times use a 15-tick native grid. |
| CCS / VoiSona / mobile | `model/techno_speech_pitch.py:TIME_UNIT_AS_TICKS_PER_BPM`, `normalize_to_tick`, `pitch_from_techno_speech_track` | Five-millisecond frame axis, then integer tick mapping; endpoint coverage and small controls can change. |
| DS | `ds/utils/pitch_param_utils.py`, `ds/diffsinger_parser.py`; bridge millisecond-axis adapter | Five-millisecond F0 samples, one decimal Hz; not the original control-point representation. |
| NN | `nn/niaoniao_generator.py`, `nn/niaoniao_parser.py`; bridge relative-wheel adapter | 100 integer bend samples per note, whole-semitone sensitivity capped at 12; native note grid is 60 ticks in version 19, otherwise 30. |
| PocketSinger | `ps_project/pocket_singer_parser.py:parse_notes` | Seconds and relative bends truncated to integer ticks/cents. |
| UST | `ust/ust_generator.py:generate_track`, `ust/pitch_mode1.py`, `ust/pitch_mode2.py` | Native mode sampling/interpolation and integer controls can alter small edits. |
| VOXFactory | `vfp/vox_factory_generator.py:generate_note_pitch` | 1024/44100-second pitch frames; coverage and small controls can change. |
| Vogen | `vog/vogen_generator.py` | Writer does not serialize the original edited-pitch curve. |
| VSPX | `vspx/vspx_generator.py:generate_pitch`, `vspx/vspx_parser.py:parse_pitch` | Integer relative cents reconstructed against base pitch. |
| VOICEVOX | `vvproj/voicevox_generator.py:generate_pitch` | 4/375-second frequency frames; coverage and small controls can change. |
| VXF | `vxf/vx_beta_generator.py:generate_track` | 1/200-second frames with truncated frame indices. |
| Y77 | `y77/y77_generator.py:generate_pitch` | 500 samples per note, integer tick mapping and sensitivity capped at 12 semitones; native 30-tick note grid. |

Paths in this table are relative to `refs/LibreSVIP/libresvip/`. These are restrictions of the frozen conversion chain, not claims that every native application is incapable of greater precision. The measured failures remain visible. No original-pitch failure is relabeled a full fidelity PASS merely because a curve is nonflat.

`export-policy.json` adds named readback restrictions to the existing source-based cardinality rules. Every changed title/mute/solo/volume/pan, lyric and pronunciation in the direction and accompaniment fixtures must be covered before acceptance. Per-track singing/audio losses are separate; ACE's shared accompaniment mix is explicitly named. Single-tempo or fixed/first-meter converters disclose those restrictions, and NN/Y77/AiSingers disclose note-time grids. Notation, lyric, graphic and parameter outputs retain their explicit output-kind notices.

The bridge supplies the same restrictions to import results. A valid-looking default state or reconstructed reading is not proof that the original field survived. Exports require explicit loss acceptance, and the native controller presents import losses before committing a replacement song.

Six missing upstream notices were retained alongside exact locked-wheel metadata. Technical dependency deployment is checked independently. jyutping's metadata-only MIT declaration and wanakana-python's conflicting upstream MIT / wheel MPL-2.0 declarations remain `MANUAL/PENDING` upstream notice reviews; original declarations and canonical MPL text are preserved without inventing or replacing license grants.
