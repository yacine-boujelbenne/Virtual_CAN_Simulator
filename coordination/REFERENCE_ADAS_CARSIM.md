# Supplemental review: TI ADAS communications and official CarSim

Reviewed 24 October 2026. This is a reference clarification, not a change to the accepted native-C++ architecture, shared law, provider assignments or 22-task queue. No simulator or communication implementation is added by this note.

## Decision

Keep the existing route: preserve P0, reconcile its source, move the primary application/control plane into C++, add resource-correct timing and durable logs, then native UI/3D and trajectory-control experiments. Retain esmini as the first backend target. CarSim remains an optional licensed backend; the new official information makes the direct C/C++ integration path more concrete.

## What the TI document adds

The supplied Texas Instruments paper, *Communication Protocols in Modern ADAS Architectures*, SLYY219A, March 2024, describes how CAN, Ethernet, FPD-Link and PCIe coexist. Pages 3-6 distinguish sensor/video movement from ECU control traffic and processing interconnects. Use it as architectural background, not as a protocol conformance specification or a source of universal latency/rate limits.

| Link family | How it fits our simulation | Scope now |
| --- | --- | --- |
| CAN / CAN FD | ECU control/status, processed object messages and diagnostics | Keep the existing broker and C++ ISO-TP work |
| Automotive Ethernet | A later data path for richer sensor/perception output and domain-controller traffic | Leave a typed link/transport extension point; do not call Docker HTTP traffic an automotive-Ethernet model |
| FPD-Link / SerDes | Dedicated camera-to-processing acquisition path | Future sensor-pipeline delay/bandwidth/validity model; no proprietary electrical/encoding emulation claim |
| PCIe | Processing-node CPU/accelerator transfers and buffering | Future internal compute-resource profile; an endpoint is not automatically another ECU container |

One ECU still maps to one worker/container. Its internal peripherals and accelerator links are sub-resources. A virtual car can use several link types without turning every sensor or hardware endpoint into a separate ECU.

For future link profiles, record effective payload bandwidth, wire overhead, queue capacity/discipline, processing/serialization/propagation delays, seeded jitter and loss policy. Preserve sample -> computed -> received -> observed -> applied timestamps and distinguish those intervals from host-network overhead. Reuse the existing event/recorder contracts and avoid counting one delay twice. The current protocol task queue is not expanded by this review.

Keep processed radar/object/control messages on the existing CAN layouts. Do not put a full camera stream into a CAN-FD message or use a rendered frame as an instant sensor input. A future raw-image path needs a separate transport and camera/inference timing model. As an illustrative payload calculation, 1920 x 1080 x 24 bits x 30 fps is 1,492,992,000 bits/s before framing overhead; four such uncompressed streams are about 5.97 Gbit/s. Whether compression is needed depends on the selected link, traffic and image format, not a blanket rule that all Ethernet video must be compressed.

The paper's CAN variants/data rates are examples in a 2024 vendor overview. Retain the project's own declared link rates and supported features; verify standards and selected transceiver/controller capabilities before adding CAN XL, SIC or other variants. Those remain deferred as already planned. Electrical ringing, retiming and signal integrity are outside the current software model.

## What official CarSim information changes

The official product page describes standalone simulation and custom C/C++ programming through the VehicleSim SDK. Therefore the MATLAB/Simulink dependency of the reference demonstration is not a mandatory application architecture for us. [C1,C2]

A future `CarSimBackend` can implement the same native world/plant contract using the VS Solver API or the VS Vehicle wrapper. The SDK describes variable exchange, solver access, co-simulation communication and output-file support. Use steering/brake/throttle requests from the ECU path as model inputs; return pose, velocities and other required physical measurements through the existing sensor-model boundary. Select exact variables, units, coordinate conversion, stepping and callback behavior against the installed SDK documentation. A valid VehicleSim product licence is required. [C2]

FMI/FMU is another supported co-simulation route. Prefer one deliberately selected integration path for a backend; do not introduce both FMI and SDK layers without a concrete need. Platform, exported binary compatibility, model data and licence capabilities must be checked before promising a Linux/container deployment. No CarSim binary or licence was available for testing in this review. [C3]

In a physics-authoritative CarSim profile, CarSim replaces the internal ego dynamics. Keep one application time coordinator and do not integrate the same ego vehicle twice. Disable conflicting built-in driver/control channels for those owned by our ECU/controller path. Rendering and UI continue to consume snapshots without advancing the solver.

Official pages also describe moving objects, occlusion-aware ADAS sensing and a VehicleSim Dynamics plugin for Unreal. These are useful future sensor/world capabilities; the site states that ADAS sensors need an extended licence. Unreal coupling can exchange terrain/friction with the vehicle model and produce outputs for post-processing. None of this is assumed to emulate our ECU firmware, CAN/SerDes wiring or compute capacity: those remain our separately tested models. [C4,C5]

Keep the native RunRecorder even if a backend supplies its own result files. Correlate vendor outputs with project hash, backend/version, time, sensor acquisition and ECU/CAN/actuation events. Add licence/platform capability detection and a controlled unavailable-backend result; preserve the existing headless backend so ordinary CI does not depend on commercial software.

## Suggested later capability spike

When an appropriate CarSim licence/SDK is available, evaluate one small scenario: initialize, map the required input/output variables, step at a declared fixed communication interval, exchange ECU-generated control requests, synchronize reset, and export a correlated run. Verify coordinate/units conversion, one solver advance, actuator authority and acquisition age. Benchmark the same maneuver against the internal plant, but do not require identical dynamics across different vehicle models. Record model parameters and observed differences.

This is a future backend evaluation, not a prerequisite for the current P0/source gates or a newly enabled automatic provider task.

## Sources

- TI: user-supplied SLYY219A PDF, especially pages 3-6; official copy: https://www.ti.com/lit/wp/slyy219a/slyy219a.pdf . The supplied publication is referenced, not redistributed in this repository.
- C1 CarSim overview: https://www.carsim.com/products/carsim/
- C2 VehicleSim SDK: https://www.carsim.com/products/supporting/vehiclesim/vs_api.php
- C3 FMI and other simulation environments: https://www.carsim.com/products/supporting/simulink/index.php
- C4 ADAS objects and sensors: https://www.carsim.com/products/supporting/objects_sensors/index.php
- C5 Unreal integration: https://www.carsim.com/products/supporting/unreal/index.php
