"""Multi-object perception and fusion checks (P0.3).

Replays the distributed protocol against the native binaries:
  build/vehicle_sim  (controller core)
  build/ecu_kernel   (one process per ECU, as in Compose)
Every test starts from a fresh controller and a fresh set of ECU kernels.
"""
import json
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = Path(os.environ.get("SIM_BINARY", ROOT / "build" / "vehicle_sim"))
KERNEL = Path(os.environ.get("ECU_BINARY", ROOT / "build" / "ecu_kernel"))
RADAR_ID = 1024
OBJECTS = ["obj0", "obj1", "obj2", "obj3"]
OBJECT_SIGNALS = [f"{prefix}_{field}" for prefix in OBJECTS
                  for field in ("range", "closing_speed", "confidence", "valid")]
LIST_SIGNALS = ["object_count", "sample_counter", "radar_valid"] + OBJECT_SIGNALS


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
class FusionChecks(unittest.TestCase):
    def setUp(self):
        self.sim = Line([str(SIM), "resources/scenarios/ev_basic.json"])
        state = self.sim.send({"action": "snapshot"})
        self.assertNotIn("error", state, state)
        self.step = state["step_ms"]
        self.kernels = {}
        for ecu in state["ecus"]:
            kernel = Line([str(KERNEL)])
            reply = kernel.send({"action": "configure", "definition": ecu["definition"],
                                 "step_ms": self.step, "epoch": state["epoch"]})
            self.assertNotIn("error", reply, reply)
            self.kernels[ecu["name"]] = kernel

    def tearDown(self):
        for kernel in self.kernels.values():
            kernel.close()
        self.sim.close()

    def tick(self):
        prepared = self.sim.send({"action": "prepare_tick"})
        results = []
        for node in prepared["nodes"]:
            result = self.kernels[node["name"]].send({**node, "action": "tick"})
            self.assertNotIn("error", result, result)
            results.append(result)
        committed = self.sim.send({"action": "complete_tick", "results": results})
        self.assertNotIn("error", committed, committed)
        return committed

    def run_ticks(self, count):
        for _ in range(count):
            self.tick()

    def command(self, action, **extra):
        state = self.sim.send({"action": action, **extra})
        self.assertNotIn("error", state, state)
        return state

    def snapshot(self):
        state = self.sim.send({"action": "snapshot"})
        self.assertNotIn("error", state, state)
        return state

    def values(self, state, ecu):
        return next(e for e in state["ecus"] if e["name"] == ecu)["values"]

    def radar_list(self, state):
        """Last RadarObjectList frame that actually reached the bus receivers."""
        frames = [f for f in state["frames"]
                  if f.get("id") == RADAR_ID and f.get("delivered_at_us") is not None]
        self.assertTrue(frames, "no delivered radar frame")
        return frames[-1]

    def emergency_scenario(self):
        """90 km/h ego vehicle closing on a stopped lead vehicle at 45 m."""
        self.command("controls", values={"throttle": 0, "lead_speed": 0, "aeb_enabled": 1})
        self.command("environment", values={"speed": 25, "lead_distance": 45})
        self.run_ticks(25)

    def test_four_object_slots_are_encoded_and_decoded_identically(self):
        self.run_ticks(8)
        state = self.snapshot()
        radar = self.values(state, "Radar ECU")
        adas = self.values(state, "ADAS ECU")
        frame = self.radar_list(state)
        decoded = frame["signals"]
        self.assertEqual(sorted(decoded), sorted(LIST_SIGNALS), decoded)
        # The transmitter and the receiver must decode the very same wire content.
        for signal in LIST_SIGNALS:
            self.assertEqual(adas[signal], decoded[signal], signal)
        # Two populated slots (lead vehicle + static obstacle), two empty slots.
        self.assertEqual(radar["object_count"], 2)
        self.assertEqual(radar["obj0_valid"], 1)
        self.assertEqual(radar["obj1_valid"], 1)
        self.assertEqual(radar["obj2_valid"], 0)
        self.assertEqual(radar["obj3_valid"], 0)
        self.assertEqual(radar["obj2_range"], 0)
        self.assertEqual(radar["obj3_range"], 0)
        self.assertEqual(decoded["object_count"], 2)
        self.assertLess(abs(decoded["obj0_range"] - state["world"]["lead_distance"]), 1.0, decoded)
        self.assertLess(abs(decoded["obj1_range"] - state["world"]["static_object_range"]), 1.0, decoded)
        # The obstacle is static: its closing speed is the ego speed.
        self.assertLess(abs(decoded["obj1_closing_speed"] - state["world"]["speed"]), 1.0, decoded)

    def test_disconnected_static_sensor_invalidates_only_the_second_slot(self):
        self.run_ticks(4)
        self.command("fault", sensor="static_object_sensor", mode="disconnected")
        self.run_ticks(4)
        state = self.snapshot()
        radar = self.values(state, "Radar ECU")
        adas = self.values(state, "ADAS ECU")
        self.assertEqual(radar["obj1_valid"], 0)
        self.assertEqual(radar["obj1_confidence"], 0)
        self.assertEqual(radar["obj0_valid"], 1)
        self.assertEqual(radar["object_count"], 1)
        self.assertEqual(radar["obj1_range"], 510)
        self.assertEqual(adas["obj1_valid"], 0)
        self.assertEqual(adas["object_count"], 1)
        self.assertEqual(adas["obj0_valid"], 1)

    def test_fusion_disagreement_inhibits_emergency_braking_but_keeps_the_warning(self):
        self.emergency_scenario()
        state = self.snapshot()
        adas = self.values(state, "ADAS ECU")
        self.assertEqual(adas["perception_valid"], 1)
        self.assertEqual(adas["fusion_disagree"], 0)
        self.assertLess(adas["perception_gap"], 5)
        self.assertEqual(adas["fcw"], 1)
        self.assertEqual(adas["aeb_request"], 1)
        # A biased second measurement of the same range makes fusion disagree.
        self.command("fault", sensor="front_perception_sensor", mode="bias", value=10)
        self.run_ticks(6)
        state = self.snapshot()
        adas = self.values(state, "ADAS ECU")
        self.assertEqual(adas["fusion_disagree"], 1)
        self.assertEqual(adas["perception_valid"], 0)
        self.assertGreater(adas["perception_gap"], 5)
        self.assertEqual(adas["aeb_request"], 0)
        self.assertEqual(adas["fcw"], 1)

    def test_stale_radar_traffic_raises_the_adas_fault_and_clears_the_warning(self):
        self.emergency_scenario()
        state = self.snapshot()
        self.assertEqual(self.values(state, "ADAS ECU")["fcw"], 1)
        self.command("network_policy", rules=[{"source": "Radar ECU", "bus": "adas", "drop": True}])
        self.run_ticks(20)
        state = self.snapshot()
        adas = self.values(state, "ADAS ECU")
        self.assertGreater(adas["radar_age_ms"], 100)
        self.assertEqual(adas["adas_fault"], 1)
        self.assertEqual(adas["fcw"], 0)
        self.assertEqual(adas["aeb_request"], 0)
        self.assertEqual(next(e for e in state["ecus"] if e["name"] == "ADAS ECU")["state"], "FAULT")
        # Traffic restored: the perception input becomes fresh again.
        self.command("network_policy", rules=[])
        self.run_ticks(20)
        state = self.snapshot()
        adas = self.values(state, "ADAS ECU")
        self.assertLessEqual(adas["radar_age_ms"], 100)
        self.assertEqual(adas["adas_fault"], 0)
        self.assertEqual(adas["fcw"], 1)

    def test_radar_sensor_fault_invalidates_the_first_object_and_raises_the_adas_fault(self):
        self.command("fault", sensor="radar_distance_sensor", mode="stuck", value=700)
        self.run_ticks(20)
        state = self.snapshot()
        radar = self.values(state, "Radar ECU")
        adas = self.values(state, "ADAS ECU")
        self.assertEqual(radar["radar_fault"], 1)
        self.assertEqual(radar["radar_valid"], 0)
        self.assertEqual(radar["obj0_valid"], 0)
        self.assertEqual(radar["obj0_confidence"], 0)
        self.assertEqual(radar["object_count"], 1)
        self.assertEqual(adas["radar_valid"], 0)
        self.assertEqual(adas["adas_fault"], 1)
        self.assertEqual(adas["fcw"], 0)
        self.assertEqual(adas["aeb_request"], 0)
        codes = [d["code"] for d in next(e for e in state["ecus"] if e["name"] == "Radar ECU")["dtcs"]]
        self.assertIn("C1A10", codes)


if __name__ == "__main__":
    unittest.main()
