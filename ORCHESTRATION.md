# Start here: native C++ vehicle laboratory

Prepared 24 October 2026 from the supplied README and CHECKPOINT. This pack defines the next development cycle; it does not claim the native desktop/3D application already exists.

Read in order:

1. `coordination/CURRENT_STATE.md` - what exists, what is only reported, and the source mismatch.
2. `LAW.md` - rules shared by every provider.
3. `coordination/NATIVE_CPP_ROADMAP.md` and `coordination/ARCHITECTURE.md`.
4. Your role: `OPENCODE.md`, `COPILOT.md`, or `CODEX.md`.
5. `coordination/OPERATING_CYCLE.md` - coordinator and manual/automatic operation.

The goal is a C++20 application with a headless runner and a native Qt desktop client. It coordinates one C++ kernel per ECU container, a vehicle model, CAN networks, resource timing, trajectory tracking, project files and durable experiment logs. A 3D world is an adapter, initially esmini. The existing web dashboard remains an optional demo/inspection client. Development coordination may use Python; production simulation must not depend on Python controllers.

## First action on your machine

Do not replace your working tree with the old GitHub `feature` snapshot. Back up and commit the local P0 implementation first. At inspection, GitHub `feature` was `2a2c9149204e65455236152162b39f7bd4656a04`; it did not contain the P0 checkpoint's new files. Preserve your current local changes, compare the source, then establish a baseline commit containing the real P0.1-P0.6 work. The coordinator's baseline gate intentionally refuses the older source.

This pack adds files; it does not replace your existing root README, CHECKPOINT, CMakeLists, application code or `.gitignore`. Merge any existing AGENTS/Copilot/OpenCode instructions rather than overwriting local guidance. The original attachments are retained under `coordination/sources/` for provenance.

```sh
python3 -m unittest discover -s tests/orchestration -v
python3 tools/coordinate.py doctor
python3 tools/coordinate.py init
python3 tools/coordinate.py status
```

`init` creates an isolated local integration branch/worktree at the current HEAD. Use a checkout containing the reconciled P0 source. Configure `coordination/providers.local.json` from `providers.example.json` after authenticating the CLIs. All automated providers start disabled. The coordinator never installs clients, purchases quota, reads credential files or pushes/merges a remote branch. A VS Code extension alone is a manual participant, not an executable CLI.

Commit the pack and the reconciled source before `init`; newly created worktrees contain committed files only. If you use the published planning branch, cherry-pick its single coordination commit onto your LOCAL P0 branch rather than switching to the older source below that planning branch.

```sh
python3 tools/coordinate.py run --max-calls 6 --max-wall-seconds 1800
```

This runs bounded implement/review attempts and safe fast-forward local integration. It stops for its budget, STOP file, all work blocked or unavailable providers. Re-run after restoring availability; SQLite state, candidate commits, reports and handoffs survive. See OPERATING_CYCLE before enabling actual provider execution. In this environment the three CLIs were absent, so only fake-adapter/git coordination tests can be exercised; no claim is made of authenticated three-provider execution.

## Deliverable map

| File | Purpose |
| --- | --- |
| LAW.md | Project invariants, ownership, evidence and handoff rules |
| OPENCODE.md | Core C++ implementation and resource/vehicle integration |
| COPILOT.md | Native UI, developer ergonomics and focused component checks |
| CODEX.md | Contracts, independent review, regression/CI and integration |
| coordination/tasks.json | Dependency graph, acceptance criteria, write scopes and checks |
| coordination/providers.example.json | Local CLI/manual adapters and fallback capabilities |
| coordination/progress/*.md | Durable provider handoff templates and initial state |
| .orchestrator/progress/*.md | Generated live progress, rebuilt from coordinator state |
| tools/coordinate.py | Local persistent coordinator; standard Python + Git |
| tools/check_baseline.py | Source gate for the reported P0 implementation |
| tests/orchestration/ | Real temporary-Git and deterministic failover tests |

The October 22 target is past. Follow acceptance gates rather than pretending that deadline is still ahead. Set a new delivery date after the real P0 build and the first 3D capability spike.
