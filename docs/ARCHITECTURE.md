# Distributed ECU simulation architecture

## Containers and responsibilities

| Container/service | Responsibility |
|---|---|
| simulator | Dashboard/API, central C++ vehicle/world model, sampled sensors, clock barrier, bus broker, gateway and flow-policy control |
| ecu-motor | One C++ motor-control kernel |
| ecu-abs | One C++ braking/ABS kernel |
| ecu-bms | One C++ battery-management kernel |
| ecu-dynamics | One C++ measured vehicle-status kernel |
| ecu-cluster | One C++ instrument-cluster kernel; displayed values come from decoded CAN |
| ecu-radar | One C++ synthetic radar-interface kernel; 32-byte CAN FD transmission of a four-object list |
| ecu-adas | One C++ FCW/AEB/ACC kernel; consumes the radar object list and vehicle-speed frames |
| ecu-reserve | One initially unconfigured kernel process, available to an imported ECU definition |

The eight ECU services reuse one small image, with one behavior instance per container. Sharing an image avoids duplicating identical runtime dependencies; definitions give each ECU its function. Each configured container has exactly one C++ ECU kernel and a small Python transport adapter. The controller holds mirrored status for display/validation, but does not execute the remote ECU tasks in distributed mode.

Containers share the host operating-system kernel. A virtual ECU's CPU clock, RAM, flash, task budget, inputs and CAN controller are functional simulation parameters. This is not a Docker-based MCU instruction-set emulator.

## Coordinated clock

Each simulation step is exactly 10 ms:

1. The central world samples sensors and releases bus frames whose simulated arrival time is due.
2. It creates one immutable tick/epoch input for each ECU worker.
3. Workers execute their own tasks, state transitions, diagnostic logic and signal encoding concurrently.
4. The main controller waits for every required worker. It validates their tick numbers and declared frame outputs.
5. Only a complete barrier commits ECU outputs, queues CAN messages, integrates the physical world and advances time.

A failed worker leaves simulation time unchanged. Completed workers cache the last tick result, so retrying a partially completed barrier does not execute their tasks twice. Missing/out-of-order ticks are rejected. Reset increments the epoch and reconfigures the workers. A stopped/restarted container needs a scenario reset to recover its lost state.

Polling the browser never advances simulation time. Wall-clock playback can be slower than simulated time if networking or execution is slow; this is deterministic functional SIL, not a hard real-time scheduler.

## Data plane and control plane

Two modeled segments are configured:

- **powertrain:** classic CAN, 500 kbit/s.
- **adas:** CAN FD capable, 500 kbit/s nominal, 2 Mbit/s data bitrate.

The Docker bridge carries HTTP coordination. It does not stand in for CAN electrical signaling. The main C++ broker carries encoded CAN frames between logical bus members. Receivers decode only declared interfaces on their segment.

Gateway routes:
- 0x120 VehicleStatus: powertrain → adas.
- 0x450 ADASControl: adas → powertrain.

FD frames are not forwarded unchanged onto a classic-only bus. The broker sorts pending requests by their CAN arbitration field, including the standard/extended-ID relationship. Serialization estimates distinguish nominal/data bitrates, frame length and BRS. A conservative overhead allowance replaces a bit-exact stuffing/CRC model.

Policies match transmitter, segment and optional CAN ID. They can drop frames or add simulated delivery latency. The dashboard shows applied rules, dropped frames and per-bus backlog/load. This is an SDN-style programmable simulation, not an OpenFlow implementation.

## ECU and virtual hardware schema

An ECU definition contains identity, inputs, initialized variables, tasks, bindings, state-machine transitions, CAN interfaces, DTC rules, assumptions and a hardware profile.

