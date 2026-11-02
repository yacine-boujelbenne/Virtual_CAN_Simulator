# Native headless runtime (Linux)

The working Qt desktop, steering/P–N–D scenario and launch instructions are now
available in [DESKTOP.md](DESKTOP.md). Both clients use the same C++ controller.

Run from the repository root so the existing relative scenario/template paths resolve:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j2
./build/native/vclab --steps 100 --save-project /tmp/lab.vcanproj
./build/native/vclab --project /tmp/lab.vcanproj --steps 100
```

GoogleTest is required for the default test build. `-DBUILD_TESTING=OFF` builds
the runnable products without GoogleTest. To include the Python regression/test
drivers, install Python/jsonschema and configure `-DVEHICLE_LAB_P0_TESTS=ON`.
Neither Python nor a browser is needed by `vclab` or its workers at runtime.

Without `--steps`, the CLI reads one JSON command per line and writes one JSON
result per line. The existing `controls`, `step`, `run`, `pause`, `advance`,
`snapshot`, `reset`, `export_project`, `import_project`, `hash_project`,
`diagnostic`, `fault`, `node_fault`, `network_policy`, `validate`, and `add_ecu`
commands are available. With `--realtime`, the C++ controller paces continuous
execution after `run`; clients only send commands/read snapshots. Without that
flag, the caller explicitly steps/advances. Batch `--steps` advances fixed ticks
without wall-clock pacing. There is one native clock owner.

```sh
printf '%s\n' '{"action":"controls","values":{"throttle":0.6}}' \
  '{"action":"step","count":100}' \
  '{"action":"diagnostic","ecu":"Motor ECU","bytes":[34,241,144],"fd":true}' \
  | ./build/native/vclab
```

The default `native-process` mode starts one persistent `vclab_worker` child for
each declared ECU. The worker owns its kernel and cached tick responses; the
controller owns the sole plant, clock, broker and prepare/complete barrier.
There is one on-demand reserve slot. Adding an ECU creates a dedicated process;
reset releases the assignment. EOF shuts down/reaps the owned workers. Worker
death or timeout never falls back to an embedded ECU. A failed partial tick
pauses the run; retry reuses the original prepared inputs and completed worker
results. Logical halt/restore can resume that tick. A dead local process requires
reset, which creates fresh workers and advances the epoch.

For HTTP workers, launch one process/container per ECU:

```sh
./build/native/vclab_worker --http 8081 127.0.0.1
```

The HTTP entrypoint prints a JSON readiness record with its actual port/PID.
Port `0` selects a free port. The optional bind address defaults to loopback;
use `0.0.0.0` explicitly inside a container. `GET /health` reports readiness;
`POST /command` accepts the same worker commands as the transitional Python
adapter. Set `CLUSTER_TOKEN` identically on the controller and workers, or use
the existing local-development default. Native HTTP uses bounded JSON bodies,
Content-Length and one response per connection. It supports HTTP over IPv4/DNS,
not TLS, IPv6 or chunked encoding. Socket I/O has a deadline; DNS resolution uses
the host resolver. This transport targets local/container laboratory networks.

Supply `--endpoints path/to/workers.json` to the controller:

```json
{
  "workers": {
    "Motor ECU": "http://127.0.0.1:8081",
    "ABS ECU": "http://127.0.0.1:8082",
    "BMS ECU": "http://127.0.0.1:8083",
    "Dynamics ECU": "http://127.0.0.1:8084",
    "Instrument Cluster": "http://127.0.0.1:8085",
    "Radar ECU": "http://127.0.0.1:8086",
    "ADAS ECU": "http://127.0.0.1:8087"
  },
  "spares": ["http://127.0.0.1:8088"]
}
```

Mappings must be complete and endpoints distinct; an empty explicit mapping is
an error. HTTP also interoperates with the old Python worker adapter for
comparison. No Python worker is launched by the native controller.

Projects retain v1 canonical hashes and core validation. Invalid projects are
validated in an isolated core before any live worker is reconfigured. Local
imports stage fresh workers before committing. Remote configuration is not a
distributed transaction: if a worker becomes unavailable during a valid import,
the controller retains the old project/time, pauses, and requires reset because
some remote workers may already have changed epoch. This is reported through
`cluster.requires_reset` and `last_error`. It never reports a successful partial
import. An invalid hash or missing endpoint leaves the active laboratory intact.

Diagnostics exchange actual C++ ISO-TP frames with the selected worker on CAN
or CAN FD. Tester/worker session state is cleaned up on success and failure.
The transport does not redefine simulation timestamps or diagnostic time.

Native programming commands `validate_ecu_update` and `update_ecu` take
`name` and `definition`. They validate the full candidate project in isolation
and regenerate its derived CAN map/hash in C++. Updates keep the ECU name and
require a paused, unblocked experiment; a successful update stages workers and
restarts the project. `restart_project` restarts the current exported project,
including deployed programs and policies. The legacy `reset` command retains
its original startup-scenario behavior for compatibility.

The existing browser/Compose launchers remain available. Docker builds now copy
the native sources and offer an optional `native-ecu` target; container execution
still needs validation on a Docker-capable host. The original desktop facade
fixture is retained alongside the live Qt client. The native scheduler models
cross-tick jobs and acquisition age; `ev_lateral.json` adds steering and P/N/D.
The desktop offers sampled telemetry and a synthetic perspective view. External
3D world integration, full durable recording, tracking and MPC remain roadmap
tasks; the perspective view does not certify those gates.
