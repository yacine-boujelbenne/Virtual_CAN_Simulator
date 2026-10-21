#!/usr/bin/env python3
"""Single-container HTTP adapter for the C++ simulation process (stdlib only)."""
import csv
import io
import json
import mimetypes
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent.parent
BINARY = os.environ.get("SIM_BINARY", str(ROOT / "build/vehicle_sim"))
SCENARIO = os.environ.get("SIM_SCENARIO", str(ROOT / "resources/scenarios/ev_basic.json"))
PORT = int(os.environ.get("PORT", "8080"))


from controller import Engine


def import_dbc(text):
    """Import a strict, documented subset: classic CAN / unsigned or signed Intel signals."""
    if len(text) > 500000:
        raise ValueError("DBC exceeds 500 KB")
    messages, current = [], None
    message_pattern = re.compile(r"^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+\w+\s*$")
    signal_pattern = re.compile(
        r'^\s*SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*'
        r'\(([^,]+),([^\)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)"\s+.*$')
    for line in text.splitlines():
        if line.strip().startswith("BO_ "):
            match = message_pattern.match(line)
            if not match:
                raise ValueError("Unsupported DBC message declaration")
            identifier, name, dlc = match.groups()
            identifier = int(identifier)
            extended = bool(identifier & 0x80000000)
            current = dict(name=name, id=identifier & 0x1fffffff, extended=extended,
                           dlc=int(dlc), direction="tx", period_ms=100, signals=[])
            if not 1 <= current["dlc"] <= 8:
                raise ValueError("DBC importer currently supports classic CAN; import JSON for CAN FD")
            messages.append(current)
        elif line.strip().startswith("SG_ "):
            match = signal_pattern.match(line)
            if not current or not match:
                raise ValueError("Multiplexed or malformed DBC signal is unsupported")
            name, start, length, order, sign, factor, offset, low, high, unit = match.groups()
            if order != "1":
                raise ValueError("Motorola signals are unsupported; convert to Intel little endian")
            current["signals"].append(dict(name=name, start=int(start), length=int(length),
                signed=sign == "-", byte_order="little_endian", factor=float(factor),
                offset=float(offset), min=float(low), max=float(high), unit=unit))
        elif line.strip().startswith(("SIG_VALTYPE_", "SG_MUL_VAL_")):
            raise ValueError("Floating-point/multiplexed DBC extensions are unsupported")
    if not messages:
        raise ValueError("No DBC messages found")
    names = [s["name"] for m in messages for s in m["signals"]]
    if len(set(names)) != len(names):
        raise ValueError("Duplicate signal names across messages: rename them before import")
    return dict(schema_version=1, name="DBC Virtual ECU", inputs=["ignition"],
        variables={s["name"]: max(s["min"], min(s["max"], 0)) for m in messages for s in m["signals"]},
        tasks=[], bindings={}, messages=messages,
        state_machine=dict(initial="OFF", states=["OFF", "RUN"], transitions=[
            {"from": "OFF", "to": "RUN", "when": "ignition"},
            {"from": "RUN", "to": "OFF", "when": {"op": "eq", "args": ["ignition", 0]}}]),
        dtcs=[], assumptions=["DBC imports wire layouts only. Edit tasks to define behavior."])


def build_definition(body, engine):
    prompt = str(body.get("prompt", "")).strip()
    if not prompt or len(prompt) > 4000:
        raise ValueError("Prompt must contain 1..4000 characters")
    templates = {p.stem: json.loads(p.read_text()) for p in (ROOT / "resources/ecu_templates").glob("*.json")}
    endpoint = os.environ.get("AI_ENDPOINT", "")
    if endpoint:
        # Provider adapter contract; endpoint must return {"definition": {...}}.
        payload = json.dumps({"prompt": prompt, "templates": templates,
                              "schema_version": 1}).encode()
        headers = {"Content-Type": "application/json"}
        if os.environ.get("AI_API_KEY"):
            headers["Authorization"] = "Bearer " + os.environ["AI_API_KEY"]
        req = urllib.request.Request(endpoint, payload, headers=headers)
        with urllib.request.urlopen(req, timeout=45) as response:
            data = response.read(2000001)
        if len(data) > 2000000:
            raise ValueError("AI response exceeds 2 MB")
        definition = json.loads(data)["definition"]
        provider = "configured AI endpoint"
    else:
        lower = prompt.lower()
        key = next((key for words, key in [
            (("abs", "brake"), "abs"), (("battery", "bms"), "bms"), (("cluster", "dashboard"), "cluster"),
            (("speed", "dynamics"), "dynamics"), (("motor", "engine"), "motor")]
                    if any(word in lower for word in words)), None)
        if key is None:
            raise ValueError("Offline builder supports motor, ABS, BMS, dynamics, and cluster templates")
        definition = templates[key]
        definition["name"] += " Preview"
        definition["bindings"] = {}
        definition["assumptions"].append("Offline template selection; no language model was called. Actuator bindings removed for an observation-only preview.")
        # Choose unused CAN IDs; preserve RX dependencies on the existing scenario.
        used = {m["id"] for e in engine.request({"action": "snapshot"})["ecus"]
                for m in e["definition"]["messages"] if m["direction"] == "tx"}
        next_id = 1024
        for message in definition["messages"]:
            if message["direction"] == "tx":
                while next_id in used:
                    next_id += 1
                message["id"] = next_id
                used.add(next_id)
        provider = "offline template builder"
    engine.request({"action": "validate", "definition": definition})
    return {"definition": definition, "provider": provider, "validated": True}