Hardware fields:
- cpu_mhz: virtual clock.
- ram_kib / flash_kib: modeled capacities.
- task_budget_us: maximum modeled task cost per step.
- Each task may declare cost_us at a 120 MHz reference clock; cost scales with the virtual clock.
- CPU load and estimated RAM/flash use are reported by the kernel.
- ADC channels apply declared resolution and range quantization to analog inputs.
- GPIO output variables expose fan/contactor states; the actuator bindings connect them to the plant.
- The virtual CAN controller has an FD capability flag and bounded RX FIFO; overflow trips its modeled watchdog.
- Container CPU/RAM limits are independently enforced by Compose.

Tasks follow list order. Assignments within a task read a shared prior-value snapshot, so a variable assigned by one task is visible to the next task of the same tick, never to another assignment of its own task. Pipelines that feed each other (perception fusion → validity → actuation) are therefore split into consecutive tasks. At most one state transition occurs per tick. States do not implicitly disable tasks; definitions explicitly gate outputs. Expressions are restricted numbers/booleans/signal references or op/args trees. No generated source code is compiled or executed.

Signal packing supports 1–32-bit signed/unsigned Intel integers at any valid bit position in an 8-byte classic or up-to-64-byte FD payload. Range, overlap and representability checks reject inconsistent definitions. Encoding saturates to declared physical ranges.

Optional freshness_inputs map a declared ECU input to an RX message's age. The ADAS kernel marks perception unavailable when radar traffic becomes stale. All values are sampled/decoded; no hidden central ADAS decision replaces the worker logic.

## ADAS closed loop

Synthetic world measurements → Radar ECU → CAN FD RadarObjectList → ADAS ECU → classic ADASControl → gateway → Motor/ABS ECU → physical vehicle → new measurements.

The radar object list (0x400, CAN FD, 32 bytes) carries a three-signal header (`object_count`, `sample_counter`, `radar_valid`) and four 56-bit object slots. Each slot holds range, closing speed, confidence and a validity flag. Slot 0 tracks the lead vehicle, slot 1 a static obstacle of the plant, slots 2 and 3 stay empty and invalid. The transmitter and the receiver declare the identical layout, so a frame decodes to the same values on both sides.

Perception fusion cross-checks the radar range of slot 0 against an independent range measurement of the same scene. A gap above 5 m raises `fusion_disagree`, which clears `perception_valid`: the forward-collision warning stays conservative and visible, while emergency braking is inhibited because the two perception paths disagree. Freshness is judged by the age of the last received radar frame; a silent radar bus marks the ADAS input unavailable.

FCW uses a time-to-collision threshold. AEB requests braking and inhibits torque. Basic ACC controls throttle toward a target speed while observing a distance threshold; it is not a validated adaptive-cruise algorithm. Radar observations are abstract range/closing-speed/confidence signals, not raw sensor data.

The longitudinal plant includes speed, position, torque, braking/friction, wheel slip, RPM, energy/SOC and approximate temperature/fan feedback. No steering, lane-control physics, full perception or route planning is claimed.

## Diagnostics between containers

The main tester and selected ECU use independent event-driven ISO-TP endpoints. Every request/flow-control/response frame crosses the container HTTP boundary and is recorded on the diagnostic virtual bus. UDS executes against the target C++ kernel's own DTC memory.

Supported demonstration services: 0x10, 0x14, 0x19/02, 0x22 and 0x3E. The demonstration VIN exercises classic multi-frame transport; a CAN FD link can carry that same response in an escaped single frame. Normal physical addressing uses 0x7E0 request / 0x7E8 response. Session responses acknowledge modes but do not implement authorization/security gating.

DTCs latch stored status on trigger, clear active status on recovery, and remain stored until cleared. Persistent faults relatch on the next kernel tick.

## Extension and persistence

One reserve container can be assigned to a JSON-imported ECU without exposing the Docker daemon. Adding more concurrent ECUs requires declaring additional worker services/endpoints. The application never silently hosts extra ECU tasks in the main container.

Imported definitions are in memory. Download JSON, add it to the scenario and supply its dedicated worker mapping for persistent startup. Reset releases imported reserve assignments.

Retention: 200 CAN frames and 80 state transitions. CSV captures retained frames; full trace recording/replay and persistent projects remain future work.
