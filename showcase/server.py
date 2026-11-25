#!/usr/bin/env python3
"""Optional HTTP/static edge for the native C++ controller; owns no simulation clock."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import threading
from http.server import ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'app'))
from server import Handler as CompatibilityHandler


class NativeEngine:
    def __init__(self, runtime, scenario):
        self.lock = threading.Lock()
        self.process = subprocess.Popen([str(runtime), '--scenario', str(scenario), '--realtime'],
            cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        try:
            self.request({'action': 'snapshot'})
        except BaseException:
            self.close()
            raise

    def request(self, command):
        data = json.dumps(command, allow_nan=False)
        if len(data.encode()) > 16 * 1024 * 1024:
            raise ValueError('Command exceeds native 16 MiB project limit')
        with self.lock:
            if self.process.poll() is not None:
                raise RuntimeError('Native controller exited; restart the showcase')
            self.process.stdin.write(data + '\n')
            self.process.stdin.flush()
            if not select.select([self.process.stdout], [], [], 30)[0]:
                self.process.kill()
                raise RuntimeError('Native controller response timed out')
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError('Native controller closed its output')
            result = json.loads(line)
            if 'error' in result:
                raise ValueError(result['error'])
            return result

    def close(self):
        with self.lock:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()


def default_mesh():
    # Generated with the same C++ CarMesh importer as the Qt desktop.
    return json.loads((ROOT / 'resources/models/default-car/sedan.preview.json').read_text())


class Handler(CompatibilityHandler):
    server_version = 'NativeVehicleShowcase/1.0'
    max_body_bytes = 16 * 1024 * 1024

    def do_GET(self):
        path = self.path.split('?', 1)[0]
        if path == '/native.js':
            return self.send(200, (ROOT / 'showcase/native.js').read_bytes(), 'text/javascript; charset=utf-8')
        if path == '/native.css':
            return self.send(200, (ROOT / 'showcase/native.css').read_bytes(), 'text/css; charset=utf-8')
        if path == '/api/capabilities':
            return self.send(200, {'engine': 'native-cpp', 'clock_owner': 'vclab --realtime',
                'client': 'optional-web-showcase', 'primary_product': 'Qt desktop', 'plant': 'planar-grip',
                'controller_pid': self.server.engine.process.pid})
        if path == '/api/car-mesh':
            try:
                project = self.server.engine.request({'action': 'export_project'})
                return self.send(200, project.get('presentation', {}).get('car_mesh') or self.server.default_mesh)
            except Exception as error:
                return self.send(503, {'error': str(error)})
        super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=Path(os.environ.get('VCLAB_RUNTIME', str(ROOT / 'build/native/vclab'))))
    parser.add_argument('--scenario', type=Path, default=ROOT / 'resources/scenarios/ev_following_demo.json')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not args.runtime.is_file() or not os.access(args.runtime, os.X_OK):
        parser.error('Build vclab/vclab_worker or pass --runtime /path/to/vclab')
    engine = NativeEngine(args.runtime.resolve(), args.scenario.resolve())
    try:
        server = ThreadingHTTPServer((args.host, args.port), Handler)
        server.engine = engine
        server.default_mesh = default_mesh()
        def stop(*_):
            threading.Thread(target=server.shutdown, daemon=True).start()
        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
        print(json.dumps({'url': f'http://{args.host}:{server.server_port}', 'engine': 'native-cpp', 'controller_pid': engine.process.pid}), flush=True)
        try:
            server.serve_forever()
        finally:
            server.server_close()
    finally:
        engine.close()


if __name__ == '__main__':
    main()
