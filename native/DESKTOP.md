# Native desktop laboratory

The default **Simulation** workspace opens the interactive traffic experiment
with live 2D health, adjustable following, project switching and custom car meshes.
See the [interactive platform guide](../docs/INTERACTIVE_NATIVE_PLATFORM.md).

More driving/environment controls and a grip-aware physics profile are available
in the Simulation sidebar. See [physics controls, equations and model limits](PHYSICS.md).
Use **Vehicle Physics → Grip-aware bicycle v2 → Apply physics & restart** to
enable mass, wind/slope forces, tire-force limits and steering-rate dynamics.

Repair build: continuous Run is now paced by the native C++ controller using
`--realtime`. The UI polls snapshots and never supplies a second clock. The
default CLI without that flag keeps deterministic manual stepping. Restoring a
halted ECU followed by Run retries the frozen barrier once; a dead worker is
reported as requiring a project restart. Rejected edits do not stop a run.

The Workroom now supports dragging ECU nodes; wires follow the moved nodes.
Positions are stored as optional `presentation.node_offsets` project metadata,
preserved by the native controller on import/export and project restart. Moving
nodes leaves the configuration hash and workers unchanged.

**Edit topology** opens an editable project definition with **Add ECU**, **Remove
selected ECU**, **Add CAN bus**, **Connect selected ECU message**, **Remove unused
bus**, **Validate**, and **Apply & restart**. Select the target ECU in Workroom
before opening connection/removal actions. The added ECU template counts ticks
and transmits CounterStatus on a free CAN ID. Validation catches missing buses,
FD incompatibility, signal/reference errors and actuator/producer conflicts.
The advanced JSON view also exposes gateway routes, bus speeds and message
layouts. Drafts affect the experiment only after Apply; save the deployed project
using Save project. An unused bus can be removed after all messages/routes that
reference it have been disconnected or edited.

Errors remain visible as **Last error** in all workspace windows, even after
telemetry refresh. **Reconnect runtime** stops the old runtime and restores the
last acknowledged project at tick zero with fresh workers. It retains program
draft text, and stops any telemetry recording. It does not restore the previous
live vehicle state. Topology/program drafts trigger a discard prompt on closing
the main application. CPU values in the hardware form now match the runtime's
1–2000 MHz range.

The desktop drives the real C++ controller and one C++ worker process per ECU.
The default interactive scenario has nine workers: the original seven plus EPS and
transmission. No Python controller or browser is required at runtime.

From the repository root, launch the built application:

```sh
bash tools/run_native_desktop.sh
```

The application opens in **3 Simulation**. The top tabs also expose **1 ECU
workroom** and **2 ECU programming**. Each workspace scrolls horizontally and
vertically when the window is small; simulation controls have their own scroll
area. Wheel gestures over sliders, selectors and numeric fields scroll the
workspace instead of changing a vehicle input. Use keyboard/click/drag to edit.

**Open current workspace in window** detaches the selected page. **Open all
three windows** shows the workroom, programming and simulation separately. They
share one controller, clock, project and set of workers. Closing a detached
window returns its page to the tab. To open all three immediately:

```sh
bash tools/run_native_desktop.sh --windows
# Or choose a single initial workspace:
bash tools/run_native_desktop.sh --workspace programming
```

## Workroom and programming

The workroom schema is built from the loaded ECU definitions and CAN bus
configuration. Select a node (or its name in the inspector) to see inputs,
message IDs, signals and actuator bindings. The node displays its real worker
PID and execution state. Use Zoom, Fit, scroll, or drag to navigate the schema.
**Open ECU program** selects that ECU in the programming workspace.

**Halt worker** pauses execution; stepping while halted reports a worker failure
without advancing the simulation. **Restore worker** allows that pending tick
to complete. **Apply traffic policy** adds a delay and/or drops the selected
ECU's outgoing frames. It replaces that ECU's source-specific rules while
preserving other rules; **Clear this ECU's traffic rules** removes them. Active
rules are displayed, and simulation loss/performance counters show their impact.

The program editor loads the executable **runtime JSON language**, including
task expressions, state-machine transitions, CAN messages, bindings and modeled
hardware budgets. It does not compile arbitrary C/C++ firmware. Edit the JSON
directly, or write CPU/RAM/flash fields into the draft with the hardware form.
Drafts survive switching ECUs, workspaces and detached windows. Save a draft to
JSON to keep it across application restarts; closing with undeployed changes
requires a discard decision.

**Validate** checks the complete candidate project in an isolated native core,
including signal layouts, references, CAN collisions and actuator ownership.
It leaves the live workers and state unchanged. **Deploy & restart** pauses the
experiment, validates, derives the new CAN map and project hash in C++, stages
new worker processes, and restarts the edited project at tick zero. A rejected
program does not replace the live program. Save the project to preserve deployed
edits. **Restart project** retains deployed programs and traffic rules.

