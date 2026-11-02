"""Real Qt/C++ process integration; Python is only the test driver."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

desktop, runtime, root = map(lambda p: str(Path(p).resolve()), sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix="vclab-desktop-") as directory:
    evidence = Path(directory) / "drive"
    result = subprocess.run(
        [desktop, "-platform", "offscreen", "--root", root, "--runtime", runtime,
         "--scenario", "resources/scenarios/ev_lateral.json",
         "--smoke", str(evidence)], capture_output=True, text=True, timeout=75)
    assert result.returncode == 0, result.stdout + result.stderr
    summary = json.loads((evidence / "summary.json").read_text())
    state = json.loads((evidence / "final-snapshot.json").read_text())
    assert summary["passed"] and summary["connected_ecus"] == 9
    assert summary["ticks"] == 620 and summary["frames"] > 4000
    assert summary["peak_speed_mps"] > 10 and summary["final_speed_mps"] == 0
    assert summary["max_abs_y_m"] > 7
    pids = {ecu["container"]["kernel_pid"] for ecu in state["ecus"]}
    assert len(pids) == 9, "One distinct C++ worker per ECU"
    assert all(not Path(f"/proc/{pid}").exists() for pid in pids), "Workers must be reaped on close"
    diagnostic = json.loads((evidence / "diagnostic.json").read_text())
    assert diagnostic["bytes"][:3] == [0x62, 0xF1, 0x90]
    assert len(diagnostic["bytes"]) == 20 and diagnostic["frame_count"] > 1
    samples = [json.loads(line) for line in (evidence / "telemetry.jsonl").read_text().splitlines()]
    assert len(samples) >= 62
    assert all(s["kind"] == "telemetry_sample" for s in samples)
    assert all(a["tick"] <= b["tick"] for a, b in zip(samples, samples[1:]))
    assert all(s["project_hash"] == summary["project_hash"] for s in samples)
    assert samples[-1]["world"] == state["world"]
    for name in ("desktop-driving.png", "desktop-stopped.png"):
        data = (evidence / name).read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n" and len(data) > 10000
    restored = subprocess.run([runtime, "--project", str(evidence / "demo.vcanproj"), "--steps", "1"],
                              cwd=root, capture_output=True, text=True, timeout=15)
    assert restored.returncode == 0, restored.stderr
    assert json.loads(restored.stdout)["project_hash"] == summary["project_hash"]
    # Existing evidence must never be silently overwritten.
    rejected = subprocess.run([desktop, "-platform", "offscreen", "--smoke", str(evidence)],
                              capture_output=True, text=True, timeout=5)
    assert rejected.returncode == 2
    # A startup failure is surfaced, not reported as a passing empty demo.
    failed = subprocess.run([desktop, "-platform", "offscreen", "--runtime", runtime,
                             "--scenario", "missing-scenario.json", "--smoke", str(Path(directory) / "failed")],
                            capture_output=True, text=True, timeout=10)
    assert failed.returncode != 0
    print(json.dumps(summary, indent=2))
