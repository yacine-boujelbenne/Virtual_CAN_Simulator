#!/usr/bin/env python3
"""A container hosts exactly one configurable C++ ECU kernel."""
import json,os,signal,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from process import KernelProcess

ROOT=Path(__file__).resolve().parent.parent
BINARY=os.environ.get("ECU_BINARY",str(ROOT/"build/ecu_kernel"))
TOKEN=os.environ.get("CLUSTER_TOKEN","local-development-token")

class Worker:
    def __init__(self):
        self.kernel=KernelProcess([BINARY],ROOT)
        self.lock=threading.Lock();self.configured=False;self.halted=False;self.sessions={}
    def request(self,body):
        with self.lock:
            action=body.get("action","snapshot")
            if action=="fault":
                mode=body.get("mode")
                if mode not in ("halt","restore"):raise ValueError("Unknown ECU fault")
                self.halted=mode=="halt";return {"halted":self.halted}
            if action=="configure":
                result=self.kernel.request(body);self.configured=True;self.halted=False;self.sessions.clear();return result
            if self.halted:raise RuntimeError("ECU watchdog: kernel halted")
            if action=="isotp_start":
                session=str(body["session"])
                if len(self.sessions)>=32 and session not in self.sessions:raise ValueError("ISO-TP session limit")
                # The ISO-TP state machine lives in the C++ kernel; this worker only carries frames.
                self.kernel.request({"action":"isotp_start","session":session,
                    "tx_id":body.get("response_id",0x7e8),"rx_id":body.get("request_id",0x7e0),
                    "fd":body.get("fd",False),"link_length":body.get("link_length",8),
                    "block_size":body.get("block_size",8),"stmin":body.get("stmin",0),
                    "extended":body.get("extended",False),"sa":body.get("sa",0),"ta":body.get("ta",0),
                    "respond":True})
                self.sessions[session]=time.monotonic()
                return {"ready":True}
            if action=="isotp_frame":
                session=str(body["session"])
                if session not in self.sessions:raise ValueError("Unknown ISO-TP session")
                if time.monotonic()-self.sessions[session]>10:self.sessions.pop(session);raise ValueError("ISO-TP session timeout")
                return self.kernel.request({"action":"isotp_frame","session":session,"frame":body["frame"]})
            if action=="isotp_end":
                session=str(body["session"]);self.sessions.pop(session,None)
                try:self.kernel.request({"action":"isotp_end","session":session})
                except Exception:pass
                return {"closed":True}
            if action=="reset":self.sessions.clear()
            return self.kernel.request(body)

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def send(self,status,data):
        payload=json.dumps(data,allow_nan=False).encode();self.send_response(status)
        self.send_header("Content-Type","application/json");self.send_header("Content-Length",str(len(payload)));self.end_headers();self.wfile.write(payload)
    def do_GET(self):
        if self.path!="/health":return self.send(404,{"error":"Not found"})
        alive=self.server.worker.kernel.process.poll() is None
        return self.send(200 if alive else 503,{"status":"ready" if alive else "failed","configured":self.server.worker.configured,"halted":self.server.worker.halted,"pid":os.getpid()})
    def do_POST(self):
        try:
            if self.path!="/command":return self.send(404,{"error":"Not found"})
            if self.headers.get("X-Cluster-Token")!=TOKEN:return self.send(403,{"error":"Cluster token mismatch"})
            length=int(self.headers.get("Content-Length","0"))
            if not 0<length<=2000000:return self.send(413,{"error":"Request size limit"})
            body=json.loads(self.rfile.read(length))
            result=self.server.worker.request(body)
            result["runtime"]={"kernel_pid":self.server.worker.kernel.process.pid,"service":os.environ.get("ECU_SERVICE","ecu-kernel")}
            self.send(200,result)
        except (ValueError,KeyError,TypeError) as error:self.send(400,{"error":str(error)})
        except Exception as error:self.send(503,{"error":str(error)})

def main():
    worker=Worker();server=ThreadingHTTPServer((os.environ.get("HOST","0.0.0.0"),int(os.environ.get("PORT","8081"))),Handler);server.worker=worker
    def stop(*_):threading.Thread(target=server.shutdown,daemon=True).start()
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:server.serve_forever()
    finally:server.server_close();worker.kernel.close()
if __name__=="__main__":main()
