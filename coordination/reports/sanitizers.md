# SANITIZE-00: opt-in native sanitizer profile

Recorded 26 October 2026 against leased base
`88a97126baba8db2af459cb629893cfda12390d4`. Ready for independent review;
this report does not mark the coordinator task DONE or integrate the candidate.

## Changes

`CMakeLists.txt` now defines `VEHICLE_LAB_SANITIZERS`, default `OFF`. When enabled
on GCC or Clang it adds AddressSanitizer and UndefinedBehaviorSanitizer to every
native target:

```cmake
option(VEHICLE_LAB_SANITIZERS "Build native targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(VEHICLE_LAB_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "VEHICLE_LAB_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()
```

The default configuration is unchanged: no sanitizer flags are added unless the
option is enabled, and no target, source, generated file or test registration was
changed. `-fno-sanitize-recover=all` makes the first UBSan finding fail the process
instead of continuing. The option fails configuration explicitly on unsupported
compilers rather than silently producing an uninstrumented build.

A dedicated workflow, `.github/workflows/sanitizers.yml`, configures
`RelWithDebInfo` with `VEHICLE_LAB_SANITIZERS=ON` and `VEHICLE_LAB_P0_TESTS=ON`,
builds with `-j2`, runs every registered CTest entry with
`--output-on-failure --no-tests=error`, and uploads the CTest evidence on every
outcome. AddressSanitizer leak detection and UBSan halt-on-error are enabled in
the job environment. A failed sanitizer run fails the job; an empty test set also
fails because of `--no-tests=error`.

No simulation logic, timestamps, envelopes, protocol code, resource files or
fixtures were modified.

## Reproduction and evidence

System GoogleTest was unavailable, so the same extracted GoogleTest 1.17.0 prefix
used by earlier native work was supplied via `CMAKE_PREFIX_PATH`; this is a local
toolchain workaround, not a project dependency change.

Sanitizer profile:

```sh
cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/tmp/ecu-gtest/usr \
  -DVEHICLE_LAB_SANITIZERS=ON -DVEHICLE_LAB_P0_TESTS=ON
cmake --build build/sanitize -j2
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/sanitize --output-on-failure
```

Result: **61/61 CTest entries passed**, zero failures, zero skipped entries.
This includes 42 native GoogleTest cases, six named P0 suites and the native
runtime/CLI cases. The sanitizer build's compile flags contain
`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g`.
No ASan, LSan or UBSan finding was reported, so there are no pre-existing findings
to document or waive.

Default profile:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/ecu-gtest/usr -DVEHICLE_LAB_P0_TESTS=ON
cmake --build build/native -j2
ctest --test-dir build/native --output-on-failure
```

Result: **61/61 CTest entries passed**. The default `flags.make` files contain no
`-fsanitize` option. This confirms the opt-in profile does not alter the normal
Release build, and the same behavior/P0 coverage passes with instrumentation.

The workflow YAML parses successfully alongside the existing
`container.yml` and `orchestration.yml`. The workflow has not been executed on
GitHub-hosted infrastructure in this attempt; only the identical local commands
were executed.

## Handoff

The change is limited to the opt-in build profile and its CI job. A different
provider must review the exact candidate; only the coordinator may run trusted
acceptance and integrate it. Any future sanitizer finding must be fixed or
recorded with its exact failing test and log, not downgraded to a warning.
