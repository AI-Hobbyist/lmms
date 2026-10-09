# Edited curve repair replacement package

Product and validation checkpoint: `d33d9287a`, pushed to master on 2026-10-09. Production fixes: `c439a46fa` (segment clipping precision) and `94a9829f0` (absolute reference defaults).

- Archive: `build/packages/lmms-enhanced-incremental-d33d9287a-20261009-115646-win64.zip`.
- Size: 8,155,517 bytes.
- SHA256: `3D48F376F60AB19A6999223C321C2686C0774C12F4FD6DD40DFFA1683F273C82`.
- Full baseline: `lmms-enhanced-full-55ced558c-win64.zip`.
- Contains the 67 runtime files from the previously delivered `99c094e27` incremental package, replacing only `lmms.exe` and updating package metadata/instructions. Retained all published plugin binaries and dependencies; concurrent unfinished compute/DiffSinger edits are excluded.
- Host SHA256: `903239C4C3C182D51E91A030D95A57489200090B590AB662A168F9C2522AEA0A`.

Packaging reused the existing `build/packages` directory and original replacement manifest/installer. Verified the previous archive's SHA256, every new archive payload's size/hash, entry count and unchanged host hash during packaging. Original installer `-VerifyOnly` against `build/Release` passed all 67 payloads, installation paths and unchanged baseline files. Foreground PowerShell PTY with SDK initialization, UTF-8 Tee logs and immediate exit checks used. No installed runtime or personal configuration was modified.

Native GUI acceptance against the retained published DiffSinger DLL: 6 passed, 0 failed, 0 skipped. The real Windows editor rendered after editing with Pencil selected, displayed an absolute default matching feedback with other overlays actually hidden, and rendered after localized absolute editing and reset. See `SVS-edited-curve-render-validation.md` and `SVS-absolute-reference-defaults-validation.md`. Executable: `build/Release/lmms.exe`.

Close LMMS and use the package's `Install-Replace.cmd` to apply it to an installation containing the named full baseline, such as `D:/Program Files/LMMS`. Configuration, personal voices, projects and caches are excluded. A test audition cache remains at `build/Release/lmms-workspace/cache/SVS/DiffSinger/d67204f1025be656ca3064132261bfc315e3c248596cf04cf2bd6b3fa37d2954.wav`; its file SHA256 matches its name.
