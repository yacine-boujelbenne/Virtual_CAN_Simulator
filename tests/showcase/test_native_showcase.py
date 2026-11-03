"""Exercise the real optional HTTP edge and native controller/worker lifecycle."""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get('VCLAB_RUNTIME', ROOT / 'build/native/vclab')).resolve()


class NativeShowcase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = subprocess.Popen([sys.executable, str(ROOT / 'showcase/server.py'), '--runtime', str(RUNTIME), '--port', '0'],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if not select.select([cls.server.stdout], [], [], 15)[0]:
            cls.server.terminate()
            raise RuntimeError('Showcase startup timeout')
        line = cls.server.stdout.readline()
        if not line:
            raise RuntimeError(cls.server.stderr.read())
        cls.ready = json.loads(line)
        cls.base = cls.ready['url']
        initial = cls.request('/api/state')
        cls.worker_pids = [e['container']['kernel_pid'] for e in initial['ecus']]

    @classmethod
    def tearDownClass(cls):
        cls.server.send_signal(signal.SIGTERM)
        cls.server.wait(timeout=12)
        stderr = cls.server.stderr.read()
        cls.server.stdout.close()
        cls.server.stderr.close()
        assert cls.server.returncode == 0, stderr
        assert not Path(f"/proc/{cls.ready['controller_pid']}").exists()
        assert all(not Path(f'/proc/{p}').exists() for p in cls.worker_pids)

    @classmethod
    def request(cls, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(cls.base + path, data=data, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=35) as response:
            return json.load(response)

    def command(self, action, **extra):
        return self.request('/api/command', {'action': action, **extra})

    def setUp(self):
        self.command('pause')
        self.command('reset')

    def test_native_authority_health_assets_and_mesh(self):
        self.assertEqual(self.request('/health')['status'], 'ok')
        self.assertEqual(self.request('/api/capabilities')['clock_owner'], 'vclab --realtime')
        state = self.request('/api/state')
        self.assertEqual(state['cluster']['controller'], 'native-cpp')
        self.assertEqual(state['cluster']['connected'], 9)
        self.assertEqual(len(set(self.worker_pids)), 9)
        mesh = self.request('/api/car-mesh')
        self.assertGreater(len(mesh['triangles']), 100)
        self.assertEqual(len(mesh['triangles'][0]), 17)
        for path in ['/', '/app.js', '/style.css', '/native.js', '/native.css']:
            with urllib.request.urlopen(self.base + path) as response:
                self.assertEqual(response.status, 200)
                self.assertGreater(len(response.read()), 100)

    def test_only_cpp_clock_advances_and_pause_is_stable(self):
        before = self.request('/api/state')['tick']
        time.sleep(.12)
        self.assertEqual(self.request('/api/state')['tick'], before)
        self.command('run')
        time.sleep(.3)  # No browser/HTTP polling is needed to advance the native clock.
        stopped = self.command('pause')
        self.assertGreater(stopped['tick'], before)
        time.sleep(.12)
        self.assertEqual(self.request('/api/state')['tick'], stopped['tick'])
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.command('advance', count=10)
        self.assertEqual(error.exception.code, 400)
        error.exception.close()

    def test_traffic_edits_and_following_reach_can(self):
        self.command('traffic_actor', operation='add', id='other', actor={'x':40,'y':4,'speed':10,'target_speed':10})
        state = self.command('traffic_actor', operation='update', id='other', actor={'target_speed':5})
        self.assertEqual(state['traffic']['lead_id'], 'lead')
        self.assertEqual(len(state['traffic']['actors']), 2)
        self.command('traffic_actor', operation='remove', id='other')
        self.command('controls', values={'following_time_s':2.3,'following_gap_m':8,'following_gain':.6,'target_speed':72})
        fractional = False
        for _ in range(12):
            state = self.command('step', count=100)
            self.assertEqual(state['world'].get('collision_events', 0), 0)
            fractional |= any(f['id'] == 1104 and 0 < f['signals']['aeb_request'] < .99 for f in state['frames'])
        self.assertTrue(fractional)
        self.assertLess(state['world']['speed'], 15)
        self.assertEqual(state['controls']['following_time_s'], 2.3)

    def test_project_switch_hash_and_custom_presentation_roundtrip(self):
        project = self.request('/api/project')
        state = self.command('new_project', scenario='ev_lateral.json')
        self.assertNotIn('traffic', state)
        self.request('/api/project', {'project':project})
        restored = self.request('/api/state')
        self.assertEqual(restored['project_hash'], project['project_hash'])
        self.assertEqual(restored['tick'], 0)
        mesh = self.request('/api/car-mesh')
        self.command('set_car_mesh', mesh=mesh)
        saved = self.request('/api/project')
        self.request('/api/project', {'project':saved})
        self.assertEqual(self.request('/api/car-mesh'), mesh)
        self.command('set_car_mesh', mesh=None)

    def test_stress_diagnostics_and_invalid_command_atomicity(self):
        state = self.command('environment', values={'motor_temperature':120,'soc':10})
        self.assertEqual(state['world']['motor_temperature'], 120)
        state = self.command('step', count=20)
        motor = next(e for e in state['ecus'] if e['name'] == 'Motor ECU')
        self.assertEqual(motor['state'], 'FAULT')
        before = self.request('/api/state')
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.command('controls', values={'following_time_s':9})
        error.exception.close()
        self.assertEqual(self.request('/api/state'), before)
        for fd in [False, True]:
            vin = self.command('diagnostic', ecu='Motor ECU', bytes=[34,241,144], fd=fd)
            self.assertEqual(vin['bytes'][:3], [98,241,144])
            self.assertGreater(vin['frame_count'], 1)
        self.assertEqual(self.request('/api/state')['tick'], before['tick'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
