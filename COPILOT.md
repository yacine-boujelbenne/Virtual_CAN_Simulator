# Copilot - native UI and component role

Read LAW and your ticket. Your default lane is the C++ Qt desktop shell, ECU topology/inspector widgets, driver controls, project dialogs, performance/log views and focused UI/component tests. Reuse the light green/white/charcoal palette; tiny purple accents only. Role assignments can move to another provider when capacity is unavailable.

Use VS Code C/C++ tooling, CMake Tools, Qt Widgets/Designer and the pinned Qt installation. UI callbacks send commands to the native application service; never calculate vehicle physics, CAN routing or project hashes in widgets. Render immutable snapshots on the UI thread and keep the simulation worker independent of frame rate. Widgets display actual validity/age and distinguish virtual hardware from host metrics.

First implement against the accepted typed interface and a labelled fixture. A fixture demonstrates UI only. Replace it with the real controller bridge before claiming native application acceptance. Wire keyboard/gamepad/sliders through DriverDemand and actuator ECUs. `New`, `Open`, `Save` and `.vcanproj` dialogs delegate validation and canonical hashing to C++ core services.

Wait if the interface dependency or write scope is unresolved. Do not refactor SimulationManager, resource timing, protocol codecs or CMake root while OpenCode owns them. Report the needed interface delta and choose another independent UI/docs task. Respect high-DPI layout and signal/slot thread boundaries. Keep the existing web UI as an optional demo; do not expand it into the product architecture.

VS Code Copilot Chat/agent is a MANUAL participant in this pack. It cannot be launched by pretending that the extension is a command-line executable. Open the claimed worktree in VS Code, paste the ticket prompt, then submit the structured report with the coordinator. An authenticated, separately installed Copilot CLI can be enabled for bounded automation after its command/permissions are verified locally.

For interruption, record the current widget/component, fixture versus real data status, changed files, candidate commit, tests and the next click/command needed. Read the other providers' team-status snapshot before resuming. Never approve your own implementation or push the integration branch.
