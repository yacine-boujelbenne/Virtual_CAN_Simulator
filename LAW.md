# LAW - shared development contract

Version 1, 24 October 2026. Applies to OpenCode, Copilot and Codex. The user's current instruction prevails. Existing higher-priority repository guidance still applies. These are cooperative workflow rules; Markdown and worktrees are not a security sandbox.

## 1. Product invariants

- The application, clock, vehicle/ECU behavior, protocol state machines, project validation/hash and experiment recorder belong in C++/C. Python may remain for test oracles, analysis, developer tooling and this coordinator.
- One ECU instance equals one isolated worker/container and one declared resource profile. Never execute a missing distributed worker silently inside the main process.
- One logical clock and one authoritative ego-vehicle plant per run. Renderer frame rate, host CPU throttling, HTTP delay and LLM latency are separate quantities.
- Preserve integer simulation-microsecond timestamps and epoch/sequence identity. A producer cannot transmit before computation finishes. A retry cannot change already-frozen tick inputs.
- Speed/pose supplied to controllers comes through declared sensors/ECU observations with validity and acquisition age. Ground truth is for physics, rendering and evaluation; do not secretly replace late/stale inputs with truth.
- Model CPU/RAM/flash, task deadlines, sensor delay and CAN buffering explicitly. Docker quotas isolate the host; do not call them MCU emulation. Label uncalibrated profiles honestly.
- Motor/ABS/EPS/transmission each retain one physical binding owner. MPC produces bounded requests that traverse the ECU/CAN path; it does not teleport the vehicle or bypass braking authority.
- Preserve actual `.vcanproj` JSON v1 and canonical C++ SHA-256/A-B semantics. Earlier design ZIP examples are not the implemented file format. Add versioned extensions/migrations only after compatibility tests.
- Keep AI offline/advisory by default, as locked in the checkpoint. Do not quietly replace deterministic control with an LLM, or make a cloud model mandatory to run the native application.

## 2. Source and evidence

Read CURRENT_STATE, the supplied checkpoint, HEAD and git status before work. Reported checkpoint tests are not freshly rerun tests. The 42-test shim run is not a substitute for genuine GoogleTest/CTest. A skipped test is not a passed test. Missing Docker permission is an environment blocker, not proof that containers passed.

First establish the commit containing the checkpoint's implementation. Never rebuild P0 from the older October 12 remote tree and call it preserved. Never delete, reset, clean or force-push a user's uncommitted work. Keep legacy tests, protocol vectors, the Python ISO-TP oracle, notices and performance evidence until replacement gates pass.

## 3. Task ownership

The coordinator database is the live task authority. `tasks.json` is the versioned initial backlog, not a file three agents edit concurrently. Claim one task/phase, get a lease and token, and work only in the assigned worktree. Read the ticket plus other providers' progress before starting and after each handoff. Scope conflicts and unmet dependencies mean WAIT; choose a different ready task if permitted.

Implementation is one reviewable commit per task attempt. Touch only the ticket's write scopes. If a needed change falls outside them, stop and report the contract dependency. Do not edit LAW, task acceptance checks, provider configuration or another provider's progress just to make your task pass. No self-approval. A reviewer must be a different provider identity; one remaining provider means independent work may continue but acceptance waits for a human or returning reviewer.

## 4. The loop

Observe -> claim -> implement -> checkpoint -> submit -> independent review -> trusted checks -> local fast-forward integration -> unlock dependent work. A successful provider process exit is not task completion. A prose claim of passing tests is not a coordinator-run check. A review approval is not permission to publish or deploy.

Leases expire. Old tokens are fenced; late reports cannot change current state. Preserve every interrupted attempt's branch/worktree and partial diff. A fresh attempt uses a new worktree, reads the partial handoff and revalidates it. Never let a recovering agent resume writing into a reassigned checkout. If the integration head advanced, rebase/recreate the attempt on the new base and obtain fresh review; do not force a merge.

Quota/context exhaustion, timeout, missing executable, authentication failure and permission denial must be distinguished from a source bug. Pause/cool down the affected provider and select another compatible enabled provider. Do not buy quota, switch paid accounts/models or relax tool permissions automatically. The coordinator cannot infer an exact token balance from provider prose; operator status and returned errors are evidence.

## 5. Progress and stopping

Before interruption write a partial report: task/phase, base commit, branch/worktree, files touched, last command, tests actually run, blocker, candidate commit if any and next concrete action. When the provider cannot write it, the coordinator preserves the worktree and records the last log and reason.

Live `.orchestrator/progress/{OPENCODE,COPILOT,CODEX}.md`, task status and events are generated from persistent SQLite state. Static `coordination/progress/*.md` are starting handoffs, updated deliberately in reviewed commits. Do not claim another provider completed work without a submitted commit and accepted evidence.

Stop for `.orchestrator/STOP`, budget, unsafe/unknown action, missing source or an all-blocked queue. No busy retry loop. When all providers are unavailable, retain state and wait/exit; do not fabricate progress. Automatic execution never uses universal approval-bypass flags. Only the configured local integration branch may advance automatically. Remote push, main/master merge and deployment remain separate authorized operations.

## 6. Definition of done

A task needs an accepted scoped commit, independent approval, successful configured checks, and a candidate based on the current integration head. Preserve units, logging and rollback behavior. A native-app release additionally needs a real CMake/CTest build, headless/container parity, project A/B compatibility, durable logs, a demonstrable 3D closed loop and a native UI. Failed or unavailable 3D/GPU execution is documented; screenshots and mocked tests do not establish driving correctness.
