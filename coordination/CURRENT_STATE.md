# Current native platform state

Updated 3 November 2026. The local P0 source has been preserved and the native
C++20 controller/workers, Qt desktop and interactive driving workflow are built
and tested. The old remote source mismatch is historical; do not replace this
checkout with that older source. The original assessment remains in
[sources/STARTING_STATE.md](sources/STARTING_STATE.md).

Start with [../docs/PLATFORM_STATUS.md](../docs/PLATFORM_STATUS.md) and
[completions.json](completions.json). The compact pre-cleanup result is 81 CTest
entries passed, real Wayland UI, ten-minute following soak and corrected pacing.
The native product branch is `platform/native`; the optional browser client is
`showcase/web`. Production simulation stays in C++; web is an optional adapter.

The delivered UI, traffic and mesh workflow does not establish esmini/CARLA,
tracking/MPC, full durable event recording/replay, container release or standalone
installer gates. Keep those tasks open. Retain P0 CAN/FD/ISO-TP, timestamp,
project/hash and oracle regressions. Never remove active tests merely because
the corresponding implementation is complete.

The task database is local under `.orchestrator`; task definitions and a manual
completion ledger are tracked under `coordination`. Completion records identify
the tested source and evidence and do not invent independent provider approval.
Stale clean attempt worktrees were removed with Git history retained; dirty
attempts and the control integration worktree remain. Read actual status before
resuming coordination. Historical immutable task criteria remain available even
when a delivered feature has a narrower current-product completion record.
