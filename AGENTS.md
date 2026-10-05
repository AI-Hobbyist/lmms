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
