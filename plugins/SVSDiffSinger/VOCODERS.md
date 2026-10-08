# Shared DiffSinger vocoders

This folder is the default global shared vocoder root: `svs/vocoders` beside
the LMMS executable. No vocoder model is bundled with LMMS.

Place each vocoder in a folder named exactly as the voicebank acoustic
configuration's `vocoder` value, containing `vocoder.yaml` (or `.yml`/`.json`)
and the referenced model files. Nested category folders are supported.

Select additional roots or individual named vocoder folders in
SVS settings > DiffSinger > Global shared vocoder directories, then apply.
Several vocoders and roots can coexist. The configured list order determines
priority for duplicate names. A voicebank's bundled `dsvocoder/vocoder.*`
always takes priority; invalid bundled configurations report an error.

The vocoder audio/mel configuration must match the acoustic model. Rescan
after changing model files so synthesis caches acquire a new fingerprint.
