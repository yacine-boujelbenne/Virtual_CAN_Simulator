# Target: a native C++ application with a virtual vehicle

Supplemental reference review: [TI ADAS communications and official CarSim](REFERENCE_ADAS_CARSIM.md). It clarifies future sensor/data links and the optional direct C/C++ CarSim SDK path; the architecture and current task priorities remain unchanged.

## Executables and boundaries

| Component | Target responsibility |
| --- | --- |
| vehicle_lab_cli | C++ application service, project lifecycle, clock, ECU coordination, broker, experiment runner and recorder; works headless |
| vehicle_lab_desktop | Qt 6 C++ desktop UI connected to the same application service, not a second simulation |
| ecu_worker | One C++ kernel/resource model and native worker transport per container; configure/tick/diagnostic/fault contracts |
| world adapter | C++ IWorldBackend; first null/headless + esmini; optional CARLA later |
| controller plugins | C++ path tracking (baseline then MPC), longitudinal controller and deterministic safety supervisor |
| optional web client | Demo/remote inspection API over the same C++ commands/snapshots; existing web UI retained through migration |
| development tools | Python oracle, analysis and multi-provider coordinator; not required for production simulation |

Proposed target directories: `native/core`, `native/transport`, `native/world`, `native/control`, `native/recording`, `native/ui`, and `tests/native`. Do not move all existing source at once. First expose narrow C++ facades around the tested code, then extract a module per accepted slice. Keep project/codec public behavior stable.

## One closed loop

At time t, the world and authoritative ego plant provide physical state. Sensor models sample it, applying quantization, noise, validity and delay. ECU jobs consume sampled inputs and delivered CAN frames under virtual resource limits. Only completed jobs can publish frames. Localization/perception ECU outputs feed a path tracker; speed/steering requests go through control/driver gateway messages, then Motor/ABS/EPS/transmission. The sole plant commits the resulting actuator state at t+dt. The world advances its non-ego actors once and the externally controlled ego pose is synchronized. Append immutable events and state to the run log.

The renderer reads state and never advances physics. UI input is a timestamped command, not a second controller writing world variables. Controllers do not read perfect x/y/heading unless an explicitly labelled ideal-localization experiment enables that sensor mode. Always log ground truth for evaluation, and estimated state/age separately.

## 3D backend choice

Start with a null backend for CI and an esmini adapter for a small road/traffic/viewer environment. esmini exposes a shared library/C API and OpenSCENARIO/OpenDRIVE-oriented tooling. Its API includes initialization, fixed-dt stepping, object state and external pose reporting. Pin a version and verify its external ego control behavior before implementation. Initially the esmini viewer may be a separate native window owned/launched by the application. Do not promise portable widget embedding until a tested integration supports it.

The C++ plant remains ego authority in this first profile. esmini may animate other actors/road context, but its default controller must not simultaneously integrate ego. Coordinate conversions (axes, heading, road-wheel angle, metres/radians/seconds), ego ID and sample order are explicit in a WorldFrame contract. Check that one application step creates exactly one world step; model reset must reset both.

CARLA is an optional later backend when richer sensors/physics and suitable hardware are available. In its physics-authoritative profile, CARLA replaces the internal ego plant; do not run both. Use synchronous fixed-step operation, one tick owner, declared substeps/sensor periods and explicit frame correlation. Translate between CARLA coordinates and core coordinates at the adapter boundary. A GPU/engine-dependent test job is separate from headless CI; a skipped CARLA job is not a passed 3D gate.

CarSim/Simulink is an optional licensed comparison backend, not a default dependency. The user reference is trajectory TRACKING: it follows a chosen path, rather than detecting a road trajectory from raw images. If perception-based lane detection is wanted later, it is a separate sensor/localization/planning stage.

## Control design

Use SI units internally. State includes x/y/z, yaw, longitudinal/lateral speed as supported, steering and actuator/gear state. Reference samples contain simulation time, position, heading, curvature and desired speed. Start with a deterministic longitudinal PI/PID and a simple lateral tracker as a baseline. Then implement MPC in new C++ code with Eigen plus a pinned maintained solver such as OSQP through its C API. Check solver status, finiteness, bounds, time budget and fallback before applying the first control move.

Borrow the reference's concept of a preview horizon, state error, control increments and input/rate constraints. Do not translate its code line-for-line. The source sets Nx=3, Nu=2, 100 ms sampling and a 25 m / 5 m/s circle; these are demonstration parameters, not validated settings for this vehicle. Its speed-increment lower and upper bound are both positive 0.05, and its yaw/control equations require independent dimensional review. Re-derive the model, angle wrapping, wheelbase terms and objective scaling, and test infeasible/time-limited QPs. `my_quadprog.m` contains a MathWorks copyright notice; the repository tree had no LICENSE. Do not import that solver/code into the project without an applicable license.

MPC produces desired acceleration/curvature or bounded wheel-angle/speed targets. Low-level C++ control translates these through actuator ECU/CAN paths. Brake arbitration, ignition/BMS inhibit, stale-input rejection and saturation are retained. Gear selection first supports a physically meaningful P/N/D EV profile; reverse/multi-speed/clutch behavior needs additional plant and tests.

## Resource/clock fidelity

Keep virtual execution costs, job release/finish, bounded RAM/flash/buffers, CAN queues and sensor/actuator delays inside the modeled ECU contract. Wall-clock worker transport and rendering overhead are host measurements. Preserve acquisition time across delayed reception. Label assumed versus calibrated profiles; a 120 MHz declaration cannot establish cycle-accurate MCU behavior. Record the plant's temporal resolution and every cross-tick completion. One project hash/run manifest binds all resource and control parameters.

## Experiment recorder

Create a C++ RunRecorder before claiming full-run experiments. Every run has immutable project/build/resource/backend/controller identifiers, seed, timing mode and failure status. Stream `events.jsonl`, `can.jsonl`, `trajectory.csv`, `controls.csv`, `performance.csv`, `manifest.json` and `summary.json`. Logs correlate command -> task -> CAN -> actuator using sequence/epoch IDs. Include reference versus actual versus estimated trajectory, tracking errors, solver status, watchdogs and loss/age statistics.

Use a bounded recorder queue and an explicit saturation policy. Do not silently drop measurement records: mark timing/result integrity invalid, pause or abort according to the profile, and preserve a partial run. Flush on stop/reset/shutdown; distinguish a clean summary from a crash/incomplete run. Persistent replay is an additional gate, not just the current A/B signature. Store generated runs/assets outside versioned source; retain manifests and small golden fixtures.
