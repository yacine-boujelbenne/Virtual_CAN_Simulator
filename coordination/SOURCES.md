# Sources and provenance

Inspected 24 October 2026. Product recommendations and provider lane assignments are this pack's design choices. CLI flags, account limits and backend APIs must be verified against the installed versions before enabling automation. No provider/model performance benchmark was performed.

- User CHECKPOINT: `sources/USER_CHECKPOINT.md`, SHA-256 `1a167ac72cd005c1f81a6c3ce0f70d971043abb6ed6536cbe2d81b3decb9c57f`.
- User README: `sources/USER_README.md`, SHA-256 `b151736660d60e7e15a0e91a026daf2025a315ed118cad0661f654468115100b`.
- GitHub plugin: private `yacine-boujelbenne/Virtual_CAN_Simulator`, feature `2a2c9149204e65455236152162b39f7bd4656a04`. The supplied later P0 implementation was not available in that inspected remote tree.
- Reference: https://github.com/bithuanglq/Carsim_MPC_Tracking-Demo - README, MY_MPCController3.m, my_quadprog.m and recursive tree. MATLAB/CarSim trajectory tracking; no LICENSE file in the inspected tree; solver contains a MathWorks notice. No code copied.
- esmini: https://esmini.github.io/esmini-lib-programming.html and https://esmini.github.io/controllers.html ; C header inspected through GitHub at `EnvironmentSimulator/Libraries/esminiLib/esminiLib.hpp`. Confirm the pinned signatures when implementing.
- CARLA synchrony: https://carla.readthedocs.io/en/latest/adv_synchrony_timestep/ . Optional later adapter.
- OSQP C interface/statuses: https://osqp.org/docs/interfaces/C.html . A maintained solver candidate, not installed/benchmarked by this pack.
- Qt Widgets: https://doc.qt.io/qt-6/qtwidgets-index.html and CMake guide https://doc.qt.io/qt-6/cmake-manual.html . Native UI recommendation.
- Codex non-interactive: https://developers.openai.com/codex/non-interactive-mode ; instructions: https://developers.openai.com/codex/agent-configuration/agents-md ; permissions: https://developers.openai.com/codex/agent-approvals-security . Official pages can redirect to learn.chatgpt.com.
- OpenCode CLI/rules/agents: https://opencode.ai/docs/cli/ , https://opencode.ai/docs/rules/ , https://opencode.ai/docs/agents/ . `opencode run` supports JSON event output; agents still need their configured permissions.
- Copilot programmatic CLI: https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-programmatic-reference . Repository instructions: https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/add-custom-instructions/add-repository-instructions . VS Code extension participation is distinct from CLI invocation.

None of these sources establishes your installed CLI versions, authentication, available model quota, GPU capacity or a successful native 3D build. Doctor/test reports must record those separately.
