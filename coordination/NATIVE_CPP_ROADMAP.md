# Escalation roadmap: preserve, extract, extend, demonstrate

There is no honest new calendar deadline until the reported P0 source is recovered and built. Milestones are gated; tasks.json gives the smaller independently reviewable slices. Multiple providers may work on non-conflicting ready tasks, but dependent interfaces cannot be guessed.

| Gate | Required result | Exit evidence |
| --- | --- | --- |
| G0 Source baseline | Local P0.1-P0.6 implementation identified, committed and compared to supplied files | SHA, clean/reviewable source, baseline report; no overwritten local work |
| G1 Genuine regression | CMake/GoogleTest/CTest plus P0 Python/vector/A-B checks; container test on a capable host | Exact commands, failures/skips and CI link; shim results labelled historical |
| G2 Native app skeleton | C++ application service + CLI with no Python production controller | Same project/commands and frame/envelope signatures as baseline |
| G3 Timing/resources | Delayed acquisition age and cross-tick jobs/limits modelled correctly | Slow CPU, memory, queue, stale/drop and epoch/retry tests |
| G4 Driving state/logs | 2D pose/steering/P-N-D plant; topology bindings; persistent C++ recorder | Physical response and complete correlated run files |
| G5 Baseline tracking | A reference path is followed through ECU/CAN requests | Straight, circle and lane-change cases; reference/estimated/actual plots |
| G6 Native 3D | esmini world synchronized with one ego authority and a native UI | Drive/control car, world-step/pose tests, real 3D run log and manual evidence |
| G7 MPC experiment | Constrained C++ MPC versus baseline on the same scenario/project | Solver-failure tests, resource/latency variants, declared tracking metrics |
| G8 Release/cleanup | Native project lifecycle/replay and optional web adapter parity | Clean install, round-trip, crash log, replay and approved removal list |

## Preserve P0

Do not reimplement what the checkpoint already completed. Keep the C++ IsoTp session, the CanTp adapter, event-envelope v2, SHA-256, transactional project import, derived CAN map, fusion layout equality, A/B signatures and performance windows. Baseline tests are regression gates during extraction.

## Extraction order

1. Freeze typed clock, snapshot, command, job, WorldFrame and recorder contracts in an ADR.
2. Wrap/extract the core without behavior changes. Do not combine refactoring with a new plant or MPC.
3. Port controller/barrier/worker transport and diagnostic framing dispatch to C++. Run C++ workers in real separate containers. Keep native-process mode as development parity.
4. Add a native C++ CLI owning project/run lifecycle; establish a native-only smoke experiment.
5. Fix acquisition-age semantics and introduce resource events, including jobs spanning ticks. Preserve the old profile through an explicit compatibility mode if required.
6. Add lateral pose, EPS/transmission roles and durable logs; then path tracking and world adapters.

## Parallel lanes after G0

OpenCode normally implements runtime/plant/world slices. Copilot normally builds Qt components against accepted contracts. Codex normally builds independent verification/CI and reviews. These are defaults; capabilities and account availability decide a particular claim. Copilot can work on a labelled UI fixture while native transport progresses, but cannot mark real integration complete.

CAN XL, DoIP/J1939, signed/encrypted projects, raw camera perception, ROS, HIL and full ADS remain backlog items. The current user goal puts native C++/3D ahead of those protocol expansions. Preserve their decisions and do not claim they were cancelled. AI remains offline/advisory until a separately accepted authority contract permits more.

## Tracking evaluation

Start with low-speed straight and 25 m-radius circle tests inspired by the reference, then a lane change and a stop obstacle. Commit scenario geometry, speed, friction, localization noise and resource budgets. Report lateral/heading/speed error, solver timing/invalid status, actuation age, safety interventions and energy estimate where meaningful. Choose explicit quantitative acceptance targets before comparing controllers; they are experiment goals, not universal vehicle-safety limits.

Run identical hashed projects/seeds for baseline and MPC. Plot actual and estimated trajectories separately. Include low-resource/delayed-CAN/stale-localization variants and failed cases. A visually smooth car or an LLM's explanation is not validation.
