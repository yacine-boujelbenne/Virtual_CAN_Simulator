"""Central control plane: fixed-time barriers, virtual-bus policy, and ECU registry."""
import json,os,threading,time,uuid,urllib.request,urllib.error
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from process import KernelProcess

ROOT=Path(__file__).resolve().parent.parent
TOKEN=os.environ.get("CLUSTER_TOKEN","local-development-token")

class Engine:
    def __init__(self):
        self.lock=threading.RLock();self.closed=threading.Event();self.failure=None
        self.last_probe=0.0
        self.registry=json.loads(os.environ.get("ECU_ENDPOINTS","{}"))
        self.spares=json.loads(os.environ.get("ECU_SPARES","[]"))
        self.distributed=bool(self.registry);self.pool=ThreadPoolExecutor(max_workers=16)
        if self.distributed:os.environ["SIM_DISTRIBUTED"]="1"
        binary=os.environ.get("SIM_BINARY",str(ROOT/"build/vehicle_sim"))
        scenario=os.environ.get("SIM_SCENARIO",str(ROOT/"resources/scenarios/ev_basic.json"))
        self.core=KernelProcess([binary,scenario],ROOT);self.nodes={};self.configured=False
        self.core.request({"action":"snapshot"})
        self.thread=threading.Thread(target=self._clock,daemon=True);self.thread.start()
    def _rpc(self,endpoint,body):
        payload=json.dumps(body,allow_nan=False).encode()
        req=urllib.request.Request(endpoint+"/command",payload,headers={"Content-Type":"application/json","X-Cluster-Token":TOKEN})
        try:
            with urllib.request.urlopen(req,timeout=3) as response:result=json.load(response)
        except urllib.error.HTTPError as error:
            message=json.loads(error.read()).get("error",str(error))
            raise RuntimeError(message) from error
        if "error" in result:raise RuntimeError(result["error"])
        return result
    def _configure(self):
        state=self.core.request({"action":"snapshot"});jobs=[]
        for ecu in state["ecus"]:
            name=ecu["name"]
            if name not in self.registry:raise RuntimeError("No dedicated ECU endpoint for "+name)
            jobs.append((name,self.pool.submit(self._rpc,self.registry[name],{"action":"configure","definition":ecu["definition"],"step_ms":state["step_ms"],"epoch":state["epoch"]})))
        for name,job in jobs:
            try:
                result=job.result(timeout=5);self.nodes[name]={"status":"connected","service":result.get("runtime",{}).get("service",self.registry[name].split("//")[-1].split(":")[0]),"kernel_pid":result.get("runtime",{}).get("kernel_pid"),"last_error":None}
            except Exception as error:
                self.nodes[name]={"status":"unavailable","service":name,"last_error":str(error)};raise
        self.configured=True
    def _snapshot(self):
        state=self.core.request({"action":"snapshot"})
        if self.distributed:
            for ecu in state["ecus"]:ecu["container"]=self.nodes.get(ecu["name"],{"status":"starting","service":ecu["name"]})
        state["cluster"]={"mode":"distributed" if self.distributed else "embedded","controller":"simulator",
            "connected":sum(n["status"]=="connected" for n in self.nodes.values()) if self.distributed else 0,
            "required":len(state["ecus"]) if self.distributed else 0,"reserve_slots":len(self.spares),
            "last_error":self.failure,"image":"virtual-ecu-kernel"}
        return state
    def _step(self,count):
        if type(count) is not int or not 1<=count<=1000:raise ValueError("Step count must be 1..1000")
        if not self.distributed:return self.core.request({"action":"step","count":count})
        if not self.configured:self._configure()
        for _ in range(count):
            prepared=self.core.request({"action":"prepare_tick"});jobs=[]
            for node in prepared["nodes"]:
                body={**node,"action":"tick"}
                jobs.append((node["name"],self.pool.submit(self._rpc,self.registry[node["name"]],body)))
            results=[];errors=[]
            for name,job in jobs:
                try:
                    result=job.result(timeout=5);results.append(result)
                    self.nodes[name].update(status="connected",last_error=None)
                except Exception as error:
                    self.nodes[name].update(status="fault",last_error=str(error));errors.append(name+": "+str(error))
            if errors:
                self.failure="; ".join(errors);self.core.request({"action":"pause"})
                raise RuntimeError("Tick paused; "+self.failure)
            self.core.request({"action":"complete_tick","results":results});self.failure=None
        return self._snapshot()
    def _diagnostic(self,body):
        name=body["ecu"]
        if name not in self.registry:raise ValueError("Unknown diagnostic ECU")
        if not self.configured:self._configure()
        payload=body["bytes"]
        if not isinstance(payload,list) or any(type(b) is not int or not 0<=b<=255 for b in payload):
            raise ValueError("Diagnostic bytes must be integers 0..255")
        fd=bool(body.get("fd",False));length=body.get("link_length",64 if fd else 8)
        block=body.get("block_size",8);stmin=body.get("stmin",0)
        session=uuid.uuid4().hex;endpoint=self.registry[name]
        self._rpc(endpoint,{"action":"isotp_start","session":session,"response_id":0x7e8,"request_id":0x7e0,
            "fd":fd,"link_length":length,"block_size":block,"stmin":stmin})
        # The tester session lives in the C++ core; this controller only carries frames.
        self.core.request({"action":"isotp_tester_start","session":session,"tx_id":0x7e0,"rx_id":0x7e8,
            "fd":fd,"link_length":length,"block_size":block,"stmin":stmin})
        queue=[(True,f) for f in self.core.request({"action":"isotp_tester_send","session":session,"bytes":payload})["frames"]]
        trace=[];start=time.monotonic();message=None
        try:
            while message is None:
                if not queue or time.monotonic()-start>10:raise RuntimeError("ISO-TP response timeout")
                to_worker,frame=queue.pop(0)
                delay=frame.get("delay_us",0)/1e6
                if delay:time.sleep(delay)
                record=self.core.request({"action":"diagnostic_trace","source":"Diagnostic tester" if to_worker else name,"frame":frame})
                if record.get("dropped"):raise RuntimeError("Diagnostic frame blocked by bus policy")
                trace.append({**frame,"source":"Diagnostic tester" if to_worker else name})
                if to_worker:
                    response=self._rpc(endpoint,{"action":"isotp_frame","session":session,"frame":frame})
                    queue.extend((False,f) for f in response["frames"])
                else:
                    result=self.core.request({"action":"isotp_tester_receive","session":session,"frame":frame})
                    queue.extend((True,f) for f in result["frames"])
                    if result.get("done"):message=result["message"]
                if len(trace)>2048:raise RuntimeError("ISO-TP frame limit")
            result=self._rpc(endpoint,{"action":"snapshot"})
            # Refresh mirrored diagnostic memory without executing an ECU task or changing time.
            self.core.request({"action":"refresh_ecu","name":name,"snapshot":result})
            return {"request":" ".join(f"{b:02x}" for b in payload),"response":" ".join(f"{b:02x}" for b in message),
                "bytes":message,"transport":"ISO-TP / distributed "+("CAN FD" if fd else "CAN"),
                "request_id":0x7e0,"response_id":0x7e8,"frames":trace,"frame_count":len(trace),"ecu_container":name}
        finally:
            try:self._rpc(endpoint,{"action":"isotp_end","session":session})
            except Exception:pass
            try:self.core.request({"action":"isotp_tester_end","session":session})
            except Exception:pass
    def request(self,command):
        with self.lock:
            action=command.get("action","snapshot")
            if action=="snapshot":return self._snapshot()
            if action=="export_project":return self.core.request({"action":"export_project"})
            if action=="import_project":return self._import_project(command)
            if action=="cluster_ready":
                if self.distributed and not self.configured:self._configure()
                return self._snapshot()
            if action=="reset":
                state=self.core.request(command);self.failure=None;self.configured=False
                for name in list(self.registry):
                    if name not in {e["name"] for e in state["ecus"]}:
                        self.spares.append(self.registry.pop(name));self.nodes.pop(name,None)
                if self.distributed:self._configure()
                return self._snapshot()
            if action=="step":return self._step(command.get("count",1))
            if action=="advance":
                state=self.core.request({"action":"snapshot"})
                return self._step(command.get("count",10)) if state["running"] else state
            if action=="run":self.failure=None
            if action=="diagnostic" and self.distributed:return self._diagnostic(command)
            if action=="node_fault":
                name=command["name"]
                if name not in self.registry:raise ValueError("Unknown ECU")
                result=self._rpc(self.registry[name],{"action":"fault","mode":command["mode"]})
                self.nodes[name].update(status="fault" if result["halted"] else "connected",last_error="Kernel halted" if result["halted"] else None)
                if result["halted"]:self.core.request({"action":"pause"})
                return self._snapshot()
            if action=="add_ecu" and self.distributed:
                if not self.spares:raise ValueError("No reserve ECU container available; add a worker in Compose")
                definition=command["definition"];self.core.request({"action":"validate","definition":definition})
                endpoint=self.spares[0];state=self.core.request({"action":"snapshot"})
                self._rpc(endpoint,{"action":"configure","definition":definition,"step_ms":state["step_ms"],"epoch":state["epoch"],"start_tick":state["tick"]})
                result=self.core.request(command)
                self.registry[definition["name"]]=endpoint;self.spares.pop(0)
                self.nodes[definition["name"]]={"status":"connected","service":endpoint.split("//")[-1].split(":")[0],"last_error":None}
                return self._snapshot()
            self.core.request(command);return self._snapshot() if action not in ("validate",) else {"valid":True}
    def _import_project(self,command):
        project=command.get("project")
        if not isinstance(project,dict):raise ValueError("Project must be a JSON object")
        definitions=project.get("ecu_definitions")
        if not isinstance(definitions,dict):raise ValueError("Project ECU definitions missing")
        names=[definition.get("name") for definition in definitions.values() if isinstance(definition,dict)]
        # The registry is checked before the import so a missing endpoint never breaks the laboratory.
        if self.distributed:
            for name in names:
                if name not in self.registry:raise ValueError("No dedicated ECU endpoint for "+str(name))
        result=self.core.request({"action":"import_project","project":project})
        self.failure=None
        if self.distributed:
            keep=set(names)
            for name in list(self.registry):
                if name not in keep:self.spares.append(self.registry.pop(name));self.nodes.pop(name,None)
            self.configured=False;self._configure()
        return result
    def _probe(self):
        if not self.distributed or not self.configured or time.monotonic()-self.last_probe<1:return
        self.last_probe=time.monotonic()
        def health(name,endpoint):
            try:
                with urllib.request.urlopen(endpoint+"/health",timeout=1) as response:status=json.load(response)
                return name,"fault" if status.get("halted") else "connected",None
            except Exception as error:return name,"unavailable",str(error)
        jobs=[self.pool.submit(health,name,endpoint) for name,endpoint in self.registry.items()]
        for job in jobs:
            name,status,error=job.result(timeout=2)
            if name in self.nodes:self.nodes[name].update(status=status,last_error=error)
    def _clock(self):
        while not self.closed.wait(.1):
            try:
                self.request({"action":"advance","count":10})
                with self.lock:self._probe()
            except Exception as error:self.failure=str(error)
    def close(self):
        self.closed.set();self.thread.join(timeout=6);self.pool.shutdown(wait=True,cancel_futures=True);self.core.close()
