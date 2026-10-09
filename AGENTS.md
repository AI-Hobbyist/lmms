## Tool Selection Strategy & Constraints

1. **Prioritize CodeGraph / Precision Retrieval**:
   - ALWAYS search for definitions, references, implementation, context, and **repository structure/architecture** using CodeGraph / Symbol Navigation first.
   - For high-level or goal-driven tasks (e.g., `/goal`, "写计划书", "分析整个项目", "架构勘察"):
     - **DO NOT** trigger `explore` or repo-wide search to write plans or analyze architecture.
     - FIRST query CodeGraph at the project root (`projectPath`) or look up top-level entry points (e.g., `main`, `export`, `InferenceEngine`, build files) to map graph topology.
   - DO NOT invoke broad file exploration (`explore`, repo-wide semantic search, directory listing, or fuzzy file navigation) for ANY reason unless CodeGraph explicitly returns empty results or unindexed project state.

2. **Token & Latency Efficiency**:
   - Treat `explore` tools as high-cost fallback actions.
   - Limit the context window to active editor files, project-root CodeGraph responses, and explicitly tagged symbols (`#file`, `#symbol`).

3. **Execution Workflow**:
   - Step 1 (Goal/Plan/Survey): Query CodeGraph at `projectPath` or locate entry symbols for key modules (e.g., export/inference/model structures).
   - Step 2: Read ONLY the target files/modules returned by CodeGraph graph traversal.
   - Step 3 (Strict Fallback): Run `explore` tools IF and ONLY IF precision graph lookup fails or CodeGraph indicates the repository is unindexed.

## Mandatory Build Pipeline & Error Diagnosis Protocol

4. **Strict Foreground Build & Logging Requirement**:
   - **一律在会话内以前台 PowerShell 交互方式**执行所有编译、构建、打包和测试。无论如何，用户必须能够展开本会话中的折叠执行记录，看到实际 PowerShell 命令及实时、完整的 stdout/stderr；不能只发送计划、口头进度或日志文件路径，让用户干等。
   - 使用能够将输出流式展示在折叠执行记录中的前台交互执行方式（PTY）。长命令跨工具等待时，必须保持同一个前台执行会话并持续回传新增输出；不得启动后台作业、脱离执行会话运行或静默等待，也不得隐藏、截断或丢弃实际执行日志。
   - **默认不得额外弹出 PowerShell / Windows Terminal 窗口**，除非用户明确要求。前台执行以会话内可展开的命令和实时输出记录为准，不以是否另开桌面终端窗口为准；Qt GUI 验证仍须遵守本文件的真实 Windows 窗口要求。
   - **MANDATORY**: EVERY compilation, build, package, or test action (regardless of language/tech stack: C++, C#, Java, Rust, Go, Node.js, etc.) MUST run interactively in the foreground using the standard PowerShell pipeline. NEVER run builds in the background or suppress stdout/stderr.
   - ALWAYS route any build command through `2>&1 | Tee-Object -FilePath "build.log" -Encoding utf8`. This guarantees real-time terminal output while silently capturing full log history.
   - ALWAYS capture `$LASTEXITCODE` and check build status immediately.
   - ALWAYS catch errors via reading `build.log`

   **Standard Universal PowerShell Pattern** (Workspace/Project Root):
   ```powershell
   # Universal execution template for ANY toolchain (cmake, dotnet, gradle, mvn, cargo, etc.)
   & <BUILD_COMMAND> 2>&1 | Tee-Object -FilePath "build.log"
   $buildExitCode =$LASTEXITCODE
   if ($buildExitCode -ne 0) { exit$buildExitCode }
   ```

## Mandatory formatting for new feature code

- All feature code added or changed after commit `a2f57e70ce9c3468b4b6d21955bbe65a0989048a`, including future additions, MUST have clear, consistent, readable formatting before delivery.
- For C/C++, follow the repository `.clang-format`: consistent indentation, brace placement, operator spacing, and sensible line wrapping. Put independent statements on separate lines; expand nontrivial control-flow and lambda bodies instead of compressing them into one line. Separate logical sections with blank lines.
- Apply equivalent indentation and line wrapping to new scripts, build definitions, and stylesheets using their language conventions. Preserve string contents, generated data, and behavior.
- Format new files in full. In files inherited from the baseline commit, restrict formatting to feature lines added or changed since that commit. Do not reformat untouched original LMMS code or make unrelated refactors.
- Before completion, inspect the formatting diff, verify that executable code changes only in layout, and run `git diff --check` on the affected files. Keep pre-existing worktree edits intact.

## Existing build and plugin deployment directories

- Reuse the existing build and development deployment directories. Do not create a replacement directory for a build, plugin deployment, or validation; keep each artifact in its original location unless the user explicitly requests relocation.
- Windows development plugins must compile directly into the existing executable's `plugins` directory: `build/Release/plugins` beside `build/Release/lmms.exe`. Overwrite the previous plugin DLLs there; do not deploy another plugin set under `build/plugins/Release` or a new installation prefix.
- Validation must load plugins from that same deployed directory. Before reporting a build complete, verify the enabled plugin targets actually wrote their DLLs there and report the executable path.
- Rebuild plugin support libraries in the same directory too. If a plugin is disabled by the current configuration, check for a leftover DLL and record its in-place retirement (for example, rename it to `.dll.disabled`) so the application cannot load an obsolete ABI.

## Native Windows GUI validation

- User requirement: GUI tests must use the native Windows Qt platform; do not use `QT_QPA_PLATFORM=offscreen`, including in child test processes. Use real application window screenshots for visual/font/layout acceptance. QWidget render images may support numerical pixel assertions, but do not replace real-window screenshots.
- Computer Use is reserved for later comparison of TuneLab piano-roll operation semantics; do not use it for current layout/font/screenshot development. Native Qt tests may capture their real windows directly.
- Test only the development LMMS installation with its configured theme; do not open the original installed LMMS. Close Computer Use test windows when finished and reopen only when needed.

## SDKS
- Before Windows configure/build/test/package commands, dot-source `./buildtools/Enter-LmmsEnvironment.ps1` in that PowerShell session. It refreshes the shared persistent SDK/tool variables for agents whose parent app was already running. Use `cmake`/`ctest` from PATH and `$env:QTDIR`, `$env:SVSSDK_ROOT`, `$env:LMMS_CMAKE_TOOLCHAIN_FILE`, `$env:LMMS_CMAKE_GENERATOR`, `$env:LMMS_CMAKE_PLATFORM` instead of repeating local paths; retain the foreground logging/exit-code pipeline above.
- **Qt6:** `C:\Qt\6.10.3`
- **Windows SDK:** `D:\Windows Kits\10`
- **Libjack DLLs:**
   - **x86:** `C:\Windows\libjack.dll`
   - **x64:** `C:\Windows\libjack64.dll`
