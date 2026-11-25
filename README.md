# ECU Platform — Virtual CAN / Vehicle Lab

Build a virtual vehicle network, program its electronic control units (ECUs), and watch their CAN messages affect a running vehicle. Vehicle Lab is a **C++20 simulation platform for Linux** with a Qt desktop, a headless command-line runner, and an optional browser showcase.

Use it to explore vehicle control, CAN/CAN FD communication, sensor faults, diagnostics, and timing without physical ECU hardware. Each ECU runs in its own native process. One C++ controller owns the simulation clock, vehicle model, and message delivery.

**This checkout: `platform/native` — the primary desktop and engine application.** The optional browser client lives on `showcase/web`.

[Screenshots](#screenshots) · [Setup](#setup-and-launch) · [First experiment](#try-your-first-experiment) · [Headless mode](#headless-mode) · [Tests](#build-and-run-tests) · [Troubleshooting](#troubleshooting)

## What you can do

| Area | Capabilities |
| --- | --- |
| ECU workroom | Inspect and move ECU nodes, edit topology, add ECUs and CAN buses, and inspect worker status and process IDs. |
| ECU programming | Edit runtime JSON tasks, state machines, signals, and hardware budgets; validate changes, then deploy and restart. The editor does not compile arbitrary C/C++ firmware. |
| Driving simulation | Control throttle, brake, steering, and P/N/D gears; view motion in a 3D perspective and top-down trace. |
| Traffic and following | Add cars, change their speed and position, and tune cruise speed, time gap, standstill gap, and proportional following gain. |
| Physics and health | Experiment with grip, mass, drag, wind, slope, and steering dynamics; monitor temperatures, charge, tire demand, and warnings. |
| CAN and diagnostics | Inspect CAN/CAN FD frames, task and delivery latency, sensor faults, traffic delay/drop policies, and ISO-TP exchanges such as reading a VIN. |
| Projects and telemetry | Save/load `.vcanproj` configurations, switch experiments, and record sampled snapshots as JSONL. |
| Vehicle appearance | Import STL, OBJ, glTF, or GLB meshes and save their visual geometry with a project. |

## Screenshots

### Simulation and live vehicle health

The Simulation workspace combines driver controls, traffic, vehicle health, a 3D view, and inspection tabs.

![Native Simulation workspace with vehicle health, driving controls, and a traffic actor](docs/images/native-platform.png)

### Driving through the native ECU network

Captured from the application's offscreen driving demo: steering changes the trajectory, CAN frames accumulate, and all nine ECU workers remain connected. The demo pauses at tick boundaries while capturing evidence.

![Native driving demo at 39.8 km/h, showing steering, trajectory, CAN frames, and connected ECU workers](docs/images/native-driving.png)

## How it works

```text
Qt desktop / headless JSON commands / optional browser adapter
                              |
                              v
                   Native C++ controller
          clock + vehicle model + CAN/CAN FD broker
                              |
                    tick synchronization
                              |
            one native worker process for each ECU
```

The interactive vehicle includes Motor, ABS, BMS, Dynamics, Instrument Cluster, Radar, ADAS, EPS, and Transmission ECUs. The controller advances fixed simulation ticks and exchanges inputs, frames, and outputs with the workers. The UI displays snapshots; it does not run a separate vehicle model or advance a second clock.

## Setup and launch

### 1. Open the native source checkout

Use the `platform/native` checkout containing `CMakeLists.txt`, `native/`, `resources/`, and `tools/run_native_desktop.sh`. Run the following commands from that repository root:

```sh
cd Virtual_CAN_Simulator
```

The parent `ECU_Platform` workspace is not the CMake source directory. If obtaining the project from Git, use a published copy of `platform/native`; older repository branches may not contain this application. An existing native checkout is sufficient for every step below.

### 2. Install build dependencies

Requirements: **Linux**, a **C++20 compiler**, **CMake 3.16+**, **Qt 6.2+ Widgets**, and **Assimp**. The current worker transport depends on Linux process facilities. Docker, Python, and Node.js are not required to run the native application.

On Ubuntu/Debian with these packages available:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev libassimp-dev
```

For a separately installed Qt SDK, add `-DCMAKE_PREFIX_PATH="/path/to/Qt/version/gcc_64"` to the configure command below. This development machine uses `$HOME/Qt/6.8.3/gcc_64`; replace that path for your installation.

### 3. Configure and build

This builds the application without requiring test dependencies:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DVEHICLE_LAB_DESKTOP=ON -DBUILD_TESTING=OFF
cmake --build build/native -j2
```

The resulting executables are `build/native/vehicle_lab_desktop`, `build/native/vclab`, and `build/native/vclab_worker`. Keep them together: the desktop launches the controller, which launches the workers.

### 4. Launch the desktop

```sh
bash tools/run_native_desktop.sh
```

The application opens the interactive traffic project in **3 Simulation**, paused. The other tabs are **1 ECU workroom** and **2 ECU programming**. The status bar should show **9 / 9 ECU workers connected** for the built-in interactive scenario.

Optional launch modes:

```sh
# Prepared following experiment: press Run after opening
bash tools/run_native_desktop.sh --scenario resources/scenarios/ev_following_demo.json

# Three separate workspace windows sharing the same experiment
bash tools/run_native_desktop.sh --windows

# Start directly in the program editor
bash tools/run_native_desktop.sh --workspace programming
```

## Try your first experiment

1. Launch the prepared following scenario with the command above. It starts paused in Drive, with the ego vehicle behind a slower lead car.
2. Press **Run** and watch speed, lead distance, brake demand, and CAN traffic change. **Pause** freezes simulation time; **Step 10 ms** advances one tick.
3. In **Interactive Traffic**, adjust the lead car's target speed or add another car. Change following settings and press **Apply adaptive following**.
4. Open **ECU workroom** to inspect Radar and ADAS, their signals, and their worker status. Use the Simulation inspection tabs for CAN frames, performance, diagnostics, and live signals.
5. Press **Save project** to keep the deployed configuration as a `.vcanproj`. Use **Open project** to reload it, or open the supplied `resources/projects/following_demo.vcanproj`.

For manual driving, open the default interactive scenario, select **D**, raise **Throttle**, and press **Run**. Adjust **Steer** to turn and use **Brake + throttle zero** to slow down. Manual braking cancels cruise authority. Reverse is not implemented.

Program changes take effect only after **Validate** and **Deploy & restart**. Saving a project preserves deployed programs and configuration; loading/restarting resets live motion and time. **Record telemetry** saves sampled observations, not a replayable recording of every event.

See the [interactive guide](docs/INTERACTIVE_NATIVE_PLATFORM.md) for following, health, project behavior, and mesh import, or the [desktop guide](native/DESKTOP.md) for topology editing and ECU programming.

## Headless mode

Build the controller and workers without Qt or Assimp, using a separate build directory:

```sh
cmake -S . -B build/headless -DCMAKE_BUILD_TYPE=Release \
  -DVEHICLE_LAB_DESKTOP=OFF -DBUILD_TESTING=OFF
cmake --build build/headless -j2

./build/headless/vclab \
  --scenario resources/scenarios/ev_following_demo.json \
  --steps 100 --save-project build/headless/following.vcanproj

./build/headless/vclab --project build/headless/following.vcanproj --steps 100
```

Each run prints a JSON snapshot and exits. `--steps` runs a fixed number of simulation ticks without wall-clock pacing. Without it, `vclab` accepts one JSON command per line on standard input; `--realtime` enables controller-paced execution after a `run` command. See the [headless API and worker guide](native/README.md).

## Build and run tests

Tests additionally need GoogleTest, Qt Test for desktop checks, and Python with `jsonschema` for the P0 regression drivers. On Ubuntu/Debian:

```sh
sudo apt-get install -y libgtest-dev python3 python3-jsonschema

cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DVEHICLE_LAB_DESKTOP=ON -DBUILD_TESTING=ON \
  -DVEHICLE_LAB_P0_TESTS=ON
cmake --build build/native -j2
ctest --test-dir build/native --output-on-failure --no-tests=error -j2
```

Add your Qt SDK prefix again if using a separate SDK. The `qt6-base-dev` package supplies Qt Test with the system Qt build.

To run the driving/steering/braking and VIN smoke demo without a display, choose an output directory that does not already exist:

```sh
./build/native/vehicle_lab_desktop -platform offscreen \
  --scenario resources/scenarios/ev_lateral.json \
  --smoke build/native/readme-demo
```

The demo writes screenshots, a saved project, sampled telemetry, diagnostic output, and `summary.json`. It exits nonzero if its driving checks fail. Offscreen checks do not validate an interactive display session.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Launcher says to build the desktop first | Configure with `VEHICLE_LAB_DESKTOP=ON` and build in `build/native`; the launcher expects that location. |
| CMake cannot find Qt6 | Install Qt 6 development packages or set `CMAKE_PREFIX_PATH` to the directory containing your Qt SDK's `lib/cmake`. |
| CMake cannot find Assimp | Install `libassimp-dev`. The Ubuntu-specific `tools/fetch_native_mesh_deps.sh` is an alternative for the development host; its package names depend on the Ubuntu release. |
| CMake cannot find GoogleTest | Install `libgtest-dev`, or configure with `BUILD_TESTING=OFF` if you only want to run the application. |
| Qt cannot connect to a display | Launch inside a graphical Linux session. Use `-platform offscreen` with the smoke demo on a headless host. |
| Worker startup fails or no ECUs connect | Keep `vclab_worker` beside `vclab`, retain executable permissions, and use an environment that permits child processes and local IPC. |
| Scenario or template cannot be found | Run CLI commands from the repository root. The desktop launcher changes to that root automatically. |
| Project changes disappear after closing | Save deployed changes with **Save project**. Session project slots and undeployed editor drafts are not durable project files. |

## Optional browser showcase

The separate `showcase/web` checkout contains `showcase/README.md` and `tools/run_web_showcase.sh`. It serves a browser UI through a Python standard-library adapter backed by `vclab --realtime`. Each launch starts an independent native experiment; it does not attach to an already running desktop.

No npm packages are needed to operate it. Node.js and Playwright are used only for browser verification. See the README in that checkout for its setup. The older `web/` and Compose adapters are documented separately in the [legacy guide](docs/LEGACY_WEB.md).

## Repository and documentation

| Path | Purpose |
| --- | --- |
| `app/`, `headers/`, `src/` | Executable entry points, protocol code, and simulation engine. |
| `native/` | Controller, worker transport, runtime contracts, and Qt desktop. |
| `resources/` | ECU definitions, scenarios, schemas, example projects, and vehicle assets. |
| `tests/` | Protocol, runtime, desktop, and development-tool regressions. |
| `tools/` | Launchers, dependency helpers, and development tools. |
| `docs/` | Architecture, user guides, screenshots, and verification records. |
| `coordination/` | Roadmap, decisions, task records, and source provenance. |

- [Architecture](docs/ARCHITECTURE.md)
- [Physics, controls, and model equations](native/PHYSICS.md)
- [Platform status](docs/PLATFORM_STATUS.md) and [recorded native verification](docs/verification/native.json)
- [Remaining roadmap](coordination/NATIVE_CPP_ROADMAP.md)
- [Default vehicle asset provenance and CC0 notice](resources/models/default-car/PROVENANCE.md)

## Scope and current limits

The vehicle model is educational and planar. The 3D view visualizes that model; custom meshes do not change collision geometry or physical parameters. Contacts use rectangular footprints, and body pitch/roll are visual cues. Hardware budgets are modeled values rather than calibrated MCU measurements.

Full durable recording/replay, external esmini/CARLA integration, path tracking/MPC, and standalone installers remain roadmap items. The repository's dated verification records describe previous runs; use the commands above to validate your own build.
