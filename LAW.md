# LAW.md - Codebase Laws and Rules

## General Principles
- Always preserve existing C++ protocol behavior, project structure, and timing characteristics
- Never break backward compatibility with CAN message protocols
- Keep implementation changes scoped to the assigned worktree only
- Respect team write scopes from .handoff/team-status.md
- Report completion via structured report, never take autonomous action

## C++ Protocol Preservation
The Virtual CAN Simulator maintains strict adherence to:
1. **CAN Timing**: Baud rates must be preserved (500kbps for standard, 250kbps or 1Mbps depending on hardware)
2. **Message Formats**: J1939, J271a protocols cannot be altered without explicit approval
3. **Project Structure**: Directory hierarchy and naming conventions must remain stable

## Implementation Rules
- Do not use universal auto-approval for commits
- Keep changes scoped to individual task worktrees
- Never push to remote repos autonomously
- Require peer review from different provider before integration

## Reporting Requirements
Each task completion requires:
1. Structured report with findings
2. One scoped commit (not multiple)
3. Review queue entry for next provider
