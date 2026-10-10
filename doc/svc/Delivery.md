# SVC Windows delivery

The development executable is `build/Release/lmms.exe`. Loadable plugins, including `svcrvc.dll`, are deployed directly to `build/Release/plugins`; validation uses this same directory and the configured development theme.

## Incremental replacement

Use the existing `buildtools/New-LmmsReplacementPackage.ps1` with the full baseline `build/packages/lmms-enhanced-full-236c5f3fe-win64.zip.manifest.json`, matching the existing incremental package chain. The installer verifies the archive hashes and unchanged baseline files. Keep the original installation as a rollback source; close LMMS before replacement. Extract the ZIP and run `Install-Replace.cmd`, selecting the directory containing `lmms.exe`. Install the required full baseline first if it is absent or verification fails. Personal configuration, audio, projects, models and conversion caches are not packaged.

The incremental payload includes the rebuilt executable/plugin ABI dependencies, RVC module and SVC theme resources. This is an API frontend; it does not include or modify the RVC server, Python inference environment or model weights. Start the RVC server separately and configure its address in SVC settings.

## Connection and operation

Connections and conversions run asynchronously. Silence analysis and splitting, input preparation, upload, inference, streaming reception and cache writes execute on the conversion worker; only clip metadata and progress updates return to the GUI. Clips are queued sequentially, with cancellation and stale-generation rejection.

A disconnected engine category is hidden from the browser while its settings remain available. Global reconnection settings apply to all SVC engines: interval (default 5 seconds) and maximum retries (default 3). Start automatic reconnection launches one bounded round for all disconnected engines; connected engines are skipped. Exhaustion stops the round; press again to retry. Individual Test connection / refresh also starts asynchronous discovery. The SVC track icon has a transparent background.

The local test address is `http://127.0.0.1:8000`, with an empty token. Test/refresh the connection to discover models, weights, compatible indexes and available methods. For production, require Bearer authentication on the server and enter the token in SVC settings. Optional persistence uses Windows Credential Manager; credentials are excluded from projects, cache identities and logs.

Conversion parameters belong to the track. All its audio clips use the same selection and splitting settings, with independent source and result caches. The clip selector switches waveform and A/B audition. Both the plugin Re-render button and the track context-menu Re-render entry queue all clips on the track. The clip context-menu Re-render entry queues only the clicked clip. Parameter edits retain previous results until rerender replaces corresponding regions. Use separate SVC tracks for different clip conversion settings.

SVS and SVC clip context menus include Change name, using the existing LMMS rename dialog. Names are stored in projects. The SVC settings tab scrolls its contents so the settings window can be resized vertically while all engine settings remain reachable.

RVC uses a single model voice (speaker ID 0). Numeric API parameters use sliders with adjacent editable values. When API pitch_shift bounds are missing, the initial slider range is -24..+24; numeric entry extends it and actual API bounds take precedence. `resample_sr=0` selects the model rate; 1..15999 is invalid. Unsupported F0 parameters are omitted for non-F0 weights, and index processing is disabled when no compatible index exists. Missing B is silent in B-only audition; ordinary track playback uses ready B with A fallback. Incomplete conversions block final export.

New SVC tracks use the selected model name; custom track names are preserved. SVC and SVS have independent sidebar pages with instrument-style search and engine groups directly at the top level. SVS voices retain their existing avatars/icons and track creation behavior. The old instrument-browser SVS category and empty-track toolbar entry are removed. Sidebar icon orientation follows the existing LMMS sidebar convention.

Automated tests cover protocol, numerical playback/mapping, persistence and native Windows windows. Artificial sine-wave API runs validate transport and publication, not voice quality. Human voice A/B listening remains MANUAL/PENDING and is non-blocking under the plan.

Sample/audio clip menus include **Copy current clip to SVC track**, with an existing SVC track submenu. The original clip remains intact. Position, length, crop offset, clip name, mute and color are copied; recorded/embedded and reversed samples are saved as a persistent 32-bit float WAV under the working samples directory `samples/svc`. Target engine/model parameters remain unchanged. The copy starts with source audio and requires explicit rerender. Keep this source WAV with the project when moving it.

