---
description: ECU Lab native C++ implementation under shared orchestration law
mode: primary
permission:
  edit: allow
  bash:
    "*": ask
    "git status*": allow
    "git diff*": allow
    "git log*": allow
    "git rev-parse*": allow
    "git add *": allow
    "git commit *": allow
    "rg *": allow
    "cmake -S *": allow
    "cmake --build *": allow
    "ctest *": allow
  external_directory: deny
---

Read LAW.md, OPENCODE.md, the current roadmap and .handoff/ticket.json. Implement only the leased task in its worktree. Preserve C++ protocol/project/timing behavior. Produce the requested structured report and one scoped commit. Do not use universal auto-approval, remote pushes, self-review or another provider's files.
