# Pitch recordings and sidebar language replacement package

Product source commit: `99c094e27`, pushed to `master` on 2026-10-09.

- Archive: `build/packages/lmms-enhanced-incremental-99c094e27-20261009-110329-win64.zip`
- Size: 8,152,349 bytes.
- SHA256: `44E2F76EA01163D8084AE17F2926736C54BAFA2193CEF2C0E55F8C7C83FD9D7C`.
- Baseline: `lmms-enhanced-full-55ced558c-win64.zip`; includes 67 changed runtime files and 4 package entries.
- Includes continuous predicted pitch, random/default and fixed/manual recording seeds, generic pitch recording history, relative bundled path defaults, and capability-derived piano-roll sidebar language selection. SDK source documentation is updated in the repository.

Created and hash-verified using the existing replacement packager. Installer `-VerifyOnly` against `build/Release` verified all 67 payload files, installation paths and unchanged baseline files. Both commands used the initialized SDK environment, foreground PowerShell PTY, UTF-8 Tee logging and immediate exit checks. No installed application or configuration was modified.

Close LMMS before applying the package to an installation based on the named full package. Personal configuration, projects and caches are excluded. An existing absolute working directory remains as configured: the inspected `D:/Program Files/LMMS/.lmmsrc.xml` still points to the development workspace. Set its working directory to `lmms-workspace/` to use the installation-relative workspace and `lmms-workspace/cache/SVS/DiffSinger/`; changing fresh defaults does not migrate existing explicit paths or their contents.

Development executable: `build/Release/lmms.exe`; DiffSinger package: `build/Release/svs/SVSDiffSinger/`. Native Windows GUI acceptance is recorded in `SVS-pitch-recording-validation.md` and `SVS-sidebar-language-validation.md` with actual visible-window screenshots.
