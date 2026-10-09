# Edited curves and segment anchor precision

Reported trigger: leave Pencil selected and drag in the SVS parameter curve lane; synthesis fails with `Invalid curve anchor`.

The deterministic regression `segmentedEditedCurveAnchors` reproduced the exact error before the fix (2 setup/cleanup passes, 1 failed test, 451 ms). A valid curve containing a drawn interval reached the segmented planner, but its output no longer passed strict anchor validation. Editing preserves outside values using adjacent floating-point ticks; subtracting the segment context origin and adding it back rounded those distinct ticks onto the same coordinate.

Segment curve clipping now keeps content-local coordinates throughout. `Curve::slice` retains its default rebasing behavior for existing callers and accepts an explicit non-rebasing option for the segmented planner. This preserves discontinuity anchors without relaxing validation or changing curve values. The regression asserts every retained tick and value, including after JSON serialization.

Native acceptance also exercises the reported Pencil tool path in the real Windows Qt piano roll with Furina, edits the voicing offset lane, verifies notes remain unchanged, and requires final synthesis status Ready before capturing the visible window.

Final run on 2026-10-09: `segmentedEditedCurveAnchors`, `segmentedPlanDependenciesAndAssembly`, `nativePencilCurveRenders`: **5 passed, 0 failed, 0 skipped**, including setup/cleanup, 17.7 seconds. Actual visible-window screenshot: `validation/SVS-pencil-curve-native.png`; inspected Ready status, the edited offset curve and normal CJK rendering. Window closed, no offscreen mode used. Built `build/Release/lmms.exe` and the native harness using foreground PowerShell PTY with SDK initialization, UTF-8 Tee logging and immediate exit checks. A test-only use of nonexistent `setCurves` was corrected to the existing `setEditorData` API before the passing run. Unrelated compute/DiffSinger worktree edits were preserved and excluded from this source commit.
