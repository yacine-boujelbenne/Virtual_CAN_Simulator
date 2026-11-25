"""Run explicitly selected unittest suites, failing on skips or empty selection.

Use SIM_BINARY and ECU_BINARY to select the binaries produced by this build.
Select test_project_ab.ProjectAB for native A/B checks, and ProjectHttp separately
against a running distributed service with TEST_URL set.
"""
import argparse
import json
import unittest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="+", help="unittest module, class or method names")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromNames(args.suites)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    passed = result.wasSuccessful() and result.testsRun > 0 and not result.skipped
    print(json.dumps({"tests": result.testsRun, "skipped": len(result.skipped),
                      "failures": len(result.failures), "errors": len(result.errors),
                      "regression_gate": "passed" if passed else "failed"}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
