# BASE-00: reconciled local P0 source

Recorded 24 October 2026. Implementation evidence is ready for independent review;
this report does not mark the coordinator task DONE or certify runtime correctness.

## Preserved source

The complete P0.1–P0.6 implementation exists in local Git history. Its final source
baseline is **`a4627f14a23808352df0a5a44164c6c96d9e649d`** (performance dashboard). The leased
base is `492a31276c526c205a46bb7ff751e4c3ecca5854` on the user's `feature` branch, also
preserved by the coordinator's local `work/native-cpp` integration branch. Both
commits are reproducible with `git show` or a separate worktree; no reconstruction
from the older source was necessary.

The local remote-tracking ref `origin/feature` resolves to
`2a2c9149204e65455236152162b39f7bd4656a04`. This is the previously inspected older source,
not a fresh network observation. No fetch, pull or remote publication was performed.
It is an ancestor of the P0 baseline, which is an ancestor of the leased base.
`feature` was seven commits ahead of that tracking ref when inspected.

The incremental source history is:

| Slice | Commit |
| --- | --- |
| P0.1/P0.2 envelope schema and timing | `d7dabd9bfea9cac89221f39c96a1882c29df39ea` |
| P0.3 radar/fusion | `a8a49eb6ba0635132a18b6c833b85303732dce60` |
| P0.4 C++ ISO-TP | `072fa890c6b50368c4cba10de1dc2f278ed371c9` |
| P0.5 project/hash and A/B | `0619ab3eeecdc02219f1c43cf3d175349a7b601b` |
| P0.6 performance/final P0 | `a4627f14a23808352df0a5a44164c6c96d9e649d` |

`git diff --exit-code a4627f14a23808352df0a5a44164c6c96d9e649d 492a31276c526c205a46bb7ff751e4c3ecca5854 -- app headers src resources
tests/test_kernel.cpp tests/test_isotp.cpp tests/test_sha256.cpp tests/vectors web
CMakeLists.txt Dockerfile compose.yaml README.md CHECKPOINT.md
.github/workflows/container.yml` exited 0. The application, schemas, vectors,
native tests, dashboard, build and container paths remain byte-for-byte unchanged
since final P0. The subsequent two commits add coordination and reference material.

## Checkpoint provenance and markers

Root `README.md` is identical to `coordination/sources/USER_README.md`, SHA-256
`b151736660d60e7e15a0e91a026daf2025a315ed118cad0661f654468115100b`.
Root `CHECKPOINT.md` is identical to `coordination/sources/USER_CHECKPOINT.md`, SHA-256
`1a167ac72cd005c1f81a6c3ce0f70d971043abb6ed6536cbe2d81b3decb9c57f`.
These match the provenance recorded in `coordination/SOURCES.md`.

All ten markers required by `tools/check_baseline.py` exist at final P0 and the
leased base, are identical between those commits, and match the working files.
None exists in the older remote-tracking commit. Their SHA-256 digests are:

| Marker | SHA-256 |
| --- | --- |
| `headers/IsoTp.hpp` | `52f5f2679a51f729649732543cf7c35555df406fa324029cb630ef4c92f88a49` |
| `src/IsoTp.cpp` | `f892e68d11f6ce8620e3a95d733dca1b700ce464562e7a604573733fd91ea3ea` |
| `headers/Sha256.hpp` | `abff28e76d53240d2e369d91974188e72952bba13dbfdb032dbf76f966afd430` |
| `src/Sha256.cpp` | `238b08f701fac47d0df00120feb3fbfa74ce304ad10ea08c321c315f65378ed7` |
| `resources/schemas/event-envelope.v2.json` | `6d9c3939355e3d81961ad1d9d7da93ae05a72fd12f03ce3171a34ff6c697fbd2` |
| `tests/test_envelopes.py` | `2e892509978191e56c5935ecf16a181f78b3504b5792355659bc0dc8818b68ef` |
| `tests/test_adas_fusion.py` | `e8ef822921dbb75679f9e460c52718ece88cea34067d262ee144a7d2df253a64` |
| `tests/test_isotp_vectors.py` | `fdf6189fd87731f539af293d2d36adee236bd2fe76177e4176a42f4174e87155` |
| `tests/test_project_ab.py` | `3024c3ca9c4302780c2419c226c3fcbb6ca61e64e67e9d0d5ac457a854b4f9fe` |
| `tests/test_performance.py` | `524e8248af42dd418d041675eed86577156a12ddaf2c35075fcd8ac240a26591` |

## Preservation and fresh checks

Initial control checkout: no tracked modifications; untracked `.orchestrator/`
and `docs/Communication Protocols in Modern ADAS Architectures (Rev. A) - slyy219a.pdf`.
The user's PDF and existing source were left untouched. Coordinator initialization
added local exclude entries and created its integration worktree at the existing
HEAD. A local configuration enables only manual Codex participation; it does not
enable automatic provider CLI execution. This implementation changes only this
report in the separately leased `BASE-00` worktree.

Freshly executed:

- `python3 tools/check_baseline.py --verify-p0`: exit 0, no missing markers,
  run in the control checkout and leased worktree. This is a presence gate.
- `python3 tools/validate_coordination.py`: exit 0, 22 valid tasks, run in both
  checkouts. This validates the coordination pack, not vehicle behavior.
- `python3 -m unittest discover -s tests/orchestration -v`: 22 tests passed,
  no skips, in the control checkout. These test temporary-Git/fake-provider
  coordination mechanics; they do not establish authenticated provider operation.
- Exact source diff and both ancestor checks above: exit 0.
- `python3 tools/coordinate.py doctor`: CMake absent; g++, Git, Python and Docker
  executables found. Docker daemon access and container execution were not tested.

The checkpoint's shim/native/Python and cluster results remain historical claims.
No new CMake, GoogleTest, CTest, P0 behavior, Docker or 3D execution is claimed here.

## Handoff

Source reconciliation is established for review; the missing-source blocker in the
initial assessment is resolved by local commit evidence. That assessment is left
unchanged because BASE-00's write scope is this report only. A different provider
must review the exact candidate; only the coordinator may run trusted acceptance
and integrate it. BASE-01 remains dependency-gated until BASE-00 is accepted.

Next after acceptance: BASE-01 must establish genuine CMake/GoogleTest/CTest and
named P0/vector/A-B results, explicitly separating skips and Docker availability.
CMake is currently missing; GoogleTest availability has not been probed. PLAN-00
is independently eligible. No application extraction or behavior change belongs
to this source-reconciliation attempt.
