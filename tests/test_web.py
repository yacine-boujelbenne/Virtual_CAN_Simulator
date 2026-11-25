"""HTTP integration checks against a running container/native server."""
import json
import os
import unittest
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

URL = os.environ.get("TEST_URL", "http://127.0.0.1:8080")


def request(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(URL + path, data=data,
                                 headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=90) as response:
        text = response.read().decode()
        return json.loads(text) if response.headers.get_content_type() == "application/json" else text


class WebIntegration(unittest.TestCase):
    def setUp(self):
        request("/api/command", {"action": "reset"})

    def test_health_and_all_browser_assets(self):
        self.assertEqual(request("/health")["status"], "ok")
        self.assertIn("ECU Lab", request("/"))
        self.assertIn("function render", request("/app.js"))
        self.assertIn("--cyan", request("/style.css"))

    def test_vehicle_and_fault_chain(self):
        request("/api/command", {"action": "controls", "values": {"throttle": 0.6}})
        state = request("/api/command", {"action": "step", "count": 1000})
        self.assertGreater(state["world"]["speed"], 20)
        request("/api/command", {"action": "fault", "sensor": "temperature_sensor", "mode": "stuck", "value": 120})
        state = request("/api/command", {"action": "step", "count": 20})
        cluster = next(e for e in state["ecus"] if e["name"] == "Instrument Cluster")
        self.assertEqual(cluster["values"]["warning"], 1)
        self.assertGreater(len(state["frames"]), 0)
        self.assertIn("signals_json", request("/api/export"))

    def test_multi_frame_uds_and_negative_response(self):
        response = request("/api/command", {"action": "diagnostic", "ecu": "Motor ECU", "bytes": [34, 241, 144]})
        self.assertEqual(len(response["bytes"]), 20)
        self.assertEqual(response["bytes"][0], 98)
        response = request("/api/command", {"action": "diagnostic", "ecu": "Motor ECU", "bytes": [39, 1]})
        self.assertEqual(response["bytes"], [127, 39, 17])

    def test_offline_builder_validates_and_adds_an_observation_ecu(self):
        result = request("/api/generate", {"prompt": "Create an ABS ECU"})
        if result["provider"] != "offline template builder":
            self.skipTest("Configured external AI endpoint")
        self.assertTrue(result["validated"])
        self.assertFalse(result["definition"]["bindings"])
        state = request("/api/command", {"action": "add_ecu", "definition": result["definition"]})
        self.assertEqual(len(state["ecus"]), 8)
        request("/api/command", {"action": "step", "count": 20})

    def test_dbc_import_and_rejection(self):
        text = (Path(__file__).resolve().parent.parent / "resources/vehicle.dbc").read_text()
        result = request("/api/dbc", {"text": text})
        self.assertEqual(len(result["definition"]["messages"]), 4)
        with self.assertRaises(urllib.error.HTTPError) as error:
            request("/api/dbc", {"text": text.replace("@1+", "@0+")})
        self.assertEqual(error.exception.code, 400)

    def test_invalid_input_rejected_and_engine_remains_available(self):
        with self.assertRaises(urllib.error.HTTPError) as error:
            request("/api/command", {"action": "controls", "values": {"throttle": 10}})
        self.assertEqual(error.exception.code, 400)
        self.assertEqual(request("/health")["status"], "ok")

    def test_malformed_definition_does_not_kill_runtime(self):
        definition = request("/api/templates")["motor"]
        del definition["state_machine"]
        with self.assertRaises(urllib.error.HTTPError) as error:
            request("/api/command", {"action": "validate", "definition": definition})
        self.assertEqual(error.exception.code, 400)
        self.assertEqual(request("/health")["status"], "ok")

    def test_concurrent_readers_share_one_clock(self):
        request("/api/command", {"action": "step", "count": 100})
        with ThreadPoolExecutor(max_workers=5) as pool:
            results = list(pool.map(lambda _: request("/api/state"), range(10)))
        self.assertTrue(all(s["time"] == 1 for s in results))


if __name__ == "__main__":
    unittest.main()
