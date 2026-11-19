# Code Coverage Report

Report date: **2026-11-19 (Asia/Calcutta)**. Tested source: [`cce8549061143fab649eab618051becc6ffc06e7`](https://github.com/ArundhathiK29/quizsphere/commit/cce8549061143fab649eab618051becc6ffc06e7). This historical measurement is pinned to that source. A subsequent review follow-up changes console input handling and adds recovery tests; its validation is recorded separately in REVIEW_FOLLOWUP.md. These coverage figures are not remeasured for that follow-up. Original source/test hashes are in [evidence/part3-2026-11-19/source-manifest.json](evidence/part3-2026-11-19/source-manifest.json).

**Measured production scope: 3874/4323 (89.61%).**

Scope: compiled Linux/GCC production `src/` translation units, including console/browser sources. Tests, dependencies and headers are outside the filter; Windows-only preprocessor paths are outside this Linux measurement. A Windows functional pass is not Windows code-coverage evidence.

Method: build with QUIZSPHERE_COVERAGE=ON, clear counters within that selected build, run full CTest and each GoogleTest executable plus real HTTP smoke, export before auth-only counter resets. Tool: gcovr 7.0

Copyright (c) 2013-2024 the gcovr authors
Copyright (c) 2013 Sandia Corporation.
Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
the U.S. Government retains certain rights in this software.. An executable line is covered when it ran at least once.

| File | Covered | Total | Rate |
| --- | ---: | ---: | ---: |
| `src/admin_user_menu.cpp` | 141 | 146 | 96.58% |
| `src/analytics_console.cpp` | 129 | 150 | 86.00% |
| `src/analytics_module.cpp` | 236 | 258 | 91.47% |
| `src/auth.cpp` | 193 | 207 | 93.24% |
| `src/auth_event_log.cpp` | 16 | 17 | 94.12% |
| `src/common_types.cpp` | 8 | 9 | 88.89% |
| `src/console_input.cpp` | 28 | 34 | 82.35% |
| `src/console_menu.cpp` | 212 | 311 | 68.17% |
| `src/data_access_layer.cpp` | 597 | 696 | 85.78% |
| `src/grading_module.cpp` | 120 | 124 | 96.77% |
| `src/import_logger.cpp` | 27 | 29 | 93.10% |
| `src/input_validation.cpp` | 24 | 24 | 100.00% |
| `src/main.cpp` | 136 | 182 | 74.73% |
| `src/password_hasher.cpp` | 76 | 84 | 90.48% |
| `src/question_bank.cpp` | 460 | 495 | 92.93% |
| `src/question_repository_file.cpp` | 211 | 238 | 88.66% |
| `src/quiz_attempt.cpp` | 143 | 145 | 98.62% |
| `src/quiz_console.cpp` | 173 | 180 | 96.11% |
| `src/quiz_engine.cpp` | 288 | 302 | 95.36% |
| `src/result_view.cpp` | 70 | 72 | 97.22% |
| `src/sqlite_user_repository.cpp` | 160 | 169 | 94.67% |
| `src/timed_console_input.cpp` | 33 | 34 | 97.06% |
| `src/web_application.cpp` | 286 | 308 | 92.86% |
| `src/web_server.cpp` | 107 | 109 | 98.17% |

Evidence: [JSON summary](evidence/part3-2026-11-19/coverage-summary.json), [Cobertura XML](evidence/part3-2026-11-19/coverage.xml), [HTML](evidence/part3-2026-11-19/coverage.html), [lines](evidence/part3-2026-11-19/coverage-lines.txt), [branches](evidence/part3-2026-11-19/coverage-branches.txt). Exact source hashes and environment are retained.

The Part 3 handout asks for measured reports and states no numerical threshold. **Above-90% whole-project branch coverage is not established.** Any separately imposed lecturer threshold remains a separate unmet criterion; this report does not waive it. Uncovered OS/storage failures and exception paths remain visible. Coverage is execution evidence, not proof that no further bugs exist.
