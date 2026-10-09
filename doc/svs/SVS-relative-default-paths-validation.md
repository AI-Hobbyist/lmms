# Relative default runtime paths

Windows installations now default to the executable's `lmms-workspace/` directory. Configured relative paths resolve against the executable directory, independently of the process working directory. Saving paths inside that directory writes relative values; external custom locations and Qt resource paths such as `data:/themes/default/` remain valid. Non-Windows default working directories retain their upstream behavior.

This covers the working directory, theme, VST compatibility directory, GIG/SF2 directories, LADSPA/STK directories, default SF2 and background image. SVS cache paths remain derived from the resolved working directory; default DiffSinger vocoder discovery remains derived from its deployed package. External voicebank scan locations retain their explicitly configured paths.

Validation on 2026-10-09: native Windows Qt `relativeConfigurationPaths` passed (3 passed including setup and cleanup). The test changed the process working directory before loading relative paths, checked runtime resolution against the executable directory, verified relative values after saving and reloading, and preserved an external soundfont directory and a Qt resource theme path. Foreground PowerShell build/test pipeline and `git diff --check` passed.

An existing copied configuration containing an external absolute working directory is not silently relocated. The installed configuration inspected earlier points into the development checkout; that existing setting must be changed to `lmms-workspace/` when deploying this update. With `D:/Program Files/LMMS/lmms.exe`, that resolves to `D:/Program Files/LMMS/lmms-workspace/`, with DiffSinger caches under `cache/SVS/DiffSinger/`.
