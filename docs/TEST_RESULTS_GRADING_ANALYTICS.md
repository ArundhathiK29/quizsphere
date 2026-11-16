# Test Plan results — Grading, Rankings, Difficulty Index, Topic Breakdown, Transcript Export (BL-44)

Owner: Chaturya. Scope: every Software Test Plan (STP v1.0) row traced to the Grading Module,
Analytics Module and the itemised Result View, with the **Actual Result** and **Pass/Fail** columns
filled from the real run. Each row names the GoogleTest(s) that implement it, so the plan and the
CI output line up.

**Run:** 2026-11-16, Debug + `-DQUIZSPHERE_COVERAGE=ON`, GCC 13 / Ubuntu 24.04 (same image as CI),
`ctest` **163/163 passed** (includes Ashwin's BL-38/39 timer tests from #78), cppcheck (CI flags) clean.
Evidence: [`evidence/grading-analytics-ctest-2026-11-16.txt`](evidence/grading-analytics-ctest-2026-11-16.txt),
[`evidence/grading-analytics-gtest-2026-11-16.txt`](evidence/grading-analytics-gtest-2026-11-16.txt),
[`evidence/grading-analytics-coverage-2026-11-16.txt`](evidence/grading-analytics-coverage-2026-11-16.txt).

Reproduce:

```
cmake -S . -B build-cov -DQUIZSPHERE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cov && ctest --test-dir build-cov
gcovr -r . --object-directory build-cov --filter 'src/' --txt
```

## Coverage (gcovr, line)

| File | Lines | Exec | Cover |
|---|---|---|---|
| src/grading_module.cpp | 113 | 108 | **95%** |
| src/analytics_module.cpp | 258 | 236 | **91%** |
| src/result_view.cpp | 72 | 70 | **97%** |
| src/analytics_console.cpp | 150 | 129 | **86%** |
| whole project (src/) | 3690 | 3124 | 84% |

Uncovered lines in `analytics_module.cpp`/`grading_module.cpp` are the `if (!result.ok()) return result;`
propagation branches for DAL failures that the SQLite DAL does not produce in a healthy database
(those paths are exercised with test doubles in `GradingModuleTest.*Failure*` for the grader).
In `analytics_console.cpp` they are the "could not list quizzes" / blank-input cancel branches.

## Unit tests (UT-GRADE, UT-ANLY)

| STP ID | Scenario (from the plan) | Expected result | GoogleTest(s) | Actual result | Pass/Fail |
|---|---|---|---|---|---|
| UT-GRADE-01 | Basic grading: +4 correct, −1 wrong | Score matches scoring scheme; itemised result produced | `GradingModuleTest.UT_GRADE_01_ScoresCorrectWrongAndBlankAnswers`, `UT_GRADE_02B_PerQuestionFloorAppliesEvenWhenTotalWouldStayNonNegative` | Correct → +marks, wrong → floored at 0, blank → 0; +4/−1 with one correct one wrong = 4.0; itemised_breakdown has one ItemResult per question | **Pass** |
| UT-GRADE-02 | Negative floor: penalty exceeds marks | Question score never below zero (BR-5) | `GradingModuleTest.UT_GRADE_02_NegativeFloor_BR5_PenaltyExceedsMarks`, `UT_GRADE_03_NegativeMarkingNeverScoresAWrongAnswerBelowZero` | Each wrong item's marks_awarded = 0.0, total 0.0 / 1.0 as expected | **Pass** |
| UT-GRADE-04 | Second graded attempt for same quiz/student | ATTEMPT_ALREADY_SUBMITTED; first attempt unchanged | `GradingModuleTest.UT_GRADE_04_SecondGradedAttemptRejected`, `BlindRegradeWithNoFreshOpenAttemptIsStillAttemptAlreadySubmitted`, `GradingFinalizesOnlyTheIntendedGradedAttemptAndLeavesPracticeUntouched` | Re-grade returns ATTEMPT_ALREADY_SUBMITTED; stored report identical; practice attempt untouched | **Pass** |
| UT-GRADE-06 | Itemised result persisted and retrievable | GradeReport stored and returned by getReport | `GradingModuleTest.UT_GRADE_06_ItemisedResultPersistsAndIsRetrievableViaGetReport`, `GradingSqliteIntegrationTest.GradedResultSurvivesClosingAndReopeningTheDatabase` | Report identical after save → getReport, also after closing/reopening SQLite | **Pass** |
| UT-GRADE-07 | Tampered answers (out-of-range, duplicate, foreign question) | Rejected by the Grading Module, not just the UI | `GradingModuleTest.UT_GRADE_07_TamperingRejectedByGradingModuleNotJustUI`, `DuplicateAnswerForTheSameQuestionIsRejected`, `AnswerForAQuestionOutsideTheAttemptIsRejected`, `NegativeOptionIndexOtherThanBlankIsRejected`, `OutOfRangeOptionIndexIsRejected` | VALIDATION_FAILED; nothing stored | **Pass** |
| UT-GRADE-08 | Unknown quiz | QUIZ_NOT_FOUND | `GradingModuleTest.UT_GRADE_08_UnknownQuizReturnsQuizNotFound` | QUIZ_NOT_FOUND | **Pass** |
| UT-GRADE-09 | Late submission after expiry | Graded and flagged auto-submitted (TIME_EXPIRED) | `GradingModuleTest.UT_GRADE_09_LateSubmissionIsStillGradedAndFlaggedTimeExpired` | Graded; report.auto_submitted = true; result view prints "(auto-submitted: time expired)" | **Pass** |
| UT-ANLY-01 | Difficulty index: 4 attempts, 3 correct | Index = 75% | `AnalyticsModuleTest.UT_ANLY_01_FourAttemptsThreeCorrectGivesSeventyFivePercent`, `DifficultyIndexComputesPercentCorrectFromGradedAttemptsOnly`, `DifficultyIndexTreatsABlankAnswerAsIncorrect`, `AnalyticsSqliteIntegrationTest.DifficultyIndex*` | 75.0; graded-only (practice leaves 50% at 50%); blank = incorrect; survives restart | **Pass** |
| UT-ANLY-02 | Topic breakdown: low in one topic, high in another | Average per subject tag correct; weakest topic identified | `AnalyticsModuleTest.TopicBreakdownReturnsTheStudentsOwnGradedTopicsOrAttemptNotFound`, `TopicAnalyticsGivesStudentVsClassAverageWeakestFirstExcludingPractice`, `TopicAnalyticsFlagsEveryTiedWeakestTopic`, `TopicAnalyticsEnforcesStudentOwnOnlyAndInstructorAdminAnyone` | Science 0% (weakest) vs Math 100%; class average 50% / 75% over graded attempts only; ties all flagged; Student own-only | **Pass** |
| UT-ANLY-03 | Per-quiz leaderboard ranking | Descending by score, ties share a rank | `AnalyticsModuleTest.LeaderboardRanksGradedAttemptsDescendingWithSharedTiesAndSkippedRanks`, `LeaderboardForUnknownQuizReturnsQuizNotFound`, `LeaderboardWithNoGradedAttemptsIsEmptyNotAnError` | Ranks 1,1,3; unknown quiz → QUIZ_NOT_FOUND; empty → OK with no rows | **Pass** |
| UT-ANLY-04 | Topic and all-time rankings | Sums across quizzes; same tie rule | `AnalyticsModuleTest.TopicLeaderboardSumsMarksAcrossQuizzesWithSharedTiesAndPracticeExcluded`, `AllTimeLeaderboardSumsScoreAcrossQuizzesWithSharedTiesAndPracticeExcluded`, `*IsEmptyNotAnError`, `LeaderboardSqliteTest.*` | Sums 3.0/3.0/1.0 → ranks 1,1,3; exact graded-report lookup (newer practice report ignored) | **Pass** |
| UT-ANLY-05 | Transcript export content | CSV with quiz, score, date per graded attempt | `AnalyticsModuleTest.TranscriptListsSubmittedGradedAttemptsOldestFirstWithCsvEscaping`, `TranscriptNeutralisesSpreadsheetFormulasInTextCells`, `TranscriptForStudentWithNoGradedAttemptsIsHeaderOnly`, `TranscriptEnforcesStudentOwnOnlyAndInstructorAdminAnyone`, `ExportTranscriptWritesThroughTheFileRepositoryAndReportsPathProblems` | Header + one row per submitted graded attempt, oldest first, RFC 4180 quoting, formulas neutralised; header-only when none; NOT_AUTHORIZED / CONFIRMATION_REQUIRED / IO_ERROR paths | **Pass** |
| UT-ANLY-06 | Practice excluded (BR-4) | Not on leaderboard; not counted in analytics | all `*PracticeExcluded*` / `*GradedAttemptsOnly*` tests above, `TopicRankingIgnoresNewerPracticeReportsAndSurvivesRestart` | Practice attempts never appear in rankings, difficulty index, topic analytics or transcript | **Pass** |

## System / integration tests (ST-GRADE, ST-ANLY, IT)

| STP ID | Scenario | Expected result | GoogleTest(s) | Actual result | Pass/Fail |
|---|---|---|---|---|---|
| ST-GRADE-01 | Itemised result screen after a graded attempt | Every question, student's answer or "blank", correct answer, marks, total | `ResultViewIntegrationTest.ResolvesOriginalOrderOptionIndexesToReadableText`, `StillResolvesReadableTextAfterClosingAndReopeningTheDatabase`, `FallsBackToIndexOnlyLineForAQuestionThatNoLongerExists…`, `ResultView.*`, `QuizAttemptTest.StudentConsoleSubmitsAndRetrievesOnlyItsOwnResult…` | Question text + readable option text (original order) + marks + "Score: x / y"; identical after restart; deleted question falls back to index | **Pass** |
| ST-ANLY-01 | Post-attempt summary | Itemised result and weak-topic summary shown together (UC-06) | `TopicAnalyticsIntegrationTest.ResultScreenShowsTopicsVsClassAverageAndWeakestAfterRestart` | "View my result" prints the itemised result followed by "Topics (you vs class average)" and "Weakest topic: Science"; byte-identical after restart | **Pass** |
| ST-ANLY-02 | Transcript export, functional | File written | `TranscriptExportIntegrationTest.WritesRereadsRefusesSilentOverwriteAndReportsBadPath`, `StudentWithNoGradedAttemptsGetsAHeaderOnlyFile` | Real file written atomically and re-read; existing file → confirmation; overwrite rewrites; unwritable path → IO_ERROR with nothing created | **Pass** |
| ST-ANLY-03 | Admin/Instructor analytics menu | Reports displayed when at least one graded attempt exists (UC-07) | `AnalyticsModuleTest.AnalyticsConsoleShowsQuizTopicAndAllTimeLeaderboards`, `AnalyticsConsoleShowsTopicAnalyticsForAStudentToInstructors`, `AnalyticsConsoleRefusesStudents`, `StudentRankings*` | Instructor/Admin see rankings + topic analytics; Student menu is read-only rankings with own rank; Students refused from analytics | **Pass** |
| ST-ANLY-04 | Admin export of a per-student transcript (UC-08) | CSV transcript file written | `AnalyticsModuleTest.AnalyticsConsoleExportsATranscriptAndAsksBeforeOverwriting` + ST-ANLY-02 | Menu "3. Export a student's transcript (CSV)" writes the file; "no" on overwrite cancels, "yes" overwrites; failures reported | **Pass** |
| IT-01 | Grading against the real DAL and Question Bank | Correct score persisted; tamper-proof | `GradingSqliteIntegrationTest.*`, `SubmittedAttemptProtectionTest.*` (BL-43) | Real SQLite grade = 1.0 / correct items; submitted attempt immutable through every API and raw SQL, before and after restart | **Pass** |

## Summary

| Group | Tests | Pass | Fail |
|---|---|---|---|
| GradingModuleTest + GradingSqliteIntegrationTest | 23 | 23 | 0 |
| AnalyticsModuleTest + AnalyticsSqliteIntegrationTest + LeaderboardSqliteTest | 34 | 34 | 0 |
| ResultView + ResultViewIntegrationTest + TopicAnalyticsIntegrationTest | 11 | 11 | 0 |
| TranscriptExportIntegrationTest + SubmittedAttemptProtectionTest | 5 | 5 | 0 |
| **Whole project (ctest)** | **163** | **163** | **0** |

Known limitation carried over from Sprint 1 (`GradingModuleTest.PracticeModeAttemptsCannotCurrentlyBeGradedAtAll`):
the real DAL's `findOpenAttempt` only resolves Graded attempts, so practice attempts are graded in
memory by the Quiz Engine and never through `gradeAttempt` — which is also why they can never
reach the analytics tables (BR-4 by construction).
