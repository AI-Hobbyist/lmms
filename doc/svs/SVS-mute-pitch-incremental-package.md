# SVS mute and pitch incremental replacement

Status: PASS, 2026-10-09. Product source commit: `be7e7957918430f9a0fece86758845c83aa2b886`; includes mute/solo fix `1d3d1c271` and pitch menu feature `be7e79579`. Evidence-only commits do not change the product input.

- ZIP: `build/packages/lmms-enhanced-incremental-be7e79579-20261009-101650-win64.zip`.
- Size: 8,138,711 bytes; 66 changed runtime files plus 4 installer/manifest entries.
- SHA256: `56F2B5F7E80EABECE99E4AC8F3EBFC1A0F7630EC39A3228F1EEECB5FF5927429C`.
- Base: `lmms-enhanced-full-55ced558c-win64.zip`. Includes changes since this full base, so the previous incremental overlay can already be installed. Installer checks all unchanged baseline files before replacing the listed payload.
- Executable: `build/Release/lmms.exe`, version `1.3.0-alpha.2.107+be7e795`; updated DiffSinger plugin remains in `build/Release/svs/SVSDiffSinger`. Native plugin and support DLL paths remain `build/Release/plugins`. Sid remains retired as `sid.dll.disabled`; GigPlayer is not deployed.

The foreground PowerShell packager re-read every ZIP entry and verified SHA256 against runtime input. The Windows PowerShell 5 installer completed `-VerifyOnly` against the existing development installation: `Verified 66 payload files and installation paths. No changes made.` Source worktree was clean at packaging time; no private voicebank, development config or audition cache is shipped.

Native Windows Qt and PCM tests: 9 passed / 0 failed / 0 skipped, plus the external DiffSinger Furina backend test PASS. Details: `SVS-mute-solo-validation.md` and `SVS-pitch-context-validation.md`. Build, test and package commands used the shared SDK script, foreground PTY and UTF-8 `build.log`; no offscreen rendering or original installed LMMS was used.

Close LMMS, extract the ZIP, run **Install-Replace.cmd**, and select the existing installation directory. The installer preserves personal projects and configuration.
