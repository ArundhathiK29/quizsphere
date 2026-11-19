# Branch Coverage Report

Report date: **2026-11-19 (Asia/Calcutta)**. Tested source: [`cce8549061143fab649eab618051becc6ffc06e7`](https://github.com/ArundhathiK29/quizsphere/commit/cce8549061143fab649eab618051becc6ffc06e7). This historical measurement is pinned to that source. A subsequent review follow-up changes console input handling and adds recovery tests; its validation is recorded separately in REVIEW_FOLLOWUP.md. These coverage figures are not remeasured for that follow-up. Original source/test hashes are in [evidence/part3-2026-11-19/source-manifest.json](evidence/part3-2026-11-19/source-manifest.json).

**Measured production scope: 5740/10914 (52.59%).**

Scope: compiled Linux/GCC production `src/` translation units, including console/browser sources. Tests, dependencies and headers are outside the filter; Windows-only preprocessor paths are outside this Linux measurement. A Windows functional pass is not Windows code-coverage evidence.

Method: build with QUIZSPHERE_COVERAGE=ON, clear counters within that selected build, run full CTest and each GoogleTest executable plus real HTTP smoke, export before auth-only counter resets. Tool: gcovr 7.0

Copyright (c) 2013-2024 the gcovr authors
Copyright (c) 2013 Sandia Corporation.
Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
the U.S. Government retains certain rights in this software.. Every GCC-reported branch is included, including C++ exception/compiler-generated branches; covered means taken at least once. No exception exclusion/denominator reduction inflates the rate.

| File | Covered | Total | Rate |
| --- | ---: | ---: | ---: |
| `src/admin_user_menu.cpp` | 273 | 455 | 60.00% |
| `src/analytics_console.cpp` | 240 | 456 | 52.63% |
| `src/analytics_module.cpp` | 242 | 424 | 57.08% |
| `src/auth.cpp` | 206 | 270 | 76.30% |
| `src/auth_event_log.cpp` | 20 | 40 | 50.00% |
| `src/common_types.cpp` | 6 | 12 | 50.00% |
| `src/console_input.cpp` | 25 | 38 | 65.79% |
| `src/console_menu.cpp` | 228 | 506 | 45.06% |
| `src/data_access_layer.cpp` | 784 | 1881 | 41.68% |
| `src/grading_module.cpp` | 126 | 198 | 63.64% |
| `src/import_logger.cpp` | 24 | 48 | 50.00% |
| `src/input_validation.cpp` | 48 | 54 | 88.89% |
| `src/main.cpp` | 270 | 579 | 46.63% |
| `src/password_hasher.cpp` | 87 | 142 | 61.27% |
| `src/question_bank.cpp` | 595 | 1008 | 59.03% |
| `src/question_repository_file.cpp` | 238 | 424 | 56.13% |
| `src/quiz_attempt.cpp` | 117 | 216 | 54.17% |
| `src/quiz_console.cpp` | 380 | 686 | 55.39% |
| `src/quiz_engine.cpp` | 441 | 754 | 58.49% |
| `src/result_view.cpp` | 136 | 240 | 56.67% |
| `src/sqlite_user_repository.cpp` | 130 | 240 | 54.17% |
| `src/timed_console_input.cpp` | 38 | 62 | 61.29% |
| `src/web_application.cpp` | 823 | 1649 | 49.91% |
| `src/web_server.cpp` | 263 | 532 | 49.44% |

Evidence: [JSON summary](evidence/part3-2026-11-19/coverage-summary.json), [Cobertura XML](evidence/part3-2026-11-19/coverage.xml), [HTML](evidence/part3-2026-11-19/coverage.html), [lines](evidence/part3-2026-11-19/coverage-lines.txt), [branches](evidence/part3-2026-11-19/coverage-branches.txt). Exact source hashes and environment are retained.

The Part 3 handout asks for measured reports and states no numerical threshold. **Above-90% whole-project branch coverage is not established.** Any separately imposed lecturer threshold remains a separate unmet criterion; this report does not waive it. Uncovered OS/storage failures and exception paths remain visible. Coverage is execution evidence, not proof that no further bugs exist.