SVC and SVS track headers use the standard 8×28 activity indicator. SVC pulses during audio playback. SVS uses the instrument indicator's note-on/hold/note-off behavior, following the note sequence, including overlapping notes and rests; pause, stop, seek and track mute release held activity. Both track gear menus and mixer channel number context menus support assignment to an existing mixer channel or creation of a new channel named/colored after the track. SVS clip menus include **Set as ghost in piano-roll**; the existing instrument piano roll displays a snapshot of notes inside the selected clip's cropped range. Setting another projection replaces it, and the existing Clear ghost notes button removes it.


SVS piano-roll bars, beats and quantization use clip content time starting at bar 1, matching the instrument piano roll. Moving a clip does not change its ruler/grid. Song playback translates through clip position and crop offset. Ruler clicks now change only the shared note-clip preview timeline; they do not move the Song timeline. Playback cursor and automatic scrolling follow only while Song playback is inside the clip. Starting the song from the beginning enters each clip at its corresponding content time.


SVS internal Play / Pause now uses the existing note-clip preview transport, like the instrument piano roll. It plays only the selected clip with its crop range and rendered audio; another overlapping clip is excluded. The standard TimeLineWidget supplies ruler interaction, loop-point editing, auto-scroll modes and stop-position controls. Preview transport, pause/resume, looping, stopping and ruler clicks leave the Song timeline position unchanged, including when switching from running Song playback. There is one active playback engine: entering preview stops Song playback, preserving its marker. Song Editor playback still drives SVS clip-relative accompaniment scrolling. Deleting the previewed clip stops its playback safely.

In the SVS note/parameter canvas, Space plays/stops the current clip and Shift+Space pauses/resumes, matching the original piano-roll shortcuts. Lyric text editing retains its own input handling.

Both piano windows include an **音号显示** menu (shown as CDE / 123 or 123 · 1=C). Enable **Print all note labels in piano roll** in Settings first. Choose standard pitch names or numbered notation; numbered mode additionally exposes the twelve tonic choices. The base for 1=C defaults to C4; change the reference C under General Settings if needed. High/low dots are automatic, and there is no separate octave menu in the piano windows. The global reference control is enabled only in numbered mode with all note labels enabled. Choices apply immediately to all open instrument/SVS piano windows and persist globally. They affect keyboard labels only; clip-body pitch labels and actual playback/synthesis pitches remain standard. When all note labels are disabled, original labels remain and the new controls are disabled.

For enabled all-note labels, white keys default to black text and black keys to white text. Theme authors can set qproperty-whiteKeyInactiveTextColor and qproperty-blackKeyTextColor in the existing PianoRoll and SVSPianoRoll rules in data/themes/default/style.css. The bundled Jianpu ASCII font is unmodified and includes its SIL OFL notice/license.

The SVS piano toolbar now includes **Clear ghost notes**, acting on the shared reference snapshot, and **Note length**, with Last note, straight and triplet values. Quantization controls snapping; note length controls the initial duration of click-created notes. Drawing a tail still permits resizing.

USVC is deployed as `plugins/svcusvc.dll`, with default address `http://127.0.0.1:8001`. Its sidebar uses **USVC / backend / model / speaker**, with speaker children only for multi-speaker models. Names, backend groups, speakers and per-model controls come from `/model` and `/settings`. Drag a model or speaker onto an existing SVC track to replace its selection, or into the Song Editor to create a track. Import or drag source WAV audio into the track and choose Re-render.

USVC sends raw WAV to `/infer`, validates the returned mono PCM16 WAV and publishes bounded audio events through the unchanged SVC ABI. Untouched backend defaults are omitted; nullable numeric values use Default, and inactive conditional values are omitted. The Python API is unchanged. Server test-fixture voices verify transport and speaker selection, not trained voice quality.

Before replacing track audio, USVC aligns small backend frame-quantization differences at the tail and converts output to the DAW global sample rate using libsamplerate. Matching rates bypass resampling. Differences above 20 ms still fail validation; no interior audio is stretched. This also prevents a successful HTTP response with a slightly short waveform from leaving the track in Failed/partial-result state.

SVC track waveforms retain each pixel interval's positive/negative peaks from both channels, scanning converted audio at its native rate. SVC stores output PCM without converting it back to the input rate. Output matching the DAW rate plays directly; differing rates use the sample track's existing AudioResampler in Linear mode. A continuous clip handle preserves resampler state across audio periods and ticks, restarting when playback seeks or the clip source/crop changes.
