# LMMS enhanced development branch

This fork is maintained as an independent development branch and will continue to sync updates from upstream [LMMS](https://github.com/LMMS/lmms). All branch enhancements listed below were developed with AI assistance.

The comparison uses this fork's inherited upstream baseline (`a2f57e70c`), rather than making claims about future upstream releases. The original upstream README is preserved below.

| Area | Inherited upstream baseline | Enhancements in this branch |
| --- | --- | --- |
| Local automation API | Interactive project editing and the existing LMMS command-line workflow | An optional command API for project queries, track/clip/note edits, instruments, mixing, automation, playback and export, with serialized transactions, undo and rollback. [Command reference](data/agent/command-reference.md) |
| Composition scripts | Existing editing and arrangement tools | Local JSON scripts, built-in composition helpers, reproducible seeds, assertions and dry-run diffs. Scripts use the same project command bus. [Local tools](data/agent/README.md) |
| MCP access | No branch-specific local MCP endpoint | An optional bearer-authenticated loopback HTTP MCP server, tool discovery and calls, settings controls, audio previews and exports. It is disabled at runtime by default. [MCP guide](data/agent/MCP.md) |
| Windows VST2 hosting | Existing VST2 instruments/effects and remote helpers | Supervised x86/x64 helper processes, crash/hang containment, bounded realtime communication, shell identities and native editor/parameter lifecycle fixes. [Validation](VST3_SUPPORT_VALIDATION.md) |
| Windows VST3 hosting and discovery | Existing VST2 instrument/effect entry points | Native VST3 through VeSTige and VstEffect, multiple search roots, categorized discovery, multi-class modules and WaveShell selection; verified Kontakt/editor compatibility fixes. [Progress and scope](VST3_SUPPORT_PROGRESS.md) |
| Singing voice tracks | MIDI, sample, pattern and automation tracks | A dedicated SVS track, independent piano editor, capability-driven voice/language/parameter controls, pronunciation dictionaries, phoneme editing, pitch curves and read-only synthesis references. [SVS validation](doc/svs/M1-validation.md) |
| SVS plugin development | No branch-specific SVS plugin ABI | A standalone native C ABI / C++ SDK, optional engine settings and catalog/resource queries, examples and conformance tooling, preserving ABI 1.0–1.3 compatibility. [SDK](sdk/svs/README.md) |
| DiffSinger | No native DiffSinger engine in this baseline | Native CPU/DirectML duration, pitch, variance, acoustic and vocoder inference; external voicebank discovery and a complete plugin dependency folder. Six local voicebanks validated. Voicebanks are grouped by engine and scanned asynchronously without blocking the interface. Global shared vocoder roots support multiple nested packages, with bundled vocoders taking priority. [A0–A4 results](doc/svs/DiffSinger-A4-validation.md) |
| SVS rendering and cache | Existing general audio rendering | Incremental rendering split at empty beats/rests, reuse of unaffected segments, current/total progress in Song Editor, frozen complete exports, and SHA256-named audio/tensor caches under `cache/SVS`. [Segment validation](doc/svs/SVS-segment-rendering-validation.md) |
| SVS visual editing | Existing MIDI piano and clip display | Per-parameter curve colors, pronunciation above notes with original lyrics inside, optional voice pitch-range shading/text, phoneme-prefix display controls, thicker note waveforms and optional Song Editor background waveforms. |
| SVS pitch recordings | No SVS automatic pitch recording workflow | Any AI voice declaring automatic pitch prediction can record new takes, use random or manually fixed seeds, switch saved takes and show pronunciation at the note's upper left with the take number at its upper right. Continuous predicted phrases retain their transitions. |
| Runtime paths | Existing working-directory configuration | Windows defaults use `lmms-workspace/` beside the executable; bundled paths are saved relatively and resolved against the executable directory. SVS caches follow the configured working directory. |
| Interface and themes | Original LMMS controls, canvases and plugin panels | Compact flat controls, vector state assets, rounded clips, modernized editor/host panels and native-window validation, retaining existing fixed instrument artwork. [UI acceptance](doc/ui-modernization/acceptance.md) |
| Piano-roll guides and notation | Standard pitch labels and existing MIDI editing controls | Optional pitch/time alignment guides, standard pitch/frequency hover labels and enlarged numbered notation with octave dots. Global tonic/reference defaults plus temporary per-window reference overrides; keyboards retain standard pitch labels. |
| Editor toolbar overflow | Separate tool groups with native overflow | Controls in the same row share an expandable overflow area; hidden dropdowns remain directly usable. Song Editor and both piano rolls use a theme-configurable SVG overflow icon. |
| SVS synthesis feedback | No singing synthesis status strip | A 4-pixel time-aligned strip above the note grid: yellow while pending/rendering, green for completed audio, red for errors, with a moving shimmer during rendering. All three colors are configurable in `style.css`; full status/diagnostics remain available on the ruler tooltip. |
| SVS host volume | No engine-independent singing volume curve | Editable host volume automation works across SVS engines independently of voicebank parameters, including playback and export. |
| Singing project interchange | Existing MIDI/project import and export | Isolated LibreSVIP conversion, editable singing notes/pitch, retained accompaniment audio, format-aware import/export defaults and preflight checks. Availability depends on the deployed conversion runtime and format capabilities. |
| Singing voice conversion | No branch-specific SVC tracks or ABI | Independent progressive SVC audio tracks, segmentation and paired caches, a versioned streaming SDK, an RVC API backend, capability-driven controls and A/B audition. Audio clips can be renamed, copied as slices and converted individually. [SVC SDK](sdk/svc/README.md) |
| Language coverage | Inherited upstream translation catalogs | Completed Chinese, Japanese, English and Korean coverage for branch features, host panels and native plugin interfaces, using shared translation catalogs and native-window checks. Voicebank README viewing is available from the SVS sidebar. |
| Clip transport | Existing song and clip playback | MIDI/SVS clip previews share transport behavior and follow the Song Editor timeline within clip boundaries; muted singing tracks suspend synthesis. |

Current release validation targets Windows x64, with x86/x64 VST helpers. SVS AI engines can share native CPU/DirectML workers with global backend/device and model memory settings. Idle release defaults to 60 seconds; immediate release and persistent models are also available. Actual per-stage routes, CPU constraints and fallback are reported. Six DiffSinger voicebanks and both available GPUs have passed inference comparisons. [GPU validation](doc/svs/DiffSinger-B3-validation.md). The linked records distinguish automated results from pending listening, external-runtime and visual checks.

## Interaction references

Some SVS editor interaction and presentation logic was informed by [TuneLab](https://github.com/LiuYunPlayer/TuneLab), including note/phoneme boundary stretching, curve editing, parameter-area arrangement, numbered-notation tonic selection and the time-aligned synthesis status strip with a looping shimmer. The references include the locally optimized TuneLab implementation under `refs/tl_ref`; those local changes should not be attributed to every upstream TuneLab release. LMMS uses its own Qt widgets, project model, synthesis scheduler and theme properties to implement these behaviors.

Singing project format conversion uses [LibreSVIP](https://github.com/SoulMelody/LibreSVIP), while the original LMMS codebase remains the foundation for arrangement, MIDI, instruments and mixing. These references describe the sources of specific behaviors and integrations, rather than claiming full feature parity with the referenced projects.

The synthesis strip colors can be customized in the theme's `lmms--gui--SVSPianoRoll` rule using `qproperty-synthesisRenderingColor`, `qproperty-synthesisReadyColor` and `qproperty-synthesisErrorColor`. The toolbar icon is configured by `qproperty-icon` in the `QToolBar QToolButton#qt_toolbar_ext_button` rule.

## Original upstream README

---

<div align="center">
	<h1>
	<img src="https://raw.githubusercontent.com/LMMS/artwork/master/Icon%20%26%20Mimetypes/lmms-64x64.svg" alt="LMMS Logo"><br>LMMS
	</h1>
	<p>Cross-platform music production software</p>
	<p>
		<a href="https://lmms.io/">Website</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/releases">Releases</a>
		⦁︎
		<a href="https://lmms.io/documentation">User manual</a>
		⦁︎
		<a href="https://lmms.io/showcase/">Showcase</a>
		⦁︎
		<a href="https://lmms.io/lsp/">Sharing platform</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/wiki">Developer wiki</a>
		⦁︎
		<a href="https://lmms.github.io/lmms">Internal documentation</a>
	</p>
	<p>
		<a href="https://github.com/LMMS/lmms/actions/workflows/build.yml"><img src="https://github.com/LMMS/lmms/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
		<a href="https://lmms.io/download"><img src="https://img.shields.io/github/release/LMMS/lmms.svg?maxAge=3600" 	alt="Latest stable release"></a>
		<a href="https://github.com/LMMS/lmms/releases"><img src="https://img.shields.io/github/downloads/LMMS/lmms/total.svg?maxAge=3600" alt="Overall downloads on Github"></a>
		<a href="https://discord.gg/3sc5su7"><img src="https://img.shields.io/badge/chat-on%20discord-7289DA.svg" alt="Join the chat at Discord"></a>
		<a href="https://www.transifex.com/lmms/lmms/"><img src="https://img.shields.io/badge/localise-on_transifex-green.svg"></a>
	</p>
</div>

What is LMMS?
--------------

LMMS is an open-source cross-platform digital audio workstation designed for music production. It includes an advanced Piano Roll, Beat Sequencer, Song Editor, and Mixer for composing, arranging, and mixing music. It comes with 15+ synthesizer plugins by default, along with VST2 and SoundFont2 support.

Features
---------

* Song-Editor for arranging melodies, samples, patterns, and automation
* Pattern-Editor for creating beats and patterns
* An easy-to-use Piano-Roll for editing patterns and melodies
* A Mixer with unlimited mixer channels and arbitrary number of effects
* Many powerful instrument and effect-plugins out of the box
* Full user-defined track-based automation and computer-controlled automation sources
* Compatible with many standards such as SoundFont2, VST2 (instruments and effects), LADSPA, LV2, GUS Patches, and full MIDI support
* MIDI file importing and exporting

Building
---------

See [Compiling LMMS](https://github.com/LMMS/lmms/wiki/Compiling)

Join LMMS-development
----------------------

If you are interested in LMMS, its programming, artwork, testing, writing demo songs, (and improving this README...) or something like that, you're welcome to participate in the development of LMMS!

Information about what you can do and how can be found in the [wiki](https://github.com/LMMS/lmms/wiki).

Before coding a new big feature, please _always_ [file an issue](https://github.com/LMMS/lmms/issues/new) for your idea and suggestions about your feature and about the intended implementation on GitHub, or ask in one of the tech channels on Discord and wait for replies! Maybe there are different ideas, improvements, or hints, or maybe your feature is not welcome/needed at the moment.
