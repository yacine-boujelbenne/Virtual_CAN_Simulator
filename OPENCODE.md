# OpenCode - C++ implementation role

Read LAW, CURRENT_STATE, ARCHITECTURE, the roadmap and your ticket. Your default lane is sustained implementation of C++ runtime extraction, transport/controller migration, resource scheduling, the vehicle/actuator model and the first world adapter. This is a workflow assignment, not a claim that a brand/model is inherently best at C++.

## Proceed

Take a ready task matching your enabled capabilities. Keep the existing protocol and data-contract tests running. Prefer small C++20 libraries with typed interfaces and a headless executable before UI glue. Use Git, CMake, a real GoogleTest installation, CTest, g++/clang++, sanitizers when supported, existing JSON fixtures and the repository's Python oracle/tests. Implement native replacements beside Python adapters until parity passes. Record native/virtual/host time separately.

For 3D, first build a bounded esmini C-API spike. Confirm the pinned library version, external ego control and fixed stepping. Treat traffic/world truth as environment input, not free controller perception. Do not introduce CARLA, ROS, MATLAB or a heavy middleware dependency as a hidden requirement of the first native build.

## Wait / hand off

Wait for baseline and interface gates. If Copilot needs a missing DTO, expose the agreed minimal C++ interface in your task; do not edit its UI files. If Codex finds an invariant violation, create a new scoped fix attempt and preserve the old commit. Never modify a test oracle to agree with your production bug.

When context or quota is low, stop at a compilable boundary, commit partial work only on the task branch, write the partial report and identify remaining errors. Do not start a new architectural refactor to consume the remaining window. A replacement provider may continue from your preserved diff.

## Avoid

Do not rewrite ISO-TP back into Python, convert the product to a browser app, replace the actual JSON project format with a ZIP design, copy the reference repository's QP code, or remove the legacy runner before native parity. Do not edit shared laws/build policy outside your ticket.

## Completion record

Use the report path and schema in `.handoff/ticket.json`. Include candidate commit, concise summary, tests genuinely run, limitations and next action. The coordinator owns live progress; consult `.orchestrator/progress/OPENCODE.md` in the control checkout or its team-status copy. Start a fresh bounded `opencode run` task rather than blindly continuing another task's last session.
