# System Validation Report

Report date: **2026-11-19 (Asia/Calcutta)**. Tested source: [`cce8549061143fab649eab618051becc6ffc06e7`](https://github.com/ArundhathiK29/quizsphere/commit/cce8549061143fab649eab618051becc6ffc06e7). This historical measurement is pinned to that source. A subsequent review follow-up changes console input handling and adds recovery tests; its validation is recorded separately in REVIEW_FOLLOWUP.md. These coverage figures are not remeasured for that follow-up. Original source/test hashes are in [evidence/part3-2026-11-19/source-manifest.json](evidence/part3-2026-11-19/source-manifest.json).

| Real process/system scenario | Result | Evidence |
| --- | --- | --- |
| `ST_ADM_01_AdminUserFlow` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `ST_SEC_08_ActualConsoleValidation` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `ST_SEC_09_TimedConfirmationControls` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `UT_DAL_01_ProcessKillAtomicity` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `ST_PART3_StudentInstructorAdminReports` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `web_timer_slow-header` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `web_timer_slow-body` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `web_timer_practice-switch` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `web_timer_cross-session-manual` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| `web_timer_cross-session-auto` | PASS | [stdout](evidence/part3-2026-11-19/ctest.txt) |
| Original browser HTTP authentication, CSRF/Host/Origin/roles, practice, grading, restart, retake and rankings | PASS | [HTTP smoke](evidence/part3-2026-11-19/http-smoke.txt) |
| PERF-01: five 20-question load/start trials | PASS: maximum 14.989645 ms; target <2000 ms | [measurements](evidence/part3-2026-11-19/gtest/part3_acceptance_tests.txt) |
| Seeded malformed CSV | PASS: 100 inputs; unchanged bank | [fuzz](evidence/part3-2026-11-19/gtest/part3_acceptance_tests.txt) |
| Storage boundary and cppcheck | PASS: zero static findings | [cppcheck](evidence/part3-2026-11-19/cppcheck.txt) |

The actual console flow creates Student/Instructor/Admin accounts, persists two questions and a quiz, selects a displayed shuffled answer, submits 4/8, checks itemised/weak-topic output, restarts, displays all rankings and exports/verifies both Instructor and Admin CSV transcripts. Separate Admin process tests cover create/edit/disable/enable/remove, cancellation and login effects.

Crash testing uses the actual app/DAL. A test-only SQLite trigger delays report insertion inside the real grading transaction. The parent kills the process with an active rollback journal. Three trials reopen SQLite, check integrity and verify no submitted flag, partial report or item rows survived. This is process termination, not merely a simulated failed write.

Performance includes DB reopen, question loading, engine wiring and starting/persisting a twenty-question graded attempt. It excludes fixture creation, login and process startup, uses five trials on one local dataset and is not a multi-user load benchmark. CPU: 12th Gen Intel(R) Core(TM) i5-1235U; compiler/environment data retained. [Windows timing](evidence/part3-2026-11-19/perf-windows.txt) is separate from Linux timing.

## Acceptance exceptions

USE-01's original console three-step target is not established. Dedicated Admin item-analysis presentation is absent although topic display and the difficulty API are tested. Question flat-file parity is tested, but a complete flat-file replacement DAL is absent. Team/course-instructor UAT signatures remain pending. These exceptions are not converted into passes: **the whole original STP exit gate is not asserted satisfied**. See TEST_CASE_DOCUMENT.md for per-ID results.
