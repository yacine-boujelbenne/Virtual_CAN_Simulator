# ECU timing and resource jobs (TIME-00)

The default `hardware.execution_profile` remains `tick`: existing tasks execute
inside the current barrier interval, with the existing CPU-budget error policy.
The default v1 project content/hash is unchanged; no serialized defaults are
added. `hardware.freshness_mode` now defaults to `acquisition` so a late CAN
packet cannot renew its measurement's age. Explicit `reception` reproduces the
earlier receive-tick age for comparison; it changes the persisted project hash.

To opt an ECU into deterministic cross-tick work, edit its definition in a
`.vcanproj` bundle and recompute the canonical hash with `hash_project`:

```json
{
  "hardware": {
    "execution_profile": "resource-v1",
    "freshness_mode": "acquisition",
    "cpu_mhz": 60,
    "ram_kib": 128,
    "flash_kib": 512,
    "scheduler": {
      "max_pending_jobs": 8,
      "queue_bytes": 32768,
      "job_overhead_bytes": 256,
      "deadline_us": 100000,
      "watchdog_us": 1000000
    }
  }
}
```

Each barrier release creates one ECU-cycle job: its frozen inputs, delivered
frames, observation metadata, release tick and epoch. The modeled CPU duration
is the sum of costs of tasks due at that release, scaled by `120 / cpu_mhz` and
rounded up to integer microseconds. A single non-preemptive FIFO executes these
cycle jobs. This is a functional resource model, not an instruction-set emulator,
calibrated MCU profile or task-priority/preemption scheduler.

A 15 ms job released at 0 cannot publish in the first 10 ms interval. It finishes
at 15,000 us during the second interval, using its original inputs even if the
plant has changed. The controller validates every completed job before committing
any ECU/plant state, and gives each CAN publication its own actual completion
time. Multiple completed jobs can publish different values in the same interval.
Jobs with no completion return null timing stages and no frames. Existing actuator
outputs are held; only the latest completed actuator state is applied at the
barrier. Earlier publications do not invent intermediate actuator commits.
The plant still commits once per barrier, rather than claiming sub-tick physics.

Queue admission reserves the compact frozen JSON byte count plus configured
bookkeeping. Queue capacity includes the running job. The pool cannot exceed
declared RAM minus the existing static-state estimate; static flash limits also
remain enforced. A full queue or insufficient RAM rejects the incoming job with
an explicit event. Reservations are released once on completion or cancellation.
The interval's CPU utilization counts actual modeled busy time, at most 100%;
the full task duration and queue delay remain distinct in job timing. Resource
actuation chains begin at the original input acquisition when known. Unknown
acquisition stages are omitted and excluded from E2E percentiles, rather than
inventing a fresh/zero-age sample. Compatibility `tick` metrics retain their
existing release-to-boundary interpretation.

Deadlines are absolute from release. An expired queued/running job is cancelled
without publishing or inventing a computation timestamp. Watchdog time is counted
from CPU start: a trip cancels the running and queued work, holds prior outputs,
and rejects new work until explicit reset. Reset clears payloads, reservations,
cached replies and watchdog state; old-epoch calls/completions are rejected.
A kernel reset retains bounded cancellation evidence with the original epoch;
worker reconfiguration constructs a new kernel and starts a new event window.
A failed expression
rolls back kernel values/state/DTC/input provenance before recording job failure.

`hardware.resources` reports the bounded pending queue, live reserved bytes,
interval CPU use, outcome counters and the latest 128 job events. Events follow
`resources/schemas/ecu-job-event.v1.json`. This window is observability, not a
durable full-run recorder; persistence remains LOG-00. Profile fields are
described by `resources/schemas/ecu-timing-profile.v1.json`.

Sensor acquisition metadata accompanies frozen inputs. CAN frames retain an
`acquisition` object through scheduling and gateway forwarding. Outbound
provenance conservatively uses the oldest declared input observation; a missing
input acquisition makes provenance unknown. At the consuming ECU, declared CAN
freshness uses observation/start time minus acquisition time, never receive time.
Missing timestamps produce an explicit large stale age. Future timestamps and
wrong epochs are rejected before mutating the kernel. Age and signal validity
are separate: composite messages retain their per-signal/object validity rules;
an invalid second radar slot does not erase a known first-slot acquisition time.

Retries of an already accepted tick must contain exactly the same logical
request. Identical retries return the cached result without enqueueing/running
anything twice; a changed input/frame/metadata request is a conflict. The core
prepare operation similarly freezes a pending barrier until completion/reset.

Verification: enable `VEHICLE_LAB_P0_TESTS=ON` in CMake. The existing strict
`test_envelopes` gate includes real controller/worker regression checks for
delayed traffic, cross-tick jobs, queue/RAM/deadline/watchdog outcomes, burst
publications, reset fencing, conflicting retries and unchanged v1 project hash.
Host wall time, HTTP latency, UI rendering and LLM calls never populate these
simulation timestamps. Read-only diagnostics remain available while a resource
job is pending; clear-DTC returns UDS busy/repeat-request (0x21) until the queue
is idle, so diagnostics cannot mutate DTC memory inside a non-preemptive job.