class Handler(BaseHTTPRequestHandler):
    server_version = "VehicleLab/1.0"

    def log_message(self, format, *args):
        pass

    def send(self, status, body, content_type="application/json"):
        data = json.dumps(body, allow_nan=False).encode() if content_type == "application/json" else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        try:
            path = self.path.split("?", 1)[0]
            if path == "/health":
                self.server.engine.request({"action": "cluster_ready"})
                return self.send(200, {"status": "ok"})
            if path == "/api/state":
                return self.send(200, self.server.engine.request({"action": "snapshot"}))
            if path == "/api/templates":
                return self.send(200, {p.stem: json.loads(p.read_text()) for p in (ROOT / "resources/ecu_templates").glob("*.json")})
            if path == "/api/project":
                return self.send(200, self.server.engine.request({"action": "export_project"}))
            if path == "/api/export":
                state = self.server.engine.request({"action": "snapshot"})
                out = io.StringIO()
                writer = csv.writer(out)
                writer.writerow(["time_s", "can_id", "ecu", "message", "dlc", "bytes", "signals_json"])
                for frame in state["frames"]:
                    writer.writerow([frame["time"], hex(frame["id"]), frame["ecu"], frame["name"],
                                     frame["dlc"], frame["bytes"], json.dumps(frame["signals"])])
                return self.send(200, out.getvalue().encode(), "text/csv; charset=utf-8")
            files = {"/": "index.html", "/app.js": "app.js", "/style.css": "style.css"}
            if path not in files:
                return self.send(404, {"error": "Not found"})
            file = ROOT / "web" / files[path]
            self.send(200, file.read_bytes(), mimetypes.guess_type(file)[0] or "application/octet-stream")
        except Exception as error:
            self.send(503, {"error": str(error)})

    def do_POST(self):
        try:
            self.connection.settimeout(60)
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 2000000:
                return self.send(413, {"error": "Request must be 1 byte..2 MB"})
            if self.headers.get_content_type() != "application/json":
                return self.send(415, {"error": "Use application/json"})
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Request must be a JSON object")
            if self.path == "/api/command":
                # Clock advancement is owned by the server.
                if body.get("action") in ("advance","prepare_tick","complete_tick","refresh_ecu","diagnostic_trace","cluster_ready"):
                    raise ValueError("Use step, run, or pause")
                result = self.server.engine.request(body)
            elif self.path == "/api/dbc":
                result = {"definition": import_dbc(body["text"])}
                self.server.engine.request({"action": "validate", **result})
            elif self.path == "/api/project":
                if "project" not in body:
                    raise ValueError("Project required")
                result = self.server.engine.request({"action": "import_project", "project": body["project"]})
            elif self.path == "/api/generate":
                result = build_definition(body, self.server.engine)
            else:
                return self.send(404, {"error": "Not found"})
            self.send(200, result)
        except (ValueError, KeyError, TypeError, OverflowError) as error:
            self.send(400, {"error": str(error)})
        except Exception as error:
            self.send(503, {"error": str(error)})


def main():
    engine = Engine()
    server = ThreadingHTTPServer((os.environ.get("HOST", "0.0.0.0"), PORT), Handler)
    server.engine = engine
    # Shutdown outside the serving thread to avoid deadlocking HTTPServer.shutdown().
    def stop(*_):
        threading.Thread(target=server.shutdown, daemon=True).start()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print(f"Virtual Vehicle Lab at http://localhost:{PORT}", flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()
        engine.close()


if __name__ == "__main__":
    main()