For a small visible experiment, select EPS ECU and replace the `steering_angle`
assignment in the `assist` task with `0.2`. Validate and deploy, then step the
simulation: the applied wheel angle becomes 0.2 radians (about 11.5 degrees).
This intentionally replaces driver-dependent steering with a constant output.
Restore the original program or reopen the original project to restore manual
steering. Hardware values remain modeled assumptions, not MCU calibration.

Select **D**, raise **Throttle**, then press **Run**. Change **Steer** to turn;
**Brake + throttle zero** decelerates through the existing actuator path.
**Pause** stops advancing simulation time. **Step 10 ms** advances exactly one
tick. Park is rejected while moving. Reverse remains unsupported.

The perspective canvas and top-down trace read the authoritative plant state.
They do not integrate their own vehicle. This is a synthetic ground-plane view,
not an esmini/OpenSCENARIO integration. Existing radar/ADAS measurements retain
the longitudinal educational scenario semantics; the road drawing is not lane
perception. Steering uses the EPS input/binding path; transmission drive permit
is broadcast over CAN. This is not yet a CAN-commanded path tracker or MPC.

The tabs show connected worker PIDs and DTCs (hover for details), the latest
60 CAN/CAN FD frames, and simulated task/delivery/end-to-end p95 latency in µs.
Host command response time is shown separately from simulated latency. The
front end sends at most one command at a time and coalesces slider changes. The
native controller independently advances fixed 10 ms ticks on paced deadlines;
the UI polls snapshots. Slow hosts run slower than
wall time; the UI never fabricates missing ticks. The 32-command queue and
20-second response deadline bound controller interactions.

**Open/Save project** uses native `.vcanproj` v1 validation and SHA-256 hashing.
**Read VIN** exchanges actual ISO-TP frames with the chosen worker. Faults are
injected into the selected sensor; **stuck** and **bias** use the editable value
in that sensor's units. The value field is disabled for modes that do not use it.
The **none** selection clears the fault. **Restart project** retains the currently
loaded project and deployed programs while resetting the experiment state.

**Record telemetry** writes displayed snapshots to a new JSONL file, including
tick, epoch, project hash, world, controls, and compact performance statistics.
It flushes each sample and stops with an error on write failure. Existing files
are never overwritten. This is sampled telemetry, not complete bus recording,
crash-safe recording, or persistent replay. Open/reset stops recording. Complete
durable event recording remains a separate roadmap item.

## Build and verify

Qt 6 Widgets and Assimp are optional desktop dependencies; headless products
keep working without them. Install a system Assimp development package or use
`bash tools/fetch_native_mesh_deps.sh` on this Ubuntu host. On this
machine Qt is installed at `$HOME/Qt/6.8.3/gcc_64`:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DVEHICLE_LAB_DESKTOP=ON -DVEHICLE_LAB_P0_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
cmake --build build/native -j3
ctest --test-dir build/native --output-on-failure -j3
```

The tests require GoogleTest, Qt Test and Python/jsonschema. For products only, use
`-DBUILD_TESTING=OFF`. Use a system Qt install or your own `CMAKE_PREFIX_PATH`
on other machines. Linux process transport is currently required. An installed
binary needs its sibling `vclab`/`vclab_worker`, Qt/Assimp shared libraries, and `--root`
pointing to the resource checkout; a standalone distributable is not yet built.

Generate screenshots and machine-readable driving evidence in a **new** folder:

```sh
./build/native/vehicle_lab_desktop -platform offscreen \
  --scenario resources/scenarios/ev_lateral.json --smoke build/native/my-demo
```

This runs 620 simulation ticks, accelerates/turns/brakes through nine workers,
reads the motor VIN, and saves PNGs, a project, telemetry, final state and summary.
It exits nonzero on a failed drive or controller error. The CTest desktop test
also checks project reload, worker cleanup, startup failure, and overwrite
protection. Offscreen rendering is tested separately from an interactive display.

`NativeDesktop.WorkspacesAndProgramming` drives the real Qt widgets and native
processes. It checks 800×600 navigation/scrolling, draft retention, validation,
program deployment with physical steering impact, immediate input-before-step,
run/pause, traffic drops, worker halt/restore, sensor faults, program persistence,
and shared windows with reattachment. Set `VCLAB_WORKSPACE_EVIDENCE` to a directory
when running `test_desktop_workspaces -platform offscreen` to save all three
workspace screenshots.

The original web application, P0 scenario, protocol vectors and all unselected
candidate worktrees remain available. Native runtime source was recovered from
`work/native-cpp` at `0b239e5`; lateral plant source from preserved `d67074b`.
