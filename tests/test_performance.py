"""Performance dashboard checks: latency decomposition, throughput and backlog.

The metrics are computed by the C++ core (it owns the simulation clock), so the
test drives the native binary directly and compares the aggregates with the
retained measurement envelopes.
"""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "build" / "vehicle_sim"
SCENARIO = ROOT / "resources" / "scenarios" / "ev_basic.json"
sys.path.insert(0, str(ROOT / "app"))
from process import KernelProcess  # noqa: E402


@unittest.skipUnless(SIM.exists(), "build/vehicle_sim required")
class Performance(unittest.TestCase):
    def setUp(self):
        self.core = KernelProcess([str(SIM), str(SCENARIO)], str(ROOT))
        self.addCleanup(self.core.close)

    def performance(self):
        return self.core.request({"action": "snapshot"})["performance"]

    def test_snapshot_exposes_a_performance_window(self):
        perf = self.performance()
        self.assertIn("ecus", perf)
        self.assertIn("buses", perf)
        totals = perf["totals"]
        for key in ("frames", "dropped", "frame_rate_hz", "task_p95_us", "delivery_p95_us", "chain_p95_us"):
            self.assertIn(key, totals)
        self.assertEqual(len(perf["ecus"]), len(self.core.request({"action": "snapshot"})["ecus"]))

    def test_latency_percentiles_are_ordered(self):
        self.core.request({"action": "controls", "values": {"throttle": 0.6}})
        self.core.request({"action": "step", "count": 120})
        perf = self.performance()
        for ecu in perf["ecus"]:
            for section in ("task", "e2e", "delivery", "actuation"):
                stats = ecu[section]
                self.assertLessEqual(stats["p50"], stats["p95"], ecu)
                self.assertLessEqual(stats["p95"], stats["max"], ecu)
                self.assertLessEqual(stats["mean"], stats["max"], ecu)
            self.assertGreater(ecu["task"]["count"], 0)
            self.assertLess(ecu["task"]["max"], 10000, "a task must fit inside the tick window")

    def test_control_chain_is_quantized_by_the_barrier(self):
        self.core.request({"action": "step", "count": 40})
        perf = self.performance()
        acting = [ecu for ecu in perf["ecus"] if ecu["e2e"]["count"]]
        self.assertTrue(acting, "at least one ECU acts on the plant")
        for ecu in acting:
            # sampled -> applied spans exactly one barrier period.
            self.assertAlmostEqual(ecu["e2e"]["p50"], 10000, delta=1)
            # computed -> applied is the barrier period minus the task cost.
            self.assertAlmostEqual(ecu["actuation"]["p50"], 10000 - ecu["task"]["p50"], delta=1)

    def test_throughput_counters_follow_the_frames(self):
        self.core.request({"action": "step", "count": 50})
        perf = self.performance()
        self.assertGreater(perf["totals"]["frames"], 0)
        self.assertGreater(perf["totals"]["frame_rate_hz"], 0)
        self.assertEqual(perf["totals"]["dropped"], sum(bus["dropped"] for bus in perf["buses"]))
        for ecu in perf["ecus"]:
            self.assertEqual(ecu["dropped"], 0)
        for bus in perf["buses"]:
            self.assertGreaterEqual(bus["load_percent"], 0)
            self.assertLess(bus["load_percent"], 100)
            self.assertGreaterEqual(bus["backlog_us"], 0)

    def test_dropped_frames_are_counted(self):
        self.core.request({"action": "network_policy", "rules": [{"source": "Motor ECU", "drop": True}]})
        self.core.request({"action": "step", "count": 30})
        perf = self.performance()
        motor = next(ecu for ecu in perf["ecus"] if ecu["name"] == "Motor ECU")
        self.assertGreater(motor["dropped"], 0)
        self.assertGreater(perf["totals"]["dropped"], 0)

    def test_timeline_samples_follow_the_simulation_clock(self):
        self.core.request({"action": "step", "count": 60})
        perf = self.performance()
        for ecu in perf["ecus"]:
            stamps = [sample["t_us"] for sample in ecu["timeline"]]
            self.assertEqual(stamps, sorted(stamps), ecu["name"])
            for sample in ecu["timeline"]:
                self.assertGreaterEqual(sample["t_us"], 0)
                for key in ("task_us", "delivery_us", "chain_us"):
                    if key in sample:
                        self.assertGreaterEqual(sample[key], 0)

    def test_reset_clears_the_measurement_window(self):
        self.core.request({"action": "step", "count": 30})
        self.assertGreater(self.performance()["totals"]["frames"], 0)
        self.core.request({"action": "reset"})
        perf = self.performance()
        self.assertEqual(perf["totals"]["frames"], 0)
        self.assertEqual(perf["totals"]["ticks"], 0)
        for ecu in perf["ecus"]:
            self.assertEqual(ecu["tx"], 0)
            self.assertEqual(ecu["task"]["count"], 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
