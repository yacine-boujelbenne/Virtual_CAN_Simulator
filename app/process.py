"""Checked newline-JSON connection to a C++ process. No simulation logic in this adapter."""
import json
import select
import subprocess
import threading

class KernelProcess:
    def __init__(self, argv, cwd):
        self.lock = threading.Lock()
        self.process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1, cwd=cwd)
    def request(self, command):
        with self.lock:
            if self.process.poll() is not None:
                raise RuntimeError("C++ kernel process exited")
            self.process.stdin.write(json.dumps(command, allow_nan=False) + "\n")
            self.process.stdin.flush()
            if not select.select([self.process.stdout], [], [], 10)[0]:
                self.process.kill()
                raise RuntimeError("C++ kernel response timeout")
            line=self.process.stdout.readline()
            if not line: raise RuntimeError("C++ kernel closed its output")
            result=json.loads(line)
            if "error" in result: raise ValueError(result["error"])
            return result
    def close(self):
        with self.lock:
            self.process.stdin.close()
            try:self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
