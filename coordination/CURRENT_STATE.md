# Actual starting level and evidence

Assessment: 24 October 2026. Primary inputs are the two user-supplied files copied unchanged into `sources/`. This assessment supersedes older conversation statements that the P0 project/timing/performance work was merely a design.

## What the checkpoint reports

| Area | Reported implementation | Qualification |
| --- | --- | --- |
| ECU execution | Dedicated C++ kernels behind container/local-worker adapters | Preserve the one-ECU-per-worker model |
| Protocols | CAN/CAN FD; event-driven C++ ISO-TP; 13 oracle vectors and 12 native cases | Keep Python transport as test oracle only |
| Timing | Integer microsecond envelopes; computation-before-transmission; tick-boundary actuation | Physical timing remains quantized by the 10 ms barrier |
| Perception | Radar FD 32-byte object list, four slots, independent range/fusion checks | Synthetic measurements, not camera/lidar perception |
| Projects | Actual JSON `.vcanproj` v1; C++ canonical SHA-256; transactional import | Not the older proposed ZIP format; not signed/encrypted |
| Experiments | A/B frames+envelopes equality over 60 ticks | Preserve this regression, do not claim complete persistent replay |
| Performance | C++ per-ECU/bus windows, percentiles and dashboard tab | Bounded in-memory windows, not a durable full-run recorder |
| Verification | Several named Python tests, 42 C++ cases via local shim | Genuine CMake/GTest/CTest and Docker remain unproven locally in that checkpoint |

The supplied README describes seven active ECUs, one spare and a main container. C++ owns core simulation, while Python still owns HTTP orchestration/adapters. The browser is the user-facing application. Thus the platform is a useful distributed SIL foundation, not yet a complete native C++ desktop/3D driving application.

## GitHub mismatch - first blocking gate

Using the connected GitHub plugin, `feature` was inspected at `2a2c9149204e65455236152162b39f7bd4656a04` (12 October). Its tree does not contain the checkpoint's `headers/IsoTp.hpp`, `src/IsoTp.cpp`, `src/Sha256.cpp`, `resources/schemas/event-envelope.v2.json` or new P0 tests; root CHECKPOINT.md is absent there. Only master and feature were returned by the branch listing.

This does NOT prove local work was lost. It means the supplied checkpoint describes work not present in the inspected remote branch. Do not overwrite the user's local P0 checkout with that remote snapshot. Task BASE-00 must identify the local commit/worktree, preserve changes, include the real implementation in a reproducible source baseline and record its SHA. If source is unavailable, BASE-00 is BLOCKED and runtime migration waits. Documentation/contract-only work can proceed independently.

## Known correctness debt to carry forward

- Input freshness in the checkpoint is based on reception tick. A delayed old radar packet can appear fresh. Preserve acquisition timestamps through the broker and test age at the consumer.
- The checkpoint requires computed time inside the current tick. A task/resource scheduler with cross-tick jobs needs an explicit job/event contract rather than faking all work as in-window cost.
- The entire sample-to-applied chain is tick-quantized. Lower task cost alone does not necessarily lower final 10 ms chain latency.
- Full task-level CPU scheduling, calibrated MCU profiles and sustained memory/queue accounting remain extensions, even though coarse budgets/FIFO/watchdog models exist.
- Embedded diagnostic return shape differs from the distributed path according to the checkpoint; native migration must resolve or explicitly preserve compatibility.
- Node/jsdom checks were not locally rerun in the checkpoint. UI project/hash/performance behavior needs focused native/UI tests later.

## Evidence discipline

This pack has not rerun the P0 application or an actual GPU/3D simulator. Its own coordinator tests use temporary Git repositories and fake provider executables. They validate scheduling/handoff mechanics, not native driving correctness or account access. See VALIDATION.md for fresh results generated for this pack. The October 22 deadline has elapsed; use the gates in the new roadmap and agree a fresh schedule after BASE-00/BASE-01.
