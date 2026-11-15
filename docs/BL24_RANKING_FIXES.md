# BL-24 ranking fixes

Follow-up to PR #62, based on `a6d408d8190c0fd2e313310d13cbd7280af79f92`.

## Student access

After logging in as a Student, choose **3. View rankings**. The read-only menu
offers per-quiz, per-topic and all-time rankings, then **0. Back** returns to the
Student menu. No answer keys or result editing are exposed.

The existing Instructor/Admin analytics entry point still refuses Students.
The separate `runStudentRankings` entry point shares only the ranking display
implementation. New privileged analytics actions must stay outside that shared
display implementation.

## Exact graded report selection

Topic rankings already enumerate graded attempts, but their old report lookup
selected the latest report for a student/quiz. A retained legacy practice report
for the same student/quiz could therefore replace the graded topic marks.

`AttemptRepository::findReportForAttempt(attempt_id, report)` is an additive
lookup implemented by the SQLite DAL. Topic rankings use the enumerated graded
attempt's ID. The existing latest-report lookup keeps its previous behavior.
Other repository implementations remain source-compatible; their default exact
lookup explicitly returns `NOT_IMPLEMENTED` until implemented.

## Regression coverage

`tests/leaderboard_sqlite_tests.cpp` uses actual SQLite storage and the grader:

- Alice earns 4 graded Math marks and Bob earns 10 on another quiz. Adding newer
  practice reports of 99 for Alice and 0 for Bob must leave Bob first with 10 and
  Alice second with 4. A practice-only student is excluded. Repeat after restart.
- A Student can read all three ranking views after restart without exposing the
  question text or changing the stored graded result.
- Exact report lookup distinguishes graded and practice attempt IDs, reports an
  unknown ID as `ATTEMPT_NOT_FOUND`, and rejects a null output pointer.

`tests/analytics_module_tests.cpp` also checks Student menu input/empty results,
end-of-input, role boundaries and the existing Instructor/Admin ranking flow.

The fix can be merged into the `bl-24-leaderboard` branch through a follow-up PR,
then delivered to `main` through #62 after its preceding dependencies merge.
BL-24 should be closed only after the final code and evidence reach `main`.
