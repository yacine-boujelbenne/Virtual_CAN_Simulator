"""Runs against Compose or app/run_local.py. Every tick reaches independent ECU kernels."""
import json,os,unittest,urllib.request,urllib.error
URL=os.environ.get("TEST_URL","http://127.0.0.1:8080")
def request(body):
    req=urllib.request.Request(URL+"/api/command",json.dumps(body).encode(),headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(req,timeout=90) as response:return json.load(response)
class Distributed(unittest.TestCase):
    def setUp(self):self.state=request({"action":"reset"})
    def test_worker_registry_and_can_fd_sensor_payload(self):
        self.assertEqual(self.state["cluster"]["mode"],"distributed")
        self.assertEqual(self.state["cluster"]["connected"],7)
        state=request({"action":"step","count":20})
        radar=[f for f in state["frames"] if f["name"]=="RadarObjectList"]
        self.assertTrue(radar);self.assertTrue(all(f["fd"] and f["brs"] and f["dlc"]==32 and f["wire_dlc"]==13 for f in radar))
        self.assertTrue(all(e["hardware"]["cpu_mhz"]>0 for e in state["ecus"]))
    def test_diagnostic_frames_reach_remote_worker_on_both_links(self):
        for fd in [False,True]:
            result=request({"action":"diagnostic","ecu":"Motor ECU","bytes":[34,241,144],"fd":fd})
            self.assertEqual(len(result["bytes"]),20);self.assertIn("distributed",result["transport"])
            self.assertEqual(result["ecu_container"],"Motor ECU")
            self.assertTrue(all(f["fd"]==fd for f in result["frames"]))
            self.assertGreater(result["frame_count"],2) if not fd else self.assertEqual(result["frame_count"],2)
    def test_halted_worker_does_not_advance_vehicle_and_can_resume(self):
        request({"action":"step","count":5});request({"action":"node_fault","name":"ABS ECU","mode":"halt"})
        before=request({"action":"snapshot"})["time"]
        with self.assertRaises(urllib.error.HTTPError):
            request({"action":"step","count":1})
        state=request({"action":"snapshot"})
        self.assertEqual(state["time"],before);self.assertIsNotNone(state["cluster"]["last_error"])
        request({"action":"node_fault","name":"ABS ECU","mode":"restore"})
        state=request({"action":"step","count":1})
        self.assertAlmostEqual(state["time"],before+.01)
    def test_policy_drops_radar_and_adas_detects_staleness(self):
        request({"action":"step","count":20})
        request({"action":"network_policy","rules":[{"bus":"adas","id":1024,"drop":True}]})
        state=request({"action":"step","count":30})
        adas=next(e for e in state["ecus"] if e["name"]=="ADAS ECU")
        self.assertEqual(adas["state"],"FAULT");self.assertGreater(state["dropped_frames"],0)
    def test_adding_model_uses_reserve_container_and_reset_releases_it(self):
        req=urllib.request.Request(URL+"/api/generate",json.dumps({"prompt":"Create an ABS ECU"}).encode(),headers={"Content-Type":"application/json"})
        with urllib.request.urlopen(req) as response:definition=json.load(response)["definition"]
        state=request({"action":"add_ecu","definition":definition})
        self.assertEqual(len(state["ecus"]),8);self.assertEqual(state["cluster"]["reserve_slots"],0)
        request({"action":"step","count":5})
        state=request({"action":"reset"})
        self.assertEqual(len(state["ecus"]),7);self.assertEqual(state["cluster"]["reserve_slots"],1)
if __name__=="__main__":unittest.main()
