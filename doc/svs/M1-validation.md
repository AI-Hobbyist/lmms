# M1 validation

Release builds of LMMS, SVSExample and SVSIntegrationTest passed. CTest passed after adding the independent editor mouse double-click test and isolated temporary configuration (1.76 seconds, 2026-10-05). Evidence: validation/M1-build.log, validation/M1-test.log and validation/M1-enum-regression.log.

The vertical slice covers voice discovery and artwork, native SVS creation with Pattern Store rejection, default/custom naming, note entry and synthesis, finite nonzero PCM, actual LMMS mixed output, XML restoration, independent clone IDs and position, and mixer channel movement/deletion. Lyrics and parameters survive restoration. Advanced editor behavior, capability schemas and scheduler/export behavior remain M2–M4 work.

## Local development application

The development executable is build/Release/lmms.exe. Qt runtime deployment is recorded in validation/M1-deploy.log. Copy data/themes into build/Release/data/themes, use portable_mode.txt and build/Release/.lmmsrc.xml, with the theme path pointing at this repository's data/themes/default/ and workingdir pointing at build/Release/lmms-workspace/. A blank templates/default.mpt in that workspace avoids loading unrelated instrument plugins which are not part of the SVS build targets.

Computer Use verified that the development executable loads the dark LMMS theme and toolbar icons without missing-plugin startup dialogs. The original installed LMMS was closed at the user's request. Development configuration, blank template and copied resources are local build fixtures, excluded from delivery. Automatic tests use QTemporaryDir configuration and do not overwrite the development or installed application settings.

Listening and broader interactive acceptance: MANUAL/PENDING. These do not block M2. The current editor is a minimal M1 entry surface; inline lyrics, tool switching, curves and complete TuneLab interaction alignment belong to M3.
