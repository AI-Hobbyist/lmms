# SVC M0–M5 implementation progress

Original plan: [LMMS SVC API支持计划书.md](../../LMMS%20SVC%20API支持计划书.md).

| Milestone | Implementation | Validation | Checkpoint |
| --- | --- | --- | --- |
| M0 | ABI 1.0, capability schema, bounded multipart parser, C consumer/reference engine, lifecycle and fixed algorithm contract | PASS: Release build; SVCContractTest + C-only SVCConsumer (2/2); no GUI changes | Commit/push checkpoint: feat(svc): add versioned SDK and streaming contracts |
| M1 | Pending | Pending | Pending |
| M2 | Pending | Pending | Pending |
| M3 | Pending | Pending | Pending |
| M4 | Pending | Pending | Pending |
| M5 | Pending | Pending | Pending |

Local RVC integration target is `http://127.0.0.1:8000`, with no Bearer token required for testing. Production recommendation on final delivery: enable Bearer token authentication and configure credentials through the engine connection settings.

Initial tracked worktree was clean. Existing untracked build logs, reference trees and SDK artifacts are preserved. Milestones run sequentially; M0–M3 use the reference backend before M4 actual RVC integration. GUI validation uses native Windows Qt only; current layout work does not use Computer Use.
