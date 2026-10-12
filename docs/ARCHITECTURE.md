# Runtime and ECU contract

## One connected loop

The physical plant stores real vehicle values. Sensors sample those values and apply faults/noise. ECU tasks read their declared inputs and prior CAN receive values, update internal variables, change state and diagnostic status, and issue actuator commands. Actuators affect the next physical integration step. The cluster gets its displayed speed/RPM/SOC/warnings from decoded CAN receive signals.

The five roles are:
- Motor: throttle to torque, temperature monitoring, fan command; receives BMS power permission.
- ABS: four wheel measurements and speed estimate to slip, active state and brake pressure.
- BMS: SOC/current/voltage/temperature monitoring and power-contactor permission.
- Dynamics: measured vehicle speed/RPM to CAN.
- Cluster: receives those four ECUs' messages and updates displayed values.

## Time and delivery

A tick is exactly 10 ms. The JSON scenario can use 1–100 ms; task, sensor and TX periods must be integer multiples of that step. Periodic tasks fire at time zero. Tasks follow their listed order; assignments within a task read the same prior variable snapshot. At most one state transition is applied per tick. State names describe behavior; task execution is not automatically disabled by OFF/FAULT. Controller definitions explicitly inhibit torque/power when required.

Per tick:
1. Sample sensors due at this time.
2. For each ECU, update physical/control inputs and decode queued CAN frames.
3. Execute due tasks, state transitions, DTC trigger/recovery and output bindings.
4. Encode due TX messages and sort numeric IDs for deterministic priority ordering.
5. Broadcast frames. Receiving ECUs consume them in the next tick.
6. Integrate vehicle equations and increment the clock.

Internal endpoints share a channel; every endpoint gets its own bounded receive queue. They do not see their own frames by default. Each simulation instance has an isolated channel. In-memory queuing is broadcast delivery, not a CAN electrical/error-state emulation.

## Physical units and simplifications

| Quantity | Internal unit |
|---|---|
| Vehicle/wheel tangential speed | m/s |
| Cluster speed / VehicleStatus signal | km/h |
| Acceleration | m/s² |
| Position | m |
| Torque | Nm |
| RPM | rev/min |
| Temperature | °C |
| Voltage/current | V/A |
| SOC | percent 0–100 |
| Throttle/brake/pressure/friction | ratio 0–1 |

The demonstrator uses 1500 kg, a 0.32 m wheel radius, a fixed 9:1 driveline and a 60 kWh battery. Traction is torque × 9 / radius. Braking is limited by road friction × mass × gravity. Aerodynamic/rolling resistance opposes forward motion. Speed integrates acceleration. Wheel slip is a deliberately simplified algebraic relationship between brake pressure and road friction. ABS applies/reduces pressure using a threshold; it is not a validated per-wheel hydraulic model.

Motor power drives battery SOC decrease and approximate thermal heating; fan output strengthens motor cooling. No regeneration or gear shifting is included. The dynamics/control/fault thresholds are educational assumptions, not OEM specifications.

Physical signals saturate at configured encoding ranges when transmitted. Measured normal sensors are clamped to their declared range; disconnected/stuck fault readings can exceed it to test fault detection. Sensors with noise use a fixed seed of 42. The default scenario has zero normal noise so baseline replay is deterministic.

## ECU JSON

Every definition contains:
- `schema_version: 1` and a unique `name`.
- `inputs`: declared controls/sensors or received CAN signals.
- `variables`: initialized numeric state.
- `tasks`: name, period_ms and simultaneous `assign` map.
- `bindings`: actuator-name to variable-name map; only one ECU can own an actuator.
- `state_machine`: initial state, known states, ordered from/to/when transitions.
- `messages`: TX/RX ID, DLC and signal layouts; TX additionally has period_ms.
- `dtcs`: display code, 24-bit demonstration UDS wire code, name, severity, trigger and recovery expressions.
- `assumptions`: human-readable model notes.

A signal has `name, start, length, factor, offset, min, max, unit, signed, byte_order`. Length is 1–32 bits, classic CAN DLC is 1–8 bytes. This V1 supports only little-endian integer layouts. Raw values are rounded, physical output values are saturated, and encoding capacity is checked before loading. RX/TX layouts on the same ID must match exactly.

Expressions are numbers, booleans, declared signal names, or objects:
```json
{"op":"if","args":[{"op":"gt","args":["temperature_sensor",90]},1,0]}
```

Allowed operations: add, sub, mul, div, gt, lt, ge, le, eq, and, or, min, max, if. Binary operators take two args; if takes three. Expressions are bounded to 24 nesting levels. Names are resolved explicitly. There is no arbitrary code, eval or subprocess execution in an ECU definition. Zero division and non-finite results fail with an error.

[ecu.schema.json](../resources/ecu.schema.json) describes the file shape. C++ validation is authoritative for cross-field constraints: signal capacity/overlap, references, timing, state membership, and network collision/dependency checks.

## DTC lifecycle and demonstration UDS

A trigger makes a DTC active and stored. Recovery clears active status, preserving stored status. ClearDiagnosticInformation clears fault memory; an underlying persistent fault relatches at the next tick.

The UDS tester performs actual single/multi-frame CAN-TP segmentation, flow control, sequence handling and reassembly between concurrent endpoints on an isolated internal bus. The selected ECU supplies its DTC memory.

| Request | Response behavior |
|---|---|
| 10 01 / 02 / 03 | 50 session + demonstration P2 timing acknowledgment |
| 3E 00 | 7E 00 |
| 22 F1 90 | 62 F1 90 + 17-byte simulated VIN |
| 22 F2 00 | 62 F2 00 + speed in unsigned 0.01 km/h |
| 19 02 mask | 59 02 09 + stored DTC records; status bit0 active, bit3 stored |
| 14 FF FF FF | 54; clear selected ECU DTC memory |
| Unsupported SID | 7F SID 11 |
| Unsupported subfunction | 7F SID 12 |
| Wrong length | 7F SID 13 |
| Unsupported DID/group | 7F SID 31 |

Unsupported services include security access, reset, routine control, write-DID and programming. The selected-target bus uses 0x7E0 request / 0x7E8 response. No session/authentication claims are implied.

## Storage and packaging

The application has one C++ subprocess per HTTP server. A Python lock serializes commands; multiple browsers inspect the same simulation. Clock advancement belongs to the server thread. Browser clients cannot invoke its advance command directly. Snapshots keep 200 CAN frames and 80 transitions. CSV export captures those retained frames; full logging/replay is not implemented.

Container assets are read-only, and the default runs as UID/GID 10001 with no Linux capabilities. Imported models are reviewed and validated in memory. Download a definition and add its path to a scenario to reuse it on the next start.

## Build migration

The original CMake glob accidentally included an interactive main inside the library, omitted a runnable application target and required a missing `googletest/` directory. Explicit targets now separate CAN, vehicle runtime, application and tests. The original incomplete SimulationManager header is replaced with the implemented runtime. Existing Sender/Receiver interfaces remain available; receive now respects availability, and rename updates the identity.

The old host-provisioning shell calls are removed from CanBus. SocketCAN connects only to an existing interface and uses checked frame sizes/IDs. Tests now use two endpoints and concurrent transport peers rather than sending and receiving a multi-frame transfer on one endpoint.
