# What to keep, replace and remove

| Item | Action now | Removal condition |
| --- | --- | --- |
| C++ CAN/FD/IsoTp/CanTp, SHA and event envelopes | Keep, extract carefully | No planned removal; replace internals only under parity tests |
| ECU JSON/hardware definitions, fusion/CAN layout | Keep and version | Only migrate with cross-version fixtures and documented mapping |
| Actual JSON `.vcanproj` v1 | Keep as compatibility baseline | New major format needs a migration/export path and A/B evidence |
| Existing protocol/oracle/A-B/performance tests | Keep | Delete a test only with a reviewed replacement covering the same failure |
| Python controller/server/worker | Keep as transitional reference | Remove from production/default Compose only after native control/transport/diagnostic and failure parity; retain useful test harnesses |
| Python `app/transport.py` | Keep as independent oracle | Never turn it back into production ISO-TP; remove only if another independent oracle exists |
| Web dashboard | Keep as optional demo/inspection | Remove only after user-approved UI policy; it does not define the native architecture |
| C++ SimulationManager monolith | Wrap then extract one responsibility per slice | Remove duplicate old implementations only after all callers and tests move |
| Old Main.cpp/config/build scripts | Audit CMake/callers first | Delete only proven-unused code with a path-specific review; do not guess |
| In-memory performance ring buffers | Keep for live UI | Add durable recorder beside them; never call the ring a full experiment log |
| User runs, assets, configs, credentials | Preserve and exclude appropriately | Never erase them in agent cleanup; no secrets in Git/logs |
| Generated builds/caches | Ignore, avoid adding to Git | Clean only task-owned disposable build directories |
| Reference MATLAB/CarSim source | Learn architecture, do not vendor | No copying of unlicensed/proprietary solver/code |
| Python development coordinator | Keep as tooling | Not a product-runtime dependency; rewrite only if there is a concrete need |

No task in this starting backlog is an authorization for wholesale deletion. Cleanup is a late, explicitly scoped slice with a before/after build and command-compatibility report.
