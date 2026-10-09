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
