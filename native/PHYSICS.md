# Vehicle controls and grip-aware physics

In Simulation, scroll the left controls to **Environment & Assistance** and
**Vehicle Physics**. Live environment settings apply without restarting. Select
**Grip-aware bicycle v2** and press **Apply physics & restart** to enable the new
model. Vehicle parameters are saved in the project's hashed plant configuration;
live controls, including slope/wind, are recorded as runtime inputs in telemetry.
The interactive template also persists controller/environment settings in hashed
`initial_controls`; the older physics/lateral templates reset them on restart.
Driver pedals/steering/gear reset on load in all templates. Save the project after
deploying a vehicle configuration.

Start directly with the new profile:

```sh
bash tools/run_native_desktop.sh --scenario resources/scenarios/ev_physics.json --workspace simulation
```

## Controls

- Existing gear, throttle, brake and steering controls remain active.
- Enable **Keyboard W/S/A/D + Space** for simulation-window keyboard control:
  W raises throttle and releases brake; S cuts throttle and raises brake; A/D
  adjust steering; Space cuts throttle and applies full brake. These are stepped
  commands in 10% increments, not a spring-return game controller.
- **Tire grip μ** controls available traction/braking/cornering force.
- **Slope** uses degrees; positive is uphill. **Wind** uses m/s; positive is
  headwind and negative is tailwind. These fields require the v2 profile.
- Ambient temperature, lead-vehicle speed, ignition, adaptive cruise, cruise
  target and automatic emergency braking use the existing native ECU paths.
  Press **Apply live environment** to commit the displayed values.
- Mass, wheel radius, wheelbase, drag area, rolling coefficient, steering rate
  and limit, brake capacity, final-drive ratio and motor torque clamp are
  configuration changes. Applying them restarts the current experiment.
  Legacy bicycle remains selectable; irrelevant physics controls are disabled.

## Implemented model

SI units are used internally, with the existing fixed simulation tick. This is
an educational planar model with idealized tire force saturation. It retains the
existing ECU torque, brake, EPS and transmission ownership and battery/thermal
models. It does not introduce another plant or clock.

Longitudinal drive force is requested motor torque × fixed reduction / wheel
radius. The net drive-minus-brake tire force is limited to ±μN, where
`N = m g cos(slope)`. Gravity along the road is `m g sin(slope)`, and moving
rolling resistance is `Crr N`. Aerodynamic force is
`0.5 rho CdA v_relative |v_relative|`, with `rho = 1.225 kg/m³` and signed
relative air speed equal to vehicle speed plus headwind.

The aerodynamic formula follows the [NASA Glenn drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/drag-equation/).
The use of relative velocity is described in NASA's [drag factors](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/factors-that-affect-drag/).

Acceleration follows the net force divided by mass. Speed remains forward-only,
bounded to the existing 0..80 m/s range. Distance uses the trapezoidal speed
integral, with stopping-time correction when speed would cross zero within a
tick. This prevents negative speed and inflated final-step brake work.

Applied road-wheel angle approaches the EPS request at a bounded rate. Requested
yaw rate is `v tan(steer) / wheelbase`, limited by the existing yaw bound and
the remaining lateral tire acceleration after longitudinal force use:
`ay_available = sqrt(max(0, (μN)^2 - Fx^2)) / m`.
This gives longitudinal force priority during simultaneous braking/cornering.
It is a friction-budget approximation, not a calibrated slip-angle tire model.
The bicycle-model family is illustrated in [MathWorks' vehicle dynamics example](https://www.mathworks.com/help/ident/ug/modeling-a-vehicle-dynamics-system.html).

The panel displays acceleration, yaw rate, lateral acceleration, tire utilization,
net drive/brake forces, drag, slope force, kinetic energy and cumulative net-tire
braking work. Requested and applied steering are separate world-state fields.
The net force measurements exclude simultaneous motor/brake internal losses;
brake work is not an OEM brake-temperature or regeneration estimate.

## Boundaries

There is no suspension, wheel rotational inertia, roll/pitch, load transfer,
reverse gear, calibrated tire slip model, road mesh contact or 3D crash physics.
Existing ABS wheel-speed and thermal/battery models remain educational. Park
retains its declared interlock behavior. Grade affects forces on a flat displayed
ground plane; the existing longitudinal radar scene is not a geometric 2D/3D
perception simulation. The parameters are assumptions for experiments, not
hardware calibration or vehicle safety certification.

Regression checks compare coast forces with independent expected equations,
steering rate and combined tire-force bounds, braking energy, stopping distance,
mass effects, invalid control rejection, and real Qt controls reaching native
state. The legacy P0 and lateral profiles retain their prior behavior.

Previous physics iteration: **77/77 CTest entries passed**, including a Qt test applying
1800 kg, 6° slope, 15 m/s headwind and disabling AEB through visible controls.
An additional native-process comparison used the same hashed 1500 kg project,
20 m/s initial speed, level road, full braking and AEB disabled. Grip coefficients
0.9, 0.4 and 0.1 produced stopping distances of **24.46 m**, **50.78 m** and
**173.16 m**, respectively. These are model outputs, not measured vehicle data.
The disposable sampled trajectories were removed during repository cleanup;
current compact evidence is in `docs/verification/native.json`. Stop times in those samples have 0.2 s
resolution. Different live grip controls were recorded alongside the common
project hash; the live grip changes are not part of its configuration hash.
