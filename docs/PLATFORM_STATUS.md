# Platform status — 3 November 2026

The primary product is `platform/native`: C++20 engine/controller/workers and a
live Qt desktop. `showcase/web` is an optional client of that engine. Python is
used for development tests, coordination and the optional HTTP edge adapter;
it does not own the showcase simulation clock or physics.

## Delivered and verified

- One C++ controller and nine isolated ECU workers; native process/HTTP parity,
  CAN/CAN FD, ISO-TP diagnostics, event envelopes and canonical project hashes.
- Acquisition age and cross-tick resource jobs; worker faults/retry/reset.
- Editable ECU topology/programs with validation/deployment and project restart.
- Three scrollable/detachable workspaces; compact layout fits health and the full
  car view on the tested 1366 × 698 desktop window.
- Live 2D health/stress and all-signal inspection; driver/environment/physics
  controls, thermal/SOC injection, grip, drivetrain, braking and steering settings.
- Native geometric traffic, actor editing and adjustable proportional following
  through radar/ADAS/CAN actuation; latched contact history.
- Kenney CC0 sedan, wheel animation, orbit/zoom, Assimp custom mesh import and
  embedded shape persistence in saved projects.
- Project switching, durable project files and sampled JSONL telemetry.

The pre-cleanup Release validation passed **81/81 CTest entries**. The suite
retains protocol/P0/oracle, runtime process/HTTP, fault/diagnostic, physics and
actual Qt UI/file-dialog checks. Post-cleanup validation also passed **81/81 entries in 31.58 seconds**, and
58 coordinator unit tests passed.
Compact measured results live in [verification/native.json](verification/native.json).
A 602.86-second host soak preserved nine connected workers and stable Pause.
After correcting tick pacing, a 120.43-second run averaged 97.66 ticks per host
second against a 100 Hz target. Following settled at 8.00033 m/s behind an 8 m/s
lead, with a 19.4631 m minimum sampled gap. These are local model outputs.

## Current limits and open work

The plant is planar and educational. Visual body pitch/roll are cues, and mesh
shape does not define collision mechanics. There is no suspension, deformable
crash or calibrated 3D road-contact model. Actors follow straight kinematic paths;
reposition explicitly teleports them. Import supports at most 20,000 faces and
32 MiB per source; materials are sampled to face colors.

Session project slots are in memory; Save/Open files persist configurations.
Telemetry is sampled; complete runtime event/CAN recording and deterministic
replay remain open. esmini/CARLA integration, path tracking/MPC, container release
gates, portable installation and independent candidate reviews retain their
existing task entries. Completed native UI features do not close those gates.

## Repository maintenance

Old clean attempt worktrees, duplicate build/evidence trees, caches, obsolete
repair notes, debug tracing and a dead pre-CMake test were removed. A compact
verification record, one product screenshot and reproducible following fixtures
replace accumulated run logs. Useful regression tests, original P0/oracles,
licenses, references, local settings and dirty worktrees are retained.

Before removal, source, coordinator state, handoffs and dirty attempt patches
were backed up outside the repository in the sibling `maintenance-backups`
directory. Attempt commits have archive refs; seven dirty attempts remain.
The exact removal list is [coordination/cleanup.json](../coordination/cleanup.json).
Completion updates distinguish verified manual delivery from independent review;
[coordination/completions.json](../coordination/completions.json) is the durable
ledger and `.orchestrator/state.sqlite3` remains the live task board.
