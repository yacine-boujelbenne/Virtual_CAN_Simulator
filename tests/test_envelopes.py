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
SIM = ROOT / "build" / "vehicle_sim"
KERNEL = ROOT / "build" / "ecu_kernel"
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


if __name__ == "__main__":
    unittest.main()
