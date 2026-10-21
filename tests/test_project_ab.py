"""Project export/import A/B gate: a .vcanproj always identifies and replays the same laboratory.

The gate checks the C++ core directly (build/vehicle_sim) so the hash and the run
signature are compared without any HTTP layer in between. The HTTP surface is
covered too when a server is running (TEST_URL).
"""
import json
import os
import re
import sys
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "build" / "vehicle_sim"
SCENARIO = ROOT / "resources" / "scenarios" / "ev_basic.json"
sys.path.insert(0, str(ROOT / "app"))
from process import KernelProcess  # noqa: E402

URL = os.environ.get("TEST_URL", "")
HASH = re.compile(r"^sha256:[0-9a-f]{64}$")


def frame_signature(state):
    return [(frame["time"], frame["id"], frame["ecu"], frame["name"], frame["bytes"])
            for frame in state["frames"]]


def envelope_signature(state):
    timing = state["timing"]
    return [(envelope.get("kind"), envelope.get("source", {}).get("ecu"), envelope.get("name"),
             envelope.get("value"), envelope.get("unit"), envelope.get("sim_time_us"),
             envelope.get("timestamps", {}).get("sampled_at_us"))
            for envelope in timing["envelopes"] + timing.get("actuations", [])]


@unittest.skipUnless(SIM.exists(), "build/vehicle_sim required")
class ProjectAB(unittest.TestCase):
    def setUp(self):
        self.core = KernelProcess([str(SIM), str(SCENARIO)], str(ROOT))
        self.addCleanup(self.core.close)

    def test_export_shape_and_hash(self):
        project = self.core.request({"action": "export_project"})
        self.assertEqual(project["kind"], "vcanproj")
        self.assertEqual(project["schema_version"], 1)
        self.assertRegex(project["project_hash"], HASH)
        self.assertIn("scenario", project)
        self.assertIn("ecu_definitions", project)
        self.assertIn("can_map", project)
        self.assertEqual(project["run"]["step_ms"], 10)
        self.assertTrue(project["can_map"])
        for entry in project["can_map"]:
            self.assertIn("ecu", entry)
            self.assertIn("message", entry)
            self.assertIn("id", entry)
        self.assertEqual(self.core.request({"action": "snapshot"})["project_hash"], project["project_hash"])

    def test_round_trip_keeps_hash(self):
        project = self.core.request({"action": "export_project"})
        other = KernelProcess([str(SIM), str(SCENARIO)], str(ROOT))
        self.addCleanup(other.close)
        result = other.request({"action": "import_project", "project": project})
        self.assertTrue(result["imported"])
        self.assertEqual(result["project_hash"], project["project_hash"])
        self.assertEqual(other.request({"action": "snapshot"})["project_hash"], project["project_hash"])

    def test_imported_project_replays_the_same_run(self):
        project = self.core.request({"action": "export_project"})
        other = KernelProcess([str(SIM), str(SCENARIO)], str(ROOT))
        self.addCleanup(other.close)
        other.request({"action": "import_project", "project": project})
        first = self.core.request({"action": "step", "count": 60})
        second = other.request({"action": "step", "count": 60})
        self.assertTrue(frame_signature(first))
        self.assertEqual(frame_signature(first), frame_signature(second))
        self.assertEqual(envelope_signature(first), envelope_signature(second))

    def test_modified_project_has_another_hash(self):
        project = self.core.request({"action": "export_project"})
        variant = json.loads(json.dumps(project))
        variant["scenario"]["sensors"][0]["max"] = 42
        rehashed = self.core.request({"action": "hash_project", "project": variant})
        self.assertNotEqual(rehashed["project_hash"], project["project_hash"])
        variant["project_hash"] = rehashed["project_hash"]
        result = self.core.request({"action": "import_project", "project": variant})
        self.assertEqual(result["project_hash"], rehashed["project_hash"])
        state = self.core.request({"action": "snapshot"})
        self.assertEqual(state["project_hash"], rehashed["project_hash"])
        self.assertEqual(state["sensors"][0]["max"], 42)

    def test_tampered_project_is_refused_and_state_survives(self):
        project = self.core.request({"action": "export_project"})
        broken = json.loads(json.dumps(project))
        broken["scenario"]["bitrate"] = 250000
        with self.assertRaises(ValueError) as failure:
            self.core.request({"action": "import_project", "project": broken})
        self.assertIn("hash mismatch", str(failure.exception))
        self.assertEqual(self.core.request({"action": "snapshot"})["project_hash"], project["project_hash"])

    def test_project_without_definitions_is_refused(self):
        project = self.core.request({"action": "export_project"})
        project["ecu_definitions"] = {}
        with self.assertRaises(ValueError):
            self.core.request({"action": "import_project", "project": project})
        self.assertEqual(self.core.request({"action": "snapshot"})["project_hash"],
                         self.core.request({"action": "export_project"})["project_hash"])


@unittest.skipUnless(URL, "TEST_URL not set")
class ProjectHttp(unittest.TestCase):
    def request(self, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(URL + path, data=data,
                                     headers={"Content-Type": "application/json"} if data else {})
        with urllib.request.urlopen(req, timeout=90) as response:
            return json.loads(response.read().decode())

    def test_export_and_import_over_http(self):
        project = self.request("/api/project")
        self.assertEqual(project["kind"], "vcanproj")
        self.assertRegex(project["project_hash"], HASH)
        result = self.request("/api/project", {"project": project})
        self.assertTrue(result["imported"])
        self.assertEqual(result["project_hash"], project["project_hash"])

    def test_tampered_project_is_refused_over_http(self):
        project = self.request("/api/project")
        project["scenario"]["bitrate"] = 250000
        with self.assertRaises(urllib.error.HTTPError) as failure:
            self.request("/api/project", {"project": project})
        self.assertEqual(failure.exception.code, 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)
