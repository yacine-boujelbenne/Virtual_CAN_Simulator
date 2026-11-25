# Protocol and ADAS research

Primary sources reviewed on 12 October 2026:

| Source | Implementation decision |
|---|---|
| [Linux SocketCAN documentation](https://docs.kernel.org/networking/can.html) | Classic CAN supports payloads up to 8 bytes. CAN FD extends payload capacity to 64 bytes and can use separate nominal/arbitration and data bitrates. Frame records explicitly distinguish byte length, DLC, extended IDs, FD and BRS. |
| [Linux ISO 15765-2 documentation](https://docs.kernel.org/networking/iso15765-2.html) | ISO-TP operates above CAN or CAN FD. Implement single/first/consecutive/flow-control frames, block size, STmin, sequence wrap and bounded transfers. This version uses normal physical addressing and limits PDUs to 4095 bytes. |
| [NHTSA driver assistance technologies](https://www.nhtsa.gov/vehicle-safety/driver-assistance-technologies) | AEB applies braking when a forward collision is imminent; FCW warns; ACC adjusts speed to maintain distance. Implement simplified FCW/AEB/ACC around a synthetic forward radar observation and measured vehicle speed. |
| [NHTSA automated vehicle safety](https://www.nhtsa.gov/vehicle-safety/automated-vehicle-safety) | Keep driver assistance distinct from a full automated-driving system. The demonstrator does not implement autonomous driving, lane perception, route planning or an operational design domain. |
| [KPIT visual brand identity](https://www.kpit.com/kpit-unveils-its-new-visual-brand-identity/) | Use the requested green/white visual direction without presenting this independent project as an official company product. |

These sources guide the functional model; they do not establish ISO compliance or safety validation.

## Layer separation

- **Docker bridge / HTTP:** carries coordination messages between containers.
- **Virtual CAN broker:** applies bus membership, declared gateway routes, frame priority, estimated serialization and configurable drop/latency policies.
- **CAN / CAN FD frame layer:** carries identifiers, frame flags and encoded signal bytes.
- **ISO-TP:** transports larger application payloads via CAN frames and receiver flow control.
- **UDS demonstration services:** execute in the selected ECU kernel, with that kernel's own diagnostic memory.
- **ADAS:** consumes synthetic measurements and CAN signals to make assistance decisions.

An SDN-style control plane can program delivery policies without replacing CAN's arbitration and broadcast semantics with Ethernet addressing. This project is not an OpenFlow switch.

## Implemented fidelity

| Implemented | Explicit boundary |
|---|---|
| 11/29-bit identifiers; classic/FD byte limits; legal FD wire lengths; BRS metadata | No bit-exact CRC or stuffing/error-confinement model |
| Priority ordering and per-bus estimated serialization/backlog | Synchronous 10 ms ECU barrier; not a real-time ECU scheduler |
| FD signal packing beyond bit 64 | Integer Intel/little-endian layouts; no multiplexing/Motorola parser |
| ISO-TP normal addressing, SF escape lengths on FD, FF/CF/FC, STmin and block sizes | No extended addressing, functional broadcast or PDUs above 4095 bytes |
| Synthetic radar observations, freshness checking, FCW, AEB and basic ACC throttle control | No raw camera/radar perception or production control calibration |
| Virtual CPU/RAM/flash and task-budget profiles; kernel halt/restore experiments | Functional software kernel, not instruction-set or electrical hardware emulation |

The UI emergency-braking scenario deliberately chooses its speed, initial gap and road friction. Outcomes are model dependent. The experiment is useful for tracing the distributed decision loop, not for asserting production safety performance.
