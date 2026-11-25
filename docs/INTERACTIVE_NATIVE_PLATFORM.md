# Interactive native vehicle laboratory

The default desktop opens the interactive traffic project in Simulation.
See the [platform status](PLATFORM_STATUS.md) for measured following,
run pacing, display checks and regression evidence.

```sh
bash tools/run_native_desktop.sh
# Optional independent workspaces, all sharing one C++ runtime:
bash tools/run_native_desktop.sh --windows
```

## Drive and follow another car

1. Select D. Scroll the left controls to Interactive Traffic.
2. Set a centre distance ahead, lateral offset and target actor speed; Add car
   ahead. Cars move along their heading with bounded acceleration/deceleration.
3. Set cruise cap, following time gap, standstill gap and gap gain. Press Apply
   adaptive following. Release manual brake and press Run. The ADAS ECU uses
   sampled range/closing speed and VehicleStatus CAN messages to regulate throttle
   and brake. Driver brake cancels cruise authority; emergency protection remains
   independently switchable in Environment & Assistance.
4. Adjust an actor's target speed, reposition it or remove it while running.
   A reposition is an explicit teleport for scenario editing, not a lane-change
   physics model. The nearest actor intersecting the forward corridor is selected.
5. Watch lead ID, range, closing speed, ECU target and fractional brake request.
   Nearby cars appear in the 3D view and top-down trace. Adjacent-lane objects do
   not become lead vehicles merely because their X coordinate is smaller.

The standstill gap/time/gain values are experiment settings, not safety guarantees.
The following variant preserves the existing CAN payload layout. Its extra native
ECU tasks produce a target of
`min(cruise_cap, max(0, estimated_lead_speed + gain * gap_error))`, where
`gap_error = measured_range - (standstill_gap + time_gap * ego_speed)`.
Units are converted explicitly between radar m/s and VehicleStatus km/h.
The original ADAS template is retained for old projects.

## Live monitoring and stress

The top-left 2D car shows motor/battery temperatures, charge, tire utilization,
brake demand, sensor/DTC/worker faults and contact alerts. These colors are based
on actual current model values and declared experiment thresholds. Contact alerts
are latched in the run's event history until restart; they are not damage estimates.

Vehicle Physics exposes grip, mass, drag, wind/slope, steering-rate and brake
measurements. Physical State Injection can set actual motor/battery temperature
and charge at a tick boundary; this is distinct from injecting an incorrect sensor
reading. Native validation is atomic. All live signals provides plant ground
truth, acknowledged controls, sampled sensor validity/age, every ECU variable and
modeled hardware/resource status. CAN and Performance retain their dedicated tabs.

## Projects

The project picker contains built-in templates and the deployed projects visited
in the current session. Load / switch preserves the old deployed configuration in
its session slot, then restarts the selected project at tick zero with fresh
workers. Unsaved ECU/topology drafts require an explicit discard decision.

Use Save project to keep a `.vcanproj` file across application sessions; Open project
adds it to the current session. Session slots themselves are in-memory. Imported
projects refresh all views before the switch finishes. The interactive template
opts into hashed `initial_controls` for controller/environment settings, so
following gains, cruise cap and road conditions survive switching/save/reload.
Driver pedals/steering/gear are not restored as active driving commands. Legacy
projects retain their previous runtime-control semantics. Live car pose, actor
pose evolution, stress history and clock are reset on project load/restart.

## Car models

The default is Kenney's CC0 sedan with texture colors and separate wheel parts.
The camera orbits by dragging and zooms with the wheel. Front wheels reflect
applied EPS angle, wheels rotate with travelled distance, and pitch/roll cues
reflect acceleration. These body-pose cues are visualization, not suspension
state or crash deformation.

Import car shape accepts STL (including binary), OBJ, glTF and GLB using Assimp.
Choose Y-up for conventional OBJ/glTF or Z-up for CAD/STL. The longest horizontal
axis is mapped forward; named front-wheel groups can establish its direction.
Turn shape 180° corrects unlabeled models facing backwards. Import fits the mesh
uniformly to a reference length of 4.5 m. Geometry is limited to 20,000 faces and
32 MiB per source file. Invalid/oversize files report an error and retain the
previous car. Mesh geometry and sampled face colors are baked into optional
project presentation metadata, so reopening the saved project does not require
its original source file or textures. This metadata does not change the semantic
simulation hash. Snapshot telemetry omits the triangle payload for responsiveness.

Custom shapes are visual geometry. Physical mass/wheel parameters remain explicit
under Vehicle Physics, and contact tests use oriented rectangular footprints
(ego 4.5 m × 1.8 m; actors have explicit dimensions). No rigid-body crash response,
soft-body deformation, raw perception or full ADS is claimed. Mesh textures are
sampled to face colors, not a photorealistic material pipeline.

## Dependencies and evaluated options

- [Assimp](https://github.com/assimp/assimp) was selected for native import; its
  [official format list](https://github.com/assimp/assimp/blob/master/doc/Fileformats.md)
  covers STL, OBJ and glTF. This host uses Ubuntu Assimp 6.0.4.
- [Kenney Car Kit](https://kenney.nl/assets/car-kit) supplies the default sedan,
  with CC0 notice and provenance in resources/models/default-car.
- [Qt Quick 3D RuntimeLoader](https://doc.qt.io/qt-6/qml-qtquick3d-assetutils-runtimeloader.html)
  supports OBJ/glTF/GLB, while STL needs another import route. Quick 3D was not
  installed on this host. The selected Qt Widgets triangle renderer works on the
  local display and in offscreen tests, with depth sorting and directional shading.
- [esmini](https://esmini.github.io/monolith.html) offers OpenSCENARIO/OpenDRIVE
  context and external ego control. [Jolt](https://github.com/jrouwe/JoltPhysics)
  offers rigid-body vehicles. Those remain future adapters with an explicit
  authority contract; neither was added as a second plant beside the current one.

No external app/plugin is needed to operate the native laboratory. System Assimp
can be installed normally; on the current Ubuntu host, local dependency packages
can instead be fetched without system modification:

```sh
bash tools/fetch_native_mesh_deps.sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64" \
  -DVEHICLE_LAB_DESKTOP=ON -DVEHICLE_LAB_P0_TESTS=ON
cmake --build build/native -j3
ctest --test-dir build/native --output-on-failure -j3
```

Assimp/Qt runtime libraries must be available to an installed executable. The
current launcher and build work from this checkout; a self-contained installer
and portable GPU engine remain separate release work.
