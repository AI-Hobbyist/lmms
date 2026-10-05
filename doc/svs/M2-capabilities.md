# M2 implementation notes

The example embeds full.json/minimal.json at build time; its dictionaries are versioned UTF-8 JSON files in the plugin package. There are no TuneLab, .NET or Qt dependencies in the example. The plugin owns every returned string and releases it through release_string.

Capability schemaVersion=1 declares input parameters and feedbackParameters separately. IDs are stable; enum values are IDs. Types are float/int/bool/enum/string and scopes are track/clip/note/phoneme. The model validates defaults, ranges, positive logarithmic ranges, integer bounds/steps, interpolation and duplicate IDs. Required unknown capabilities produce diagnostics; optional fields remain in the original declaration. visibleWhen compares context values. Hidden/unknown parameter data are preserved in XML.

String parameters may declare a nonempty resourceIds array; the editor uses an ID selector and rejects undeclared IDs. This does not expose arbitrary filesystem paths. Numeric range fields must be JSON numbers. Dictionary imports also check the voice's declared language and phoneme set; incompatible saved dictionaries remain intact and produce result diagnostics.

SVSParameterPanel updates by ID, keeps widgets and focused edits, presents unset/common/mixed selections and disables result controls. Track and clip panels plus language/dictionary controls are integrated into the independent editor. The reusable note/phoneme panels are connected to the editor's selection and phoneme editing in M3. Capability rechecks after edits run off the GUI thread, with a track request gate preventing old declarations from publishing.

Dictionary entries are arrays so duplicate words and candidates are diagnosed with an entry index. Dictionaries include ID/version/language/phonemeSet and receive a SHA-256 hash. Precedence is manual phonemes, manual pronunciation, project dictionary, voice dictionary, then the plugin's parser. The host does not implement a language-specific parser. Unknown text is retained with diagnostics. Continuation is enabled only by the plugin's declared marker and is invalid without an adjacent source note.

The optional pronunciation tail in the C function table accepts a JSON request and returns plugin-owned JSON; the required prefix remains usable without this function. The example parser recognizes its declared symbols. Complete ABI compatibility/error-package checks remain the M5 SDK acceptance work.

Current automation covers full/minimal declarations, invalid/duplicate schema entries, multilingual dictionary candidates, manual overrides, unknown words, invalid continuation, three-state values, editor focus preservation, read-only controls, XML parameters and actual gain changes in mixed PCM. Broad piano editing is M3; scheduler/caches/export are M4.

M2 acceptance: PASS. Release LMMS/example/test builds and CTest passed (six QtTest slots, 2026-10-05). Full/minimal/full switching preserves track, clip and phoneme inputs; unavailable controls reject writes. The gain test measures a 0.25 energy ratio after halving amplitude. Japanese and Chinese parsing, project precedence, resource selectors and readonly controls are tested. Logs: validation/M2-build.log, M2-test.log, M2-QtTest.txt. Listening: MANUAL/PENDING.
