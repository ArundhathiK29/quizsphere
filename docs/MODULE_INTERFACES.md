# Module interfaces (BL-01)

Every module has its own header in `include/` and source in `src/`. Other modules may only use the header.
The headers are an **agreed contract**: implement the stub bodies in the `.cpp`, but change a header only through a PR
the whole team reviews. Unfinished functions return `ErrorCode::NOT_IMPLEMENTED` (see `notImplemented()` in
`common_types.hpp`) so nothing silently "succeeds".

| Module | Header / source | Owner (backlog) | Status |
|--------|-----------------|-----------------|--------|
| Auth | `auth.hpp`, `auth.cpp`, `password_hasher.*`, `auth_event_log.*`, `sqlite_user_repository.*` | Arundhathi (BL-03, 11-14, 27) | backend and Admin user console implemented in PR #82 candidate |
| Question Bank | `question_bank.hpp`, `question_bank.cpp`, `question_repository*` | Aryan (BL-05) | implemented |
| Console UI | `console_menu.hpp`, `console_menu.cpp`, `main.cpp` | Aryan (BL-04) | role menus, question bank and quiz entry points implemented |
| Quiz Engine | `quiz_engine.hpp`, `quiz_engine.cpp` | Ashwin (BL-07, 08) | creation/scoring, shuffled attempts, navigation and modes implemented; production grading handoff and runtime countdown/auto-submit implemented |
| Grading | `grading_module.hpp`, `grading_module.cpp` | Chaturya (BL-10) | real grading, report persistence and per-question floor merged; readable itemised results implemented |
| Analytics | `analytics_module.hpp`, `analytics_module.cpp` | see backlog | rankings, difficulty core API, topic summaries and transcript export implemented; dedicated Admin item-analysis display absent |
| Data Access Layer | `data_access_layer.hpp`, `data_access_layer.cpp` | Ashwin (BL-06) | users, questions, quizzes, attempts and results implemented; see DAL_PERSISTENCE.md |

Shared structs live in `quiz_types.hpp` (`Quiz`, `QuizConfig`, `ScoringScheme`, `Attempt`, `Answer`, `GradeReport`,
`ItemResult`, `TopicScore`, `LeaderboardEntry`) and `common_types.hpp` (`Role`, `Session`, `Result`, `ErrorCode`).

## Conventions everyone relies on

- **Option indexes are always the question's original index.** The Quiz Engine shuffles options for display and maps the
  student's choice back before storing an `Answer`; Grading compares with `Question::correct_option` directly.
- A skipped question is `kBlankAnswer` (-1) and scores 0, even with negative marking.
- `negative_mark` is a non-negative wrong-answer penalty. BR-5 clamps each item at zero before topic/total aggregation; the submitted #60 correction is merged.
- Timestamps are UTC ISO-8601 strings (`2026-10-25T10:30:00Z`), same as `Session::logged_in_at`.
- Modules return `Result` (value or named `ErrorCode`), never throw across module boundaries.
- Only the Data Access Layer touches SQLite or files (checked by the `no_fstream_outside_repository_or_logging` test).
- Graded: one attempt per student per quiz (`ATTEMPT_ALREADY_SUBMITTED`). Practice: repeatable, never on the leaderboard.
- Error codes from SAD 4.3: `QUIZ_NOT_FOUND`, `ATTEMPT_ALREADY_SUBMITTED`, `TIME_EXPIRED` (still graded, `auto_submitted` set).

## How to implement your stub

1. Replace the stub class in your `.cpp` (keep the factory function and the header signatures).
2. Add tests named after the test plan (`UT-QE-xx`, `UT-GRADE-xx`, ...) in a new `tests/<module>_tests.cpp` and list it in
   `CMakeLists.txt`.
3. Run `ctest` and `cppcheck` (see README), open a PR with `Closes #<issue>`, and ask a teammate to review.

Sprint 1 adds `QuizAttemptSession` (`quiz_attempt.hpp`) and narrow quiz console entry points (`quiz_console.hpp`), with no changes to the shared attempt/answer structs or existing virtual signatures. See [Sprint 1 quiz attempts](SPRINT1_QUIZ_ATTEMPTS.md) for the original-index mapping contract, practice non-persistence and grading dependency.

## Current requirements baseline

Status refreshed 19 November 2026 for the PR #82 candidate; this is not a claim that the candidate is merged. FR-017 is itemised results; FR-018 is leaderboards. See [SRS](SRS.md), [RTM](REQUIREMENTS_TRACEABILITY.md) and [deviations](REQUIREMENT_DEVIATIONS.md) for scope and acceptance exceptions. The console is retained alongside an optional loopback browser adapter around the same core. Feature and test ownership remains as recorded in the backlog.
