# BASE-01: genuine native and P0 regression baseline

Recorded 24 October 2026 against leased base
`41c021e11ccec2c4d96086e812b6a68d6589828e`. Ready for independent review;
coordinator acceptance and integration are separate from the results below.

## Changes

CTest now discovers the 42 individual GoogleTest cases instead of reporting one
aggregate executable. Registration uses GoogleTest's CMake source scanning for
the existing plain TEST cases, keeping generated metadata in the build directory
while execution retains the source working directory required by resource fixtures.
An explicit `VEHICLE_LAB_P0_TESTS=ON` registers six named
Python regression suites using the exact CMake controller/worker target paths.
`SIM_BINARY` and `ECU_BINARY` overrides preserve the legacy test defaults while
preventing a nested build from accidentally testing an older `build/` binary.

`tests/run_regressions.py` fails if selected suites are empty, fail, have import
errors or skip tests. Native project A/B and HTTP project tests are selected
separately, so absent HTTP service coverage is explicit. Full Python regression
registration is opt-in to preserve the existing minimal Docker build, whose
build stage does not install Python/jsonschema. CMake prints that exclusion when
disabled. The host CI stage explicitly enables it and installs the dependencies.
The unchanged coordinator check argv still works; operators must enable the
option for the full gate rather than treating the default native-only run as P0
Python evidence.

The container workflow uses CMake/GoogleTest/CTest for the host baseline, saves
CTest logs and runs strict HTTP/distributed suites. The actual nine-container
identity and Docker motor stop/restart checks remain in the workflow. This report
does not claim the modified workflow has run remotely.

## Toolchain and reproduction

System CMake/GoogleTest were absent. `sudo -n apt-get install -y cmake libgtest-dev`
failed because interactive authentication is required. Approved package downloads
provided official Ubuntu packages, extracted into
`/tmp/ecu-native-toolchain/root` without a system install or a GoogleTest shim:

| Package | Version |
| --- | --- |
| cmake, cmake-data | 4.2.3-2ubuntu2 |
| libgtest-dev, googletest | 1.17.0-1build1 |
| libjsoncpp26 | 1.9.6-5 |
| librhash1 | 1.4.6-1.1 |

Compiler: GNU C++ 15.2.0. Python: 3.14.4 with system jsonschema. CMake found
`/tmp/ecu-native-toolchain/root/usr/lib/x86_64-linux-gnu/cmake/GTest/GTestConfig.cmake`.
The link command uses that package's `libgtest_main.a` and `libgtest.a`; the old
`/tmp/opencode` shim was not used.

From the leased worktree, the full build/test commands were:

```sh
. /tmp/ecu-native-toolchain/env.sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release -DVEHICLE_LAB_P0_TESTS=ON
cmake --build build/native -j2
ctest --test-dir build/native --output-on-failure --no-tests=error
python3 tools/check_baseline.py --verify-p0
```

The temporary environment sets PATH to the extracted `usr/bin`, LD_LIBRARY_PATH
to its `usr/lib/x86_64-linux-gnu` and CMAKE_PREFIX_PATH to its `usr` prefix. On a
host with system packages those overrides are unnecessary. Subsequent execution
of the task's exact configure/build commands succeeded on the same configured
build tree, preserving the cached ON option. Temporary tooling must be restored
or system packages installed before coordinator checks on a fresh host; no local
provider configuration or acceptance checks were modified by this task.

## Fresh results

| Check | Result |
| --- | --- |
| CMake Release configure/build | Passed; controller, worker and genuine GoogleTest executable built |
| CTest native | 42/42 cases passed, including 12 ISO-TP and three SHA-256 cases |
| `test_protocols` | 6 passed, zero skips |
| `test_envelopes` | 4 passed, zero skips |
| `test_adas_fusion` | 5 passed, zero skips |
| `test_isotp_vectors` | One test replayed all 13 JSON vectors against C++ and the Python oracle; passed |
| `test_project_ab.ProjectAB` | 6 passed, zero skips, including 60-tick frame/envelope equality |
| `test_performance` | 7 passed, zero skips |
| CTest combined | 48/48 entries passed (42 C++ cases plus six Python suites); repeated after isolating discovery metadata |
| Baseline presence check | Passed, no missing P0 markers |
| Coordination validator | Passed, 22 task definitions valid; not vehicle validation |
| CI YAML parse | Passed; full P0, HTTP and real Docker recovery stages present |
| Strict-runner negative checks | Missing binary, unset HTTP URL, empty selection and invalid module each exited 1 as required |
| Source preservation | No diff in app, headers, src, resources, Dockerfile, compose.yaml or web versus the leased base |

Negative skip checks deliberately produced six missing-binary skips and two
missing-HTTP-URL skips and were rejected. These are tests of the gate, not passing
vehicle regressions. All positive selected suites above had zero skips.

## Local-process HTTP/distributed evidence

Sandbox sockets are unavailable. With approved execution outside the sandbox,
the temporary harness `/tmp/ecu-native-toolchain/verify_local_cluster.py` started
eight independent worker processes (seven active, one reserve) and a controller
on dynamically allocated loopback ports. It selected this worktree's CMake
binaries, disabled external AI, waited for health and invoked:

```sh
python3 tests/run_regressions.py test_web test_distributed test_project_ab.ProjectHttp
```

`TEST_URL` pointed at the harness controller; `SIM_BINARY` and `ECU_BINARY` pointed
at `build/native`. The exact harness invocation was
`python3 /tmp/ecu-native-toolchain/verify_local_cluster.py <leased-worktree>`.
The harness stopped its own process groups after testing. Server output remains
at `/tmp/ecu-native-toolchain/local-cluster.log`. Results: eight web tests, five
distributed tests and two HTTP project tests passed; 15 total, zero skips, in
12.568 seconds. This includes logical worker halt/recovery, independent worker
diagnostics over both CAN links, reserve assignment and shared-clock behavior.
These are local process results, not Docker isolation evidence.

## Explicit unavailable evidence and handoff

`docker info --format '{{.ServerVersion}}'` failed with permission denied for
`/var/run/docker.sock` both inside and outside the sandbox. Docker image builds,
nine live container identities and the actual worker-container stop/restart test
were not run here. The existing CI workflow retains them; no CI link or container
pass is claimed. GPU/3D, native UI, calibrated MCU execution and durable replay
are outside this baseline task.

A different provider must review the exact candidate and the coordinator must
run its frozen checks before acceptance. Ensure CMake and genuine GoogleTest are
available in that check environment; for this machine the temporary environment
above provides them. To reproduce all P0 checks, configure with
`-DVEHICLE_LAB_P0_TESTS=ON`. Preserve the Docker access limitation in any release
report. Dependent work unlocks only after coordinator acceptance.
