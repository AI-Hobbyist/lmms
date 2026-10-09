# SVS piano-roll sidebar languages

The singer sidebar now includes a Language selector when the selected engine/voice declares more than one supported language. Choices come exclusively from its capability schema, using native language names with the identifier as fallback. Switching uses the existing track language setter, and both the sidebar and existing properties control stay synchronized. Capability refreshes update the choices when the voice changes. Single-language or unavailable voices hide the selector; read-only tracks disable it.

Validation on 2026-10-09:

- Built `SVSIntegrationTest` and `lmms` in the existing Release deployment using foreground PowerShell PTY, SDK environment initialization, UTF-8 Tee logs and immediate exit checks.
- `nativeSidebarLanguages`: **3 passed, 0 failed, 0 skipped**, including setup/cleanup. The real Windows Qt editor verified capability-derived choices, keyboard language switching, properties synchronization and hiding after changing to the minimal single-language voice. The initial test used Down on the last item, which does not wrap; the corrected test uses Home then Down.
- Captured and inspected the visible native window in `validation/SVS-sidebar-language-native.png`: Language appears below Singer, Japanese and Chinese render normally. Test window closed; no offscreen mode used.
- Production executable: `build/Release/lmms.exe`. Existing unrelated JACK/Carla/icon startup warnings remain outside this change. `git diff --check` passed.
