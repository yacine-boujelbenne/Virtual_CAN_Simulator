# Repository agent entry point

Read `LAW.md`, `coordination/CURRENT_STATE.md` and `coordination/NATIVE_CPP_ROADMAP.md` before editing. Then read the role file for your current provider: OPENCODE.md, COPILOT.md or CODEX.md.

The user requires a C++/C primary application with native desktop/3D experiments. Web and Python tooling are secondary. Preserve the supplied P0 work; reconcile the local checkpoint with the older remote source first.

If running under the coordinator, `.handoff/ticket.json` is your assignment. Read `.handoff/team-status.md`, obey its write scopes/lease, and write only the specified report. Never take an unclaimed task or integrate/push a branch yourself. Keep one implementation commit per attempt and require a different provider for review.

If no ticket exists, inspect `python3 tools/coordinate.py status` from the control checkout, or read ORCHESTRATION.md. Do not assume that merely opening these instructions authorizes an autonomous unbounded loop.
