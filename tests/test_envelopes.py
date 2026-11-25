"""Envelope v2 and barrier-contract checks (resources/schemas/event-envelope.v2.json).

Replays the distributed protocol against native binaries:
  build/vehicle_sim  (controller core)
  build/ecu_kernel   (one process per ECU, as in Compose)
Skipped when the binaries have not been built yet (cmake --build build).
"""
import json
import os
import subprocess
import unittest
from pathlib import Path

import jsonschema

ROOT = Path(__file__).resolve().parent.parent
SIM = Path(os.environ.get("SIM_BINARY", ROOT / "build" / "vehicle_sim"))
KERNEL = Path(os.environ.get("ECU_BINARY", ROOT / "build" / "ecu_kernel"))
SCHEMA = json.loads((ROOT / "resources" / "schemas" / "event-envelope.v2.json").read_text())
ORDER = ["sampled_at_us", "computed_at_us", "received_at_us", "observed_at_us", "applied_at_us"]


class Line:
    def __init__(self, cmd):
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, cwd=ROOT)

    def send(self, obj):
        self.p.stdin.write(json.dumps(obj) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        assert line, "process closed its output"
        return json.loads(line)

    def close(self):
        self.p.terminate()
        self.p.wait(timeout=6)
        self.p.stdin.close()
        self.p.stdout.close()


@unittest.skipUnless(SIM.exists() and KERNEL.exists(), "build/vehicle_sim and build/ecu_kernel required")
class EnvelopeChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.validator = jsonschema.Draft202012Validator(SCHEMA)
        cls.sim = Line([str(SIM), "resources/scenarios/ev_basic.json"])
        state = cls.sim.send({"action": "snapshot"})
        cls.epoch, cls.step = state["epoch"], state["step_ms"]
        cls.kernels = {}
        for ecu in state["ecus"]:
            kernel = Line([str(KERNEL)])
            reply = kernel.send({"action": "configure", "definition": ecu["definition"],
                                 "step_ms": cls.step, "epoch": cls.epoch})
            assert "error" not in reply, reply
            cls.kernels[ecu["name"]] = kernel

    @classmethod
    def tearDownClass(cls):
        for kernel in cls.kernels.values():
            kernel.close()
        cls.sim.close()

    def tick(self, tamper=None, rupture=None):
        prepared = self.sim.send({"action": "prepare_tick"})
        results = []
        for node in prepared["nodes"]:
            result = self.kernels[node["name"]].send({**node, "action": "tick"})
            self.assertNotIn("error", result, result)
            if tamper == node["name"]:
                result.pop("timing", None)
            if rupture == node["name"]:
                result["timing"]["computed_at_us"] = 10 ** 12
            results.append(result)
        return self.sim.send({"action": "complete_tick", "results": results})

    def envelopes(self, state):
        timing = state["timing"]
        return timing["envelopes"] + timing.get("actuations", [])

    def test_barrier_requires_valid_timing_envelope(self):
        committed = self.tick()
        self.assertNotIn("error", committed, committed)
        rejected = self.tick(tamper="Motor ECU")
        self.assertIn("timing", rejected.get("error", ""), rejected)
        recovered = self.tick()
        self.assertNotIn("error", recovered, recovered)
        rejected = self.tick(rupture="Radar ECU")
        self.assertIn("window", rejected.get("error", ""), rejected)
        self.assertNotIn("error", self.tick())

    def test_every_envelope_validates_and_is_monotonic(self):
        for _ in range(5):
            self.assertNotIn("error", self.tick())
        state = self.sim.send({"action": "snapshot"})
        envelopes = self.envelopes(state)
        self.assertTrue(envelopes)
        for envelope in envelopes:
            errors = list(self.validator.iter_errors(envelope))
            self.assertFalse(errors, (envelope, errors[0].message if errors else ""))
            stamps = envelope.get("timestamps", {})
            values = [stamps[key] for key in ORDER if key in stamps]
            self.assertEqual(values, sorted(values), envelope)
            self.assertLessEqual(values[0], values[-1], envelope)

    def test_frames_never_hit_the_wire_before_computation(self):
        self.assertNotIn("error", self.tick())
        state = self.sim.send({"action": "snapshot"})
        checked = 0
        for frame in state["frames"]:
            if "seq" not in frame:
                continue
            self.assertIn("computed_at_us", frame, frame)
            if frame.get("delivered_at_us") is not None:
                checked += 1
                self.assertGreaterEqual(frame["delivered_at_us"], frame["computed_at_us"], frame)
        self.assertGreater(checked, 0, "no delivered frame to verify")

    def test_actuation_and_drop_envelopes_survive_the_acquisition_flood(self):
        for _ in range(3):
            self.assertNotIn("error", self.tick())
        self.sim.send({"action": "controls", "values": {"throttle": 0.6}})
        for _ in range(30):
            self.assertNotIn("error", self.tick())
        state = self.sim.send({"action": "snapshot"})
        actuations = state["timing"].get("actuations", [])
        self.assertTrue(actuations, "torque change produced no actuation envelope")
        for envelope in actuations:
            self.assertFalse(list(self.validator.iter_errors(envelope)), envelope)
        self.sim.send({"action": "network_policy",
                       "rules": [{"source": "Dynamics ECU", "bus": "powertrain", "drop": True}]})
        for _ in range(20):
            self.assertNotIn("error", self.tick())
        state = self.sim.send({"action": "snapshot"})
        drops = [e for e in self.envelopes(state) if e.get("quality_reason") == "dropped_frame"]
        self.assertTrue(drops, "drop policy produced no invalid envelope")
        for envelope in drops:
            self.assertFalse(list(self.validator.iter_errors(envelope)), envelope)
        self.assertTrue(any(f.get("dropped") for f in state["frames"]))




@unittest.skipUnless(SIM.exists() and KERNEL.exists(), "native binaries required")
class ResourceTimingChecks(unittest.TestCase):
    def setUp(self):
        self.sim = Line([str(SIM), "resources/scenarios/ev_basic.json"])
        self.addCleanup(self.sim.close)
        self.project = self.sim.send({"action": "export_project"})
        self.kernels = {}
        self.job_validator = jsonschema.Draft202012Validator(json.loads(
            (ROOT / "resources/schemas/ecu-job-event.v1.json").read_text()))

    def configure(self, scheduler=None, cpu=60, resource=True, burst=False, target="Radar ECU", initial_state=None, transform=None):
        project = json.loads(json.dumps(self.project))
        radar = next(d for d in project["ecu_definitions"].values() if d["name"] == target)
        if initial_state:
            project["scenario"]["initial_state"].update(initial_state)
        if resource:
            radar["hardware"]["execution_profile"] = "resource-v1"
            radar["hardware"]["cpu_mhz"] = cpu
            radar["hardware"]["scheduler"] = dict(max_pending_jobs=8, queue_bytes=32768,
                job_overhead_bytes=128, deadline_us=100000, watchdog_us=100000)
            radar["hardware"]["scheduler"].update(scheduler or {})
            if burst:
                for task in radar["tasks"]:
                    task["cost_us"] = 1000 // len(radar["tasks"])
                    task["period_ms"] = 10
                for message in radar["messages"]:
                    if message["direction"] == "tx":
                        message["period_ms"] = 10
                for message in project["can_map"]:
                    if message["ecu"] == target and message["direction"] == "tx":
                        message["period_ms"] = 10
                radar["tasks"].append(dict(name="slow_setup", period_ms=30, cost_us=14000, assign={}))
            else:
                for task in radar["tasks"]:
                    task["cost_us"] = 10
                radar["tasks"][0]["cost_us"] = 7500 - 10 * (len(radar["tasks"]) - 1)
        if transform:
            transform(project, radar)
        project["project_hash"] = self.sim.send({"action": "hash_project", "project": project})["project_hash"]
        reply = self.sim.send({"action": "import_project", "project": project})
        self.assertNotIn("error", reply, reply)
        state = self.sim.send({"action": "snapshot"})
        for ecu in state["ecus"]:
            kernel = Line([str(KERNEL)])
            self.addCleanup(kernel.close)
            reply = kernel.send(dict(action="configure", definition=ecu["definition"], step_ms=10, epoch=state["epoch"]))
            self.assertNotIn("error", reply, reply)
            self.kernels[ecu["name"]] = kernel
        return state

    def prepare(self):
        prepared = self.sim.send({"action": "prepare_tick"})
        results = []
        for node in prepared["nodes"]:
            result = self.kernels[node["name"]].send(dict(node, action="tick"))
            self.assertNotIn("error", result, result)
            results.append(result)
        return prepared, results

    def commit(self, results):
        state = self.sim.send(dict(action="complete_tick", results=results))
        self.assertNotIn("error", state, state)
        state = self.sim.send(dict(action="snapshot"))
        for ecu in state["ecus"]:
            resources = ecu["hardware"].get("resources")
            if resources:
                self.assertLessEqual(ecu["hardware"]["ram_used_bytes"], ecu["hardware"]["ram_capacity_bytes"])
                self.assertLessEqual(ecu["hardware"]["cpu_load_percent"], 100)
                for event in resources["events"]:
                    self.assertFalse(list(self.job_validator.iter_errors(event)), event)
        return state

    def tick(self):
        prepared, results = self.prepare()
        return self.commit(results), next(r for r in results if r["name"] == "Radar ECU")

    def radar(self, state):
        return next(e for e in state["ecus"] if e["name"] == "Radar ECU")

    def test_delayed_radar_retains_acquisition_age_despite_continuous_reception(self):
        self.configure(resource=False)
        self.sim.send(dict(action="network_policy", rules=[dict(bus="adas", id=1024, latency_ms=150)]))
        for _ in range(35):
            state, _ = self.tick()
        self.assertTrue(any(f["name"] == "RadarObjectList" and f.get("delivered_at_us") is not None for f in state["frames"]))
        adas = next(e for e in state["ecus"] if e["name"] == "ADAS ECU")
        self.assertGreaterEqual(adas["values"]["radar_age_ms"], 150)
        self.assertEqual(adas["values"]["adas_fault"], 1)

    def test_cross_tick_job_keeps_inputs_and_never_publishes_early(self):
        self.configure()
        first, result = self.tick()
        self.assertEqual(result["completed_jobs"], [])
        self.assertIsNone(result["timing"]["computed_at_us"])
        self.assertFalse(any(f["ecu"] == "Radar ECU" for f in first["frames"]))
        resource = self.radar(first)["hardware"]["resources"]
        frozen_hash = resource["pending"][0]["input_hash"]
        self.assertEqual(resource["pending"][0]["duration_us"], 15000)
        self.assertEqual(resource["busy_us"], 10000)
        self.sim.send(dict(action="environment", values=dict(lead_distance=40)))
        second, result = self.tick()
        publication = result["completed_jobs"][0]
        self.assertEqual(publication["input_hash"], frozen_hash)
        self.assertEqual(publication["values"]["obj0_range"], 120)
        self.assertEqual(publication["timing"]["sampled_at_us"], 0)
        self.assertEqual(publication["timing"]["computed_at_us"], 15000)
        self.assertEqual(publication["timing"]["deadline_at_us"], 100000)
        self.assertNotIn("task_budget_us", publication["timing"])
        frames = [f for f in second["frames"] if f["ecu"] == "Radar ECU"]
        self.assertTrue(frames)
        self.assertTrue(all(f["computed_at_us"] == 15000 and f["time"] * 1e6 >= 15000 for f in frames))

    def test_fast_and_slow_cpu_have_distinct_completion_boundaries(self):
        self.configure(cpu=120)
        state, result = self.tick()
        self.assertEqual(result["completed_jobs"][0]["timing"]["computed_at_us"], 7500)
        self.assertTrue(any(f["ecu"] == "Radar ECU" for f in state["frames"]))

    def test_completed_and_pending_retries_are_identical_and_conflicts_rejected(self):
        self.configure()
        prepared, results = self.prepare()
        request = dict(next(n for n in prepared["nodes"] if n["name"] == "Radar ECU"), action="tick")
        kernel = self.kernels["Radar ECU"]
        original = next(r for r in results if r["name"] == "Radar ECU")
        self.assertEqual(kernel.send(request), original)
        before = kernel.send(dict(action="snapshot"))
        conflict = json.loads(json.dumps(request))
        conflict["inputs"]["radar_distance_sensor"] += 10
        self.assertIn("Conflicting retry", kernel.send(conflict)["error"])
        self.assertEqual(kernel.send(dict(action="snapshot")), before)
        self.commit(results)
        prepared, results = self.prepare()
        bad = json.loads(json.dumps(results))
        radar = next(r for r in bad if r["name"] == "Radar ECU")
        radar["completed_jobs"][0]["epoch"] -= 1
        self.assertIn("epoch", self.sim.send(dict(action="complete_tick", results=bad))["error"])
        self.assertEqual(self.sim.send(dict(action="snapshot"))["tick"], 1)
        for node, expected in zip(prepared["nodes"], results):
            self.assertEqual(self.kernels[node["name"]].send(dict(node, action="tick")), expected)
        self.assertEqual(self.commit(results)["tick"], 2)

    def test_queue_bound_rejects_new_work_and_releases_ram_once(self):
        self.configure(dict(max_pending_jobs=1))
        first, _ = self.tick()
        self.assertGreater(self.radar(first)["hardware"]["resources"]["reserved_bytes"], 0)
        second, result = self.tick()
        resources = self.radar(second)["hardware"]["resources"]
        self.assertEqual(resources["counters"]["queue_full"], 1)
        self.assertEqual(resources["reserved_bytes"], 0)
        self.assertEqual(len(result["completed_jobs"]), 1)
        self.assertEqual(resources["pending"], [])

    def test_ram_bound_rejects_before_input_copy_or_publication(self):
        self.configure(dict(queue_bytes=512))
        state, result = self.tick()
        resources = self.radar(state)["hardware"]["resources"]
        self.assertEqual(resources["counters"]["ram_exhausted"], 1)
        self.assertEqual(resources["reserved_bytes"], 0)
        self.assertEqual(resources["pending"], [])
        self.assertEqual(result["completed_jobs"], [])
        self.assertEqual(self.radar(state)["hardware"]["cpu_load_percent"], 0)

    def test_deadlines_cancel_jobs_without_claiming_computation(self):
        self.configure(dict(deadline_us=12000))
        for _ in range(4):
            state, result = self.tick()
            self.assertEqual(result["completed_jobs"], [])
        events = self.radar(state)["hardware"]["resources"]["events"]
        failed = [e for e in events if e["state"] == "deadline_failed"]
        self.assertTrue(failed)
        self.assertTrue(all("computed_at_us" not in e for e in failed))
        self.assertFalse(any(f["ecu"] == "Radar ECU" for f in state["frames"]))

    def test_watchdog_latches_and_cancels_queued_work(self):
        self.configure(dict(watchdog_us=12000))
        self.tick()
        second, _ = self.tick()
        hardware = self.radar(second)["hardware"]
        self.assertEqual(hardware["watchdog"], "TRIPPED")
        self.assertEqual(hardware["resources"]["reserved_bytes"], 0)
        self.assertEqual(hardware["resources"]["pending"], [])
        self.assertEqual(hardware["cpu_load_percent"], 20)
        third, result = self.tick()
        self.assertEqual(result["completed_jobs"], [])
        self.assertEqual(self.radar(third)["hardware"]["resources"]["counters"]["watchdog_latched"], 1)

    def test_burst_completions_encode_each_jobs_values_and_timestamp(self):
        self.configure(cpu=120, burst=True)
        self.tick()
        second, result = self.tick()
        self.assertEqual(len(result["completed_jobs"]), 2)
        self.assertEqual([r["timing"]["computed_at_us"] for r in result["completed_jobs"]], [15000, 16000])
        frames = [f for f in second["frames"] if f["ecu"] == "Radar ECU" and not f["gateway"]]
        self.assertEqual([f["computed_at_us"] for f in frames], [15000, 16000])
        self.assertEqual([f["signals"]["sample_counter"] for f in frames], [1, 2])

    def test_only_latest_completed_actuator_state_is_applied_at_boundary(self):
        self.configure(cpu=120, burst=True, target="Motor ECU", initial_state=dict(torque_request=15))
        self.sim.send(dict(action="controls", values=dict(throttle=0.8)))
        self.tick()
        self.sim.send(dict(action="controls", values=dict(throttle=0.2)))
        prepared, results = self.prepare()
        motor = next(r for r in results if r["name"] == "Motor ECU")
        self.assertEqual(len(motor["completed_jobs"]), 2)
        definition = next(e for e in self.sim.send(dict(action="snapshot"))["ecus"] if e["name"] == "Motor ECU")["definition"]
        signal = definition["bindings"]["torque_request"]
        outputs = [job["values"][signal] for job in motor["completed_jobs"]]
        self.assertNotEqual(outputs[0], outputs[1])
        state = self.commit(results)
        commits = [e for e in state["timing"]["actuations"] if e["name"] == "torque_request" and e["tick"] == 1]
        self.assertEqual(len(commits), 1)
        self.assertEqual(commits[0]["value"], outputs[-1])
        self.assertEqual(state["world"]["torque_request"], outputs[-1])
        self.assertEqual(commits[0]["timestamps"]["computed_at_us"], motor["completed_jobs"][-1]["timing"]["computed_at_us"])
        acquisition = motor["completed_jobs"][-1]["acquisition"]["sampled_at_us"]
        if acquisition is None:
            self.assertNotIn("sampled_at_us", commits[0]["timestamps"])
            self.assertEqual(commits[0]["validity"], "unknown")
        else:
            self.assertEqual(commits[0]["timestamps"]["sampled_at_us"], acquisition)

    def test_resource_chain_includes_age_before_job_release(self):
        def isolated_sensor(project, definition):
            # Keep the established TX layouts, but drive the actuator from one
            # explicit 20ms sensor so original acquisition has an independent oracle.
            for name in definition["inputs"]:
                definition["variables"][name] = 0
            definition["inputs"] = ["front_perception_sensor"]
            definition["messages"] = [m for m in definition["messages"] if m["direction"] == "tx"]
            project["can_map"] = [m for m in project["can_map"] if not (m["ecu"] == "Motor ECU" and m["direction"] == "rx")]
            torque = definition["bindings"]["torque_request"]
            definition["bindings"] = dict(torque_request=torque)
            definition["tasks"] = [dict(name="range_observer", period_ms=10, cost_us=15000,
                assign={torque: dict(op="div", args=["front_perception_sensor", 10])})]
            definition["state_machine"] = dict(initial="RUN", states=["RUN"], transitions=[])
            definition["dtcs"] = []
        self.configure(cpu=120, target="Motor ECU", transform=isolated_sensor)
        for _ in range(3):
            state, _ = self.tick()
        metrics = next(e for e in state["performance"]["ecus"] if e["name"] == "Motor ECU")
        # Second job: release 10ms, uses the sensor's 0ms acquisition, finishes
        # at 30ms. Queue time and sensor hold time must both remain in E2E age.
        self.assertEqual(metrics["e2e"]["p95"], 30000)

    def test_invalid_resource_import_preserves_live_clock_and_state(self):
        self.configure(resource=False)
        for _ in range(5):
            before, _ = self.tick()
        project = self.sim.send(dict(action="export_project"))
        radar = next(d for d in project["ecu_definitions"].values() if d["name"] == "Radar ECU")
        radar["hardware"].update(execution_profile="resource-v1", scheduler=dict(max_pending_jobs=0))
        project["project_hash"] = self.sim.send(dict(action="hash_project", project=project))["project_hash"]
        self.assertIn("limits", self.sim.send(dict(action="import_project", project=project))["error"])
        self.assertEqual(self.sim.send(dict(action="snapshot")), before)

    def test_project_hash_compatibility_and_resource_profile_persistence(self):
        self.assertEqual(self.project["project_hash"], "sha256:4843e7e92081e52c8c5a5fca226041fe4e88e71cc872165f1b2e96b88d545ecb")
        self.configure()
        exported = self.sim.send(dict(action="export_project"))
        self.assertNotEqual(exported["project_hash"], self.project["project_hash"])
        radar = next(d for d in exported["ecu_definitions"].values() if d["name"] == "Radar ECU")
        self.assertEqual(radar["hardware"]["execution_profile"], "resource-v1")
        self.assertEqual(radar["hardware"]["scheduler"]["deadline_us"], 100000)


@unittest.skipUnless(KERNEL.exists(), "native ECU binary required")
class AcquisitionAndResetChecks(unittest.TestCase):
    def setUp(self):
        self.kernel = Line([str(KERNEL)])
        self.addCleanup(self.kernel.close)
        self.definition = dict(schema_version=1, name="Timing probe", inputs=["age_ms", "value"],
            variables=dict(out=0), tasks=[dict(name="copy", period_ms=10, cost_us=100, assign=dict(out="value"))],
            bindings={}, freshness_inputs=dict(age_ms=dict(id=17)),
            messages=[dict(name="Input", id=17, direction="rx", dlc=1,
                signals=[dict(name="value", start=0, length=8, min=0, max=255)]),
                dict(name="Output", id=18, direction="tx", dlc=1, period_ms=10,
                signals=[dict(name="out", start=0, length=8, min=0, max=255)])],
            state_machine=dict(initial="RUN", states=["RUN"], transitions=[]), dtcs=[],
            hardware=dict(cpu_mhz=120, ram_kib=64, flash_kib=64))

    def configure(self, definition=None, epoch=7, tick=20):
        result = self.kernel.send(dict(action="configure", definition=definition or self.definition,
                                       epoch=epoch, start_tick=tick))
        self.assertNotIn("error", result, result)

    def request(self, tick=20, sampled=0, epoch=7, metadata=True):
        frame = dict(id=17, data=[9])
        if metadata:
            frame["acquisition"] = dict(epoch=epoch, sampled_at_us=sampled, validity="valid")
        return dict(action="tick", tick=tick, epoch=epoch, inputs={}, frames=[frame])

    def test_age_uses_original_acquisition_and_legacy_mode_is_explicit(self):
        self.configure()
        self.assertEqual(self.kernel.send(self.request())["values"]["age_ms"], 200)
        self.assertEqual(self.kernel.send(self.request(tick=21))["values"]["age_ms"], 210)
        self.definition["hardware"]["freshness_mode"] = "reception"
        self.configure()
        self.assertEqual(self.kernel.send(self.request())["values"]["age_ms"], 0)

    def test_unknown_future_and_old_epoch_metadata_never_become_fresh(self):
        self.configure()
        before = self.kernel.send(dict(action="snapshot"))
        future = self.request(sampled=200001)
        self.assertIn("Future", self.kernel.send(future)["error"])
        old = self.request()
        old["frames"][0]["acquisition"]["epoch"] = 6
        self.assertIn("epoch", self.kernel.send(old)["error"])
        self.assertEqual(self.kernel.send(dict(action="snapshot")), before)
        unknown = self.kernel.send(self.request(metadata=False))
        self.assertGreater(unknown["values"]["age_ms"], 1000000)

    def test_epoch_reset_cancels_pending_jobs_and_never_publishes_old_inputs(self):
        definition = json.loads(json.dumps(self.definition))
        definition["tasks"][0]["cost_us"] = 15000
        definition["hardware"]["execution_profile"] = "resource-v1"
        self.configure(definition, epoch=1, tick=0)
        old = self.request(tick=0, epoch=1)
        self.assertEqual(self.kernel.send(old)["completed_jobs"], [])
        busy = self.kernel.send(dict(action="uds", bytes=[0x14, 255, 255, 255]))
        self.assertEqual(busy["bytes"], [0x7f, 0x14, 0x21])
        reset = self.kernel.send(dict(action="reset", epoch=2))
        events = reset["hardware"]["resources"]["events"]
        self.assertTrue(any(e["epoch"] == 1 and e["state"] == "cancelled" for e in events))
        self.assertEqual(reset["hardware"]["resources"]["reserved_bytes"], 0)
        self.assertEqual(self.kernel.send(dict(action="uds", bytes=[0x14, 255, 255, 255]))["bytes"], [0x54])
        self.assertIn("epoch", self.kernel.send(old)["error"])
        new = self.request(tick=0, epoch=2)
        new["frames"][0]["data"] = [33]
        self.assertEqual(self.kernel.send(new)["completed_jobs"], [])
        new["tick"] = 1
        result = self.kernel.send(new)
        self.assertEqual(result["completed_jobs"][0]["values"]["out"], 33)
        self.assertTrue(all(r["epoch"] == 2 for r in result["completed_jobs"]))

    def test_reset_cannot_reuse_a_retired_identity_and_policy_typos_reject(self):
        self.configure()
        self.kernel.send(self.request())
        before = self.kernel.send(dict(action="snapshot"))
        self.assertIn("epoch", self.kernel.send(dict(action="reset", epoch=7))["error"])
        self.assertEqual(self.kernel.send(dict(action="snapshot")), before)
        definition = json.loads(json.dumps(self.definition))
        definition["hardware"].update(execution_profile="resource-v1", scheduler=dict(unknown_option=1))
        self.assertIn("Unknown scheduler", self.kernel.send(dict(action="configure", definition=definition))["error"])

    def test_memory_flash_and_timestamp_overflow_reject_explicitly(self):
        self.configure()
        before = self.kernel.send(dict(action="snapshot"))
        request = dict(action="reset", epoch=8, start_tick=2**64 - 1)
        self.assertIn("overflow", self.kernel.send(request)["error"])
        self.assertEqual(self.kernel.send(dict(action="snapshot")), before)
        bad = json.loads(json.dumps(self.definition))
        bad["hardware"]["flash_kib"] = 1
        bad["assumptions"] = ["x" * 2048]
        self.assertIn("flash", self.kernel.send(dict(action="configure", definition=bad))["error"])
        bad = json.loads(json.dumps(self.definition))
        bad["hardware"].update(execution_profile="resource-v1", ram_kib=4)
        self.assertIn("RAM", self.kernel.send(dict(action="configure", definition=bad))["error"])


if __name__ == "__main__":
    unittest.main()
