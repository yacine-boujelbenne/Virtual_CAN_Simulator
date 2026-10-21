"""ISO-TP conformance vectors: the C++ engine and the Python oracle must agree.

Every vector in tests/vectors/isotp is replayed twice: once through the C++ session
in build/ecu_kernel, once through the oracle in app/transport.py. Both must produce
the frame sequences, messages and failures written in the vector.
"""
import json,os,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/"app"))
from transport import IsoTp
VECTORS=ROOT/"tests/vectors/isotp"
BINARY=ROOT/"build/ecu_kernel"
SHARED=("fd","link_length","block_size","stmin","extended","n_as","n_bs","n_ar","n_cr")

def fromhex(text):return [int(text[i:i+2],16) for i in range(0,len(text),2)]
def tohex(data):return "".join(f"{b:02x}" for b in data)

def endpoint_config(config,swapped):
    options={key:config[key] for key in SHARED if key in config}
    if swapped:
        return {"tx_id":config["rx_id"],"rx_id":config["tx_id"],
                "sa":config.get("ta",0),"ta":config.get("sa",0),**options}
    return {"tx_id":config["tx_id"],"rx_id":config["rx_id"],
            "sa":config.get("sa",0),"ta":config.get("ta",0),**options}

class Clock:
    def __init__(self):self.now=0
    def __call__(self):return self.now

class OracleEndpoint:
    def __init__(self,config,clock):
        self.session=IsoTp(clock=clock,**config)
        self.peer=config["rx_id"];self.last=None
    def send(self,payload,now):return self.session.send(payload)
    def receive(self,frame,now):
        frames=self.session.receive(frame)
        if self.session.message is not None:
            self.last=list(self.session.message);self.session.message=None
        return frames
    def poll(self,now):self.session.poll()

class Kernel:
    def __init__(self):
        self.process=subprocess.Popen([str(BINARY)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
            text=True,bufsize=1,cwd=str(ROOT))
        definition=json.loads((ROOT/"resources/ecu_templates/radar.json").read_text())
        self.request({"action":"configure","definition":definition,"step_ms":10,"epoch":1})
        self.count=0
    def request(self,body):
        self.process.stdin.write(json.dumps(body,allow_nan=False)+"\n");self.process.stdin.flush()
        line=self.process.stdout.readline()
        if not line:raise RuntimeError("C++ kernel closed its output")
        result=json.loads(line)
        if "error" in result:raise ValueError(result["error"])
        return result
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:self.process.kill()

class CppEndpoint:
    def __init__(self,kernel,config):
        self.kernel=kernel;self.count=kernel.count;kernel.count+=1
        self.session=f"vector-{os.getpid()}-{self.count}";self.peer=config["rx_id"];self.last=None
        kernel.request({"action":"isotp_start","session":self.session,"respond":False,**config})
    def send(self,payload,now):
        return self.kernel.request({"action":"isotp_send","session":self.session,"bytes":payload,
                                    "now_ms":now})["frames"]
    def receive(self,frame,now):
        result=self.kernel.request({"action":"isotp_frame","session":self.session,"frame":frame,
                                    "now_ms":now})
        if result["message"] is not None:self.last=list(result["message"])
        return result["frames"]
    def poll(self,now):
        self.kernel.request({"action":"isotp_poll","session":self.session,"now_ms":now})

def play(make_endpoint,config,steps,clock):
    """Replays a vector and returns what each step produced."""
    endpoints={"a":make_endpoint(config,False),"b":make_endpoint(config,True)}
    trace=[]
    for index,step in enumerate(steps):
        clock.now=step.get("at",clock.now)
        actor=endpoints[step["actor"]]
        record={"frames":[],"message":None,"error":None}
        try:
            if step["action"]=="send":
                for frame in actor.send(fromhex(step["payload"]),clock.now):
                    record["frames"].append({"data":tohex(frame["data"]),"delay_us":frame.get("delay_us",0)})
            elif step["action"]=="receive":
                frame={"id":actor.peer,"data":fromhex(step["frame"]),
                       "fd":config.get("fd",False),"brs":config.get("fd",False),
                       "extended":actor.peer>0x7ff}
                for produced in actor.receive(frame,clock.now):
                    record["frames"].append({"data":tohex(produced["data"]),
                                             "delay_us":produced.get("delay_us",0)})
            elif step["action"]=="poll":
                actor.poll(clock.now)
            elif step["action"]=="message":
                record["message"]=actor.last
            else:raise AssertionError("Unknown vector step: "+step["action"])
        except Exception as error:
            record["error"]=str(error)
        record["expected"]=step.get("frames");record["expect_message"]=step.get("value")
        record["expect_error"]=step.get("error");record["index"]=index
        trace.append(record)
    return trace

def check(trace,implementation):
    """Asserts one replay against the vector expectations."""
    for record in trace:
        where=f"{implementation} step {record['index']}"
        if record["expect_error"] is not None:
            if record["error"] is None:raise AssertionError(f"{where}: expected failure {record['expect_error']}")
            if record["expect_error"].lower() not in record["error"].lower():
                raise AssertionError(f"{where}: failure {record['error']!r} does not contain {record['expect_error']!r}")
            continue
        if record["error"] is not None:raise AssertionError(f"{where}: unexpected failure {record['error']}")
        if record["expected"] is not None:
            got=[frame["data"] for frame in record["frames"]]
            if got!=record["expected"]:
                raise AssertionError(f"{where}: frames {got} != {record['expected']}")
        if record["expect_message"] is not None:
            if record["message"] is None or tohex(record["message"])!=record["expect_message"]:
                raise AssertionError(f"{where}: message {record['message']} != {record['expect_message']}")

class IsoTpVectors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.exists():
            raise unittest.SkipTest("build/ecu_kernel is missing; build the C++ binaries first")
        cls.kernel=Kernel()
    @classmethod
    def tearDownClass(cls):
        cls.kernel.close()
    def test_vectors(self):
        files=sorted(path for path in VECTORS.glob("*.json"))
        self.assertTrue(files,"No ISO-TP vectors found in tests/vectors/isotp")
        for path in files:
            with self.subTest(vector=path.name):
                vector=json.loads(path.read_text())
                config=vector["config"];steps=vector["steps"]
                clock=Clock()
                oracle=play(lambda cfg,swapped:OracleEndpoint(endpoint_config(cfg,swapped),clock),
                            config,steps,clock)
                check(oracle,"python oracle")
                clock.now=0
                compiled=play(lambda cfg,swapped:CppEndpoint(self.kernel,endpoint_config(cfg,swapped)),
                              config,steps,clock)
                check(compiled,"cpp engine")
                for step,py,ccpp in zip(steps,oracle,compiled):
                    self.assertEqual(ccpp["frames"],py["frames"],f"{path.name} step {step}")
                    self.assertEqual(ccpp["message"],py["message"],f"{path.name} step {step}")
                    self.assertEqual(bool(ccpp["error"]),bool(py["error"]),f"{path.name} step {step}")
                    if ccpp["error"]:
                        self.assertIn(py["error"].lower(),ccpp["error"].lower(),f"{path.name} step {step}")

if __name__=="__main__":unittest.main()
