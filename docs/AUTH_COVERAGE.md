# BL-29: authentication tests and coverage

Owner: Arundhathi K. Execution date: 10 November 2026 (Asia/Calcutta).

## Result

**40 tests passed; 0 failures, 0 skips.** This is a fresh GCC coverage-enabled build based on main `8615120cf193294786b49fbe39a63f13e55b9864`, with the additional Instructor-boundary regression in this PR. Existing production authentication code is unchanged.

| Production source | Lines executed | Branches taken at least once |
|---|---:|---:|
| src/auth.cpp | 188 / 204 (92.16%) | 200 / 268 (74.63%) |
| src/password_hasher.cpp | 76 / 84 (90.48%) | 86 / 140 (61.43%) |
| src/auth_event_log.cpp | 16 / 17 (94.12%) | 21 / 42 (50.00%) |
| src/sqlite_user_repository.cpp | 160 / 169 (94.67%) | 129 / 240 (53.75%) |
| **Total** | **440 / 474 (92.83%)** | **436 / 690 (63.19%)** |

Scope is the four named production translation units, measured from only the Auth, PasswordHasher, AuthEventLog, SqliteUserRepository and AuthRoleBoundary suites. Headers, third-party libraries, UI, Question Bank, Quiz Engine and grading are outside this coverage denominator.

Every branch reported by GCC is counted, including exception/compiler-generated branches. Coverage values therefore vary with compiler and platform. Uncovered source lines and branch locations are included in the JSON/HTML; no exclusions were applied to hide unexecuted auth code. Coverage reports execution, not correctness. BL-29 specifies a report, not a numeric minimum; these results do not claim 100% coverage.

## Evidence and test-plan results

- [Actual results for UT-AUTH-01–09](TEST_PLAN_AUTH_RESULTS.md).
- [HTML coverage report](evidence/auth-coverage-2026-11-10/coverage.html).
- [Counts, source hashes and uncovered locations](evidence/auth-coverage-2026-11-10/coverage-summary.json).
- [Raw test output](evidence/auth-coverage-2026-11-10/auth-tests.txt).
- [Native gcov output](evidence/auth-coverage-2026-11-10/gcov-output.txt).
- [Original configure/build transcript](evidence/auth-coverage-2026-11-10/build.txt).
- [Build-input manifest](evidence/auth-coverage-2026-11-10/source-manifest.json).

The existing [Sprint 1 auth record](AUTH_VERIFICATION.md) remains a historical 39-test verification. This BL-29 record adds the direct Student-to-Instructor boundary case and line/branch measurement.

## Reproduce

Requires CMake, a GCC toolchain, matching gcov, SQLite/OpenSSL and GoogleTest (or CMake's existing fallback), plus Python 3.9+. The report script uses only Python's standard library.

From the repository root on Linux:

```bash
cmake -S . -B build-auth-coverage -DQUIZSPHERE_BUILD_TESTS=ON -DQUIZSPHERE_COVERAGE=ON
cmake --build build-auth-coverage --target auth_tests -j2
python3 scripts/auth_coverage.py --source-dir . --build-dir build-auth-coverage --output-dir auth-coverage --gcov gcov --revision "$(git rev-parse HEAD)"
```

On Windows, configure with your matching GCC generator/toolchain and dependencies, then use `python` and the matching `gcov.exe`. The recorded environment was GCC/gcov 15.1.0, SQLite 3.50.3 and OpenSSL 3.5.1, with MinGW Makefiles and installed GoogleTest.

`scripts/auth_coverage.py` removes only gcda counters inside the selected build directory, runs the focused suites in an isolated temporary directory, reads GCC's native JSON counters and emits HTML, JSON, test output and native gcov output. Do not run another coverage-instrumented executable against the same build while generating the report. Temporary fixtures are not committed. Raw compressed gcov JSON remains in the output's raw-gcov folder for inspection, but the repository keeps only the compact derived report and stdout.

CI first captures the existing full-suite coverage.xml artifact, then resets counters and generates a separate auth-only measurement. Download the `auth-coverage` artifact for the current PR/commit; the committed Windows evidence remains pinned to this execution and does not claim CI's numeric values.

## Limits and separate work

UT-AUTH-08 module account operations are tested, but the Admin-menu walkthrough/removal confirmation is not claimed here; that is BL-27/ST-ADM-01. Other modules' full security/system flows remain BL-28. The legacy PDF's plain SHA-256 wording differs from the implemented salted PBKDF2-HMAC-SHA256 design; this deviation is explicitly recorded in the result addendum and must be aligned in BL-30. No PDF regeneration, final release or complete product test-plan pass is claimed.
