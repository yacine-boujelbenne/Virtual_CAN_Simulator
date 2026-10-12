#!/usr/bin/env python3
"""Development equivalent of Compose: independent worker processes, shared clock."""
import json,os,signal,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
names=["Motor ECU","ABS ECU","BMS ECU","Dynamics ECU","Instrument Cluster","Radar ECU","ADAS ECU"]
def main():
    processes=[]
    try:
        for i in range(8):
            processes.append(subprocess.Popen([sys.executable,str(ROOT/"app/worker.py")],
                env={**os.environ,"HOST":"127.0.0.1","PORT":str(8081+i),"ECU_SERVICE":["ecu-motor","ecu-abs","ecu-bms","ecu-dynamics","ecu-cluster","ecu-radar","ecu-adas","ecu-reserve"][i]},cwd=ROOT))
        endpoints={name:f"http://127.0.0.1:{8081+i}" for i,name in enumerate(names)}
        controller=subprocess.Popen([sys.executable,str(ROOT/"app/server.py")],
            env={**os.environ,"ECU_ENDPOINTS":json.dumps(endpoints),"ECU_SPARES":json.dumps(["http://127.0.0.1:8088"])},cwd=ROOT)
        processes.append(controller)
        def stop(*_):raise KeyboardInterrupt
        signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
        while controller.poll() is None:time.sleep(.2)
        return controller.returncode
    except KeyboardInterrupt:return 0
    finally:
        for p in reversed(processes):
            if p.poll() is None:p.send_signal(signal.SIGTERM)
        for p in processes:
            try:p.wait(timeout=6)
            except subprocess.TimeoutExpired:p.kill();p.wait()
if __name__=="__main__":raise SystemExit(main())
