# Validation of this orchestration pack

Date: 24 October 2026.

## Fresh checks

- `python3 tools/validate_coordination.py`: passed; 22 dependency-scoped tasks, required role/law files and disabled provider example validated.
- `python3 -m py_compile tools/coordinate.py tools/check_baseline.py tools/validate_coordination.py`: passed.
- `python3 -m unittest discover -s tests/orchestration -v`: **22 tests passed**, including real temporary Git branches/worktrees and SQLite state.

Tests cover independent review and fast-forward integration; self-review refusal; dependency/scope waits; expired-token fencing and preserved worktrees; heartbeat; out-of-scope/dirty/multi-commit rejection; reviewer HEAD integrity; trusted-check failure; stale-base rework; simulated quota failure with a second author and a third reviewer; missing executable; timeout; one-provider review wait; STOP; persisted progress; cooldown/auth recovery; manual/unverified mode; immutable task/cycle rejection; and concurrent claims.

Provider subprocesses in tests are explicitly fake Python programs. They exercise coordinator mechanics and failure handling. They are not OpenCode, Copilot or Codex sessions and cannot establish model quality, quota behavior or CLI permission compatibility.

## Fresh limitations

Doctor found Git, Python, g++, Node and a `code` command in this environment. It did not find CMake, Docker, OpenCode, Copilot CLI or Codex CLI. No account login, model invocation, native app build, real 3D viewer, MPC solve, runtime ECU calibration or provider token consumption was performed.

`check_baseline.py --verify-p0` intentionally fails on the coordination-only pack and the inspected old source because the checkpoint's later P0 files are absent. It is a blocking source-preservation gate, not a regression in the new coordinator. The 42 C++ tests and P0 application checks in the supplied checkpoint remain reported historical evidence until rerun against the real local source.

The new GitHub workflow verifies coordination tooling only. The existing application/container workflow remains a separate check. Future native/3D gates require the actual implementation and appropriate host/dependencies.

## Operational limits

One automatic provider process is dispatched at a time; manual participants may work on disjoint tasks. Reports, branches and checks are cooperative integrity controls, not a hostile-agent sandbox. Context/quota errors cause bounded failover, not automatic model/account purchases. A single remaining author cannot independently review itself. Canonical integration is local fast-forward only; advanced bases trigger a new attempt/review. Provider wall budgets and trusted-check timeouts are separate.
