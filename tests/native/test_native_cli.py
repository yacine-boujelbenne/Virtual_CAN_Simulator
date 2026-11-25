"""Exercise actual native executables; Python is only the integration-test driver."""
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import urllib.request

CLI, WORKER = sys.argv[1:3]
del sys.argv[1:3]
NAMES = ["Motor ECU", "ABS ECU", "BMS ECU", "Dynamics ECU", "Instrument Cluster", "Radar ECU", "ADAS ECU"]


class Client:
    def __init__(self, *args):
        self.process = subprocess.Popen([CLI, *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True)

    def request(self, body):
        self.process.stdin.write(json.dumps(body) + "\n")
        self.process.stdin.flush()
        if not select.select([self.process.stdout], [], [], 20)[0]:
            raise AssertionError("Native CLI response timeout")
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError("Native CLI exited: " + self.process.stderr.read())
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()
        self.process.stderr.close()


class NativeCLI(unittest.TestCase):
    def setUp(self):
        self.workers = []
        self.addCleanup(self.stop_workers)
        self.endpoints = {}
        for name in NAMES:
            process = subprocess.Popen([WORKER, "--http", "0"], stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.workers.append(process)
            self.assertTrue(select.select([process.stdout], [], [], 10)[0], "Native worker startup timeout")
            ready = json.loads(process.stdout.readline())
            self.endpoints[name] = "http://127.0.0.1:" + str(ready["port"])
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.mapping = Path(self.directory.name) / "endpoints.json"
        self.mapping.write_text(json.dumps({"workers": self.endpoints}))

    def stop_workers(self):
        for worker in self.workers:
            if worker.poll() is None:
                worker.terminate()
            try:
                worker.wait(timeout=8)
            except subprocess.TimeoutExpired:
                worker.kill()
                worker.wait()
            worker.stdout.close()
            worker.stderr.close()

    def client(self, remote=True):
        client = Client(*(["--endpoints", str(self.mapping)] if remote else []))
        self.addCleanup(client.close)
        return client

    def test_http_matches_native_process_signatures_and_diagnostics(self):
        remote, local = self.client(), self.client(False)
        for client in (remote, local):
            self.assertNotIn("error", client.request({"action": "controls", "values": {"throttle": 0.6}}))
        actual = remote.request({"action": "step", "count": 60})
        expected = local.request({"action": "step", "count": 60})
        self.assertEqual(actual["cluster"]["worker_transport"], "http")
        self.assertEqual(actual["cluster"]["connected"], 7)
        for key in ("world", "frames", "timing", "performance", "project_hash"):
            self.assertEqual(actual[key], expected[key], key)
        for fd in (False, True):
            reply = remote.request({"action": "diagnostic", "ecu": "Motor ECU", "bytes": [34, 241, 144], "fd": fd})
            self.assertEqual(len(reply["bytes"]), 20)
            self.assertEqual(reply["bytes"][0], 98)
            self.assertTrue(all(frame["fd"] == fd for frame in reply["frames"]))
        project = remote.request({"action": "export_project"})
        self.assertNotIn("error", remote.request({"action": "import_project", "project": project}))
        self.assertEqual(remote.request({"action": "step"})["tick"], 1)

    def test_resource_jobs_match_between_native_process_and_http_workers(self):
        baseline = self.client(False)
        project = baseline.request({"action": "export_project"})
        radar = next(d for d in project["ecu_definitions"].values() if d["name"] == "Radar ECU")
        radar["hardware"].update(execution_profile="resource-v1", cpu_mhz=60,
            scheduler=dict(deadline_us=100000, watchdog_us=100000, queue_bytes=32768))
        for task in radar["tasks"]:
            task["cost_us"] = 10
        radar["tasks"][0]["cost_us"] = 7500 - 10 * (len(radar["tasks"]) - 1)
        project["project_hash"] = baseline.request({"action": "hash_project", "project": project})["project_hash"]
        path = Path(self.directory.name) / "resource.vcanproj"
        path.write_text(json.dumps(project))
        snapshots = []
        for remote in (False, True):
            args = ["--project", str(path)]
            if remote:
                args += ["--endpoints", str(self.mapping)]
            client = Client(*args)
            self.addCleanup(client.close)
            first = client.request({"action": "step"})
            self.assertNotIn("error", first)
            self.assertFalse(any(f["ecu"] == "Radar ECU" for f in first["frames"]))
            second = client.request({"action": "step"})
            self.assertNotIn("error", second)
            frames = [f for f in second["frames"] if f["ecu"] == "Radar ECU"]
            self.assertTrue(frames)
            self.assertTrue(all(f["computed_at_us"] == 15000 for f in frames))
            self.assertEqual(second["cluster"]["connected"], 7)
            snapshots.append(second)
        for key in ("world", "frames", "timing", "project_hash"):
            self.assertEqual(snapshots[0][key], snapshots[1][key], key)

    def test_partial_retry_and_worker_process_death(self):
        remote = self.client()
        remote.request({"action": "step", "count": 5})
        remote.request({"action": "node_fault", "name": "BMS ECU", "mode": "halt"})
        self.assertIn("error", remote.request({"action": "step"}))
        self.assertEqual(remote.request({"action": "snapshot"})["tick"], 5)
        remote.request({"action": "node_fault", "name": "BMS ECU", "mode": "restore"})
        self.assertEqual(remote.request({"action": "step"})["tick"], 6)
        self.workers[0].terminate()
        self.workers[0].wait(timeout=8)
        self.assertIn("error", remote.request({"action": "step"}))
        state = remote.request({"action": "snapshot"})
        self.assertEqual(state["tick"], 6)
        self.assertFalse(state["running"])
        self.assertIsNotNone(state["cluster"]["last_error"])

    def test_authentication_and_project_files(self):
        request = urllib.request.Request(self.endpoints[NAMES[0]] + "/command", b'{"action":"snapshot"}',
                                         headers={"Content-Type": "application/json"})
        with self.assertRaises(urllib.error.HTTPError) as error:
            urllib.request.urlopen(request, timeout=5)
        self.assertEqual(error.exception.code, 403)
        error.exception.close()
        project = Path(self.directory.name) / "sample.vcanproj"
        result = subprocess.run([CLI, "--steps", "12", "--save-project", str(project)],
                                capture_output=True, text=True, timeout=30, check=True)
        before = json.loads(result.stdout)
        self.assertEqual(before["tick"], 12)
        result = subprocess.run([CLI, "--project", str(project), "--steps", "12"],
                                capture_output=True, text=True, timeout=30, check=True)
        after = json.loads(result.stdout)
        self.assertEqual(before["project_hash"], after["project_hash"])
        self.assertEqual(before["world"], after["world"])
        invalid = subprocess.run([CLI, "--steps", "-1"], capture_output=True, text=True, timeout=5)
        self.assertNotEqual(invalid.returncode, 0)

    def test_failed_remote_import_preserves_project_and_requires_explicit_reset(self):
        remote = self.client()
        before = remote.request({"action": "step", "count": 5})
        project = remote.request({"action": "export_project"})
        corrupt = dict(project, project_hash="sha256:invalid")
        self.assertIn("error", remote.request({"action": "import_project", "project": corrupt}))
        self.assertEqual(remote.request({"action": "snapshot"}), before)
        old = self.workers[2]
        old.terminate()
        old.wait(timeout=8)
        self.assertIn("error", remote.request({"action": "import_project", "project": project}))
        failed = remote.request({"action": "snapshot"})
        for key in ("world", "tick", "epoch", "project_hash"):
            self.assertEqual(failed[key], before[key])
        self.assertTrue(failed["cluster"]["requires_reset"])
        self.assertIn("error", remote.request({"action": "step"}))
        port = self.endpoints["BMS ECU"].rsplit(":", 1)[1]
        replacement = subprocess.Popen([WORKER, "--http", port], stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.workers.append(replacement)
        self.assertTrue(select.select([replacement.stdout], [], [], 10)[0])
        self.assertEqual(json.loads(replacement.stdout.readline())["port"], int(port))
        reset = remote.request({"action": "reset"})
        self.assertFalse(reset["cluster"]["requires_reset"])
        self.assertEqual(remote.request({"action": "step"})["tick"], 1)

    def test_malformed_http_is_rejected_without_losing_worker_health(self):
        port = int(self.endpoints[NAMES[0]].rsplit(":", 1)[1])
        for headers in ("Content-Length: 2000001\r\n", "Content-Length: 0\r\nContent-Length: 0\r\n",
                        "Transfer-Encoding: chunked\r\n"):
            with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                connection.sendall(("POST /command HTTP/1.1\r\n" + headers + "\r\n").encode())
                response = connection.recv(8192)
                self.assertIn(b"400", response.split(b"\r\n")[0])
        with urllib.request.urlopen(self.endpoints[NAMES[0]] + "/health", timeout=5) as response:
            self.assertEqual(json.load(response)["status"], "ready")


if __name__ == "__main__":
    unittest.main(verbosity=2)
