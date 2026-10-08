# Windows native plugin initialization: test host identity

Status: fixed and validated. The reproduced LADSPA initialization crash is a
test-host problem, not a crash of the normally named development LMMS executable.

## Root cause and causal evidence

Windows native LMMS plugin DLLs import host symbols from `lmms.exe`. The SVS
integration harness was named `SVSIntegrationTest.exe`, although it statically
contains and initializes its own Engine. Loading native plugins consequently
loads another `lmms.exe` PE image. That image's Engine has not been initialized;
its LADSPA/LV2 manager state differs from the running harness's state.

The initial loop reproduced an access violation in
`Ladspa2LMMS::getValidEffects` during PluginFactory discovery. Discovery order
also produces the equivalent failure in `Lv2SubPluginFeatures::listSubPluginKeys`.
Both occur during `initTestCase`, before the selected SVS test runs.

`validation/LADSPA-name-only-comparison.json` proves the name-only causal test:
the executable bytes and SHA256 are identical. Under `SVSIntegrationTest.exe`
the native plugin initialization crashes (exit `0xC0000005`); under `lmms.exe`
the same binary initializes normally and enumerates 202 LADSPA effect keys.
See the corresponding name-only crash report and stack log.

## Minimal fix

`tests/CMakeLists.txt` sets the SVS integration target's Windows output name to
`lmms`, matching the existing UI harness convention. Its target/CTest name stays
`SVSIntegrationTest`, and its existing output directory stays `build/tests/Release`.
CTest now resolves `build/tests/Release/lmms.exe`. The old incorrectly named test
executable is retired in place as `SVSIntegrationTest.exe.disabled`.

`nativeLadspaPluginHost` exercises the actual plugin-discovery seam. It checks
that `GetModuleHandleW(L"lmms.exe")` is the running main image, the Engine manager
is initialized, the deployed `build/Release/plugins/ladspaeffect.dll` is registered,
and its real subplugin enumeration is nonempty. It shows/captures a real native
Windows Qt window. No production LADSPA, PluginFactory, Engine, or SVS behavior
was changed; no null guard, plugin exclusion, or offscreen workaround was added.

## Validation and actual-application comparison

- `validation/LADSPA-startup-before.txt`: original selected-test startup failure.
- `validation/LADSPA-regression-before.txt` and its stack log: regression added
  before the CMake fix; native discovery still crashed.
- `validation/LADSPA-regression-after.txt`: 4 passed, 0 failed, 202 effect keys;
  native-plugin host identity and original embedded window lifecycle PASS.
- `validation/LADSPA-original-loop-final.txt`: final original-loop rerun after
  the negative name-only comparison, 3 passed, 0 failed, 202 effect keys.
- `validation/LADSPA-release-and-DiffSinger-after.txt`: 4 passed, 0 failed.
  The formerly blocked external-voice render now passes in the real test editor.
  The existing release-window test additionally starts the actual
  `build/Release/lmms.exe` twice, with configured and empty voice catalogs, exposes
  stable real windows, verifies deployed native modules, and closes both normally
  with exit code zero. This establishes that normal application startup does not
  suffer the test-host initialization failure.

All runs use the normal Windows Qt platform and the same development plugin
directory, with isolated test configuration and the configured development theme.
The original installed application was not launched. Tests close their owned
windows. Local real-window screenshots are `validation/LADSPA-test-native-window.png`
and the existing release-window capture paths. Release screenshots prove startup,
not readiness of every voice catalog; a missing-voice label observed there is a
separate follow-up, outside this LADSPA task.

Foreground PowerShell SDK refresh, Tee logging and immediate native exit-code
checks were used. The existing `build/tests/build.log` path avoids contention
with concurrent tasks writing the workspace's `build.log`. The initial throwaway
startup probe and its unsuitable capture were removed. Existing unrelated
Song/VST changes are preserved and excluded from this fix.
