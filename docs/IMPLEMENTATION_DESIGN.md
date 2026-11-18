# Architecture and implementation design cross-reference

This BL-30 supplement defines the aliases used in the RTM. It connects the
retained SAD v1.1 to actual interfaces/files; it does not claim that pending
code is in `main` or replace Aryan's BL-36 SAD/diagram revision.

The SAD's four layers remain Presentation, Business Logic, Data Access and
Data (sections 3.4-3.5, PDF pp. 12-13). Implementation uses injected repository
interfaces/factories and concrete classes behind headers, rather than requiring
every operation to be a plain free function. See [deviations](REQUIREMENT_DEVIATIONS.md).

## A-AUTH

Auth business logic plus the account repository implements registration,
authentication, role-bearing sessions and Admin account operations. SAD 3.4
Auth/DAL and 3.9 security, with Login interface in SAD 4.3.

## D-AUTH

[`auth.hpp`](../include/auth.hpp) exposes `registerUser`, `bootstrapAdmin`,
`loginUser`, `makeSession`, `createUserAsAdmin`, `setAccountEnabled`, `setUserRole`
and `removeUser`. [`password_hasher.hpp`](../include/password_hasher.hpp)
encodes/verifies salted PBKDF2-HMAC-SHA256. [`user_repository.hpp`](../include/user_repository.hpp)
is the account storage boundary; [`sqlite_user_repository.cpp`](../src/sqlite_user_repository.cpp)
implements prepared queries, case-insensitive uniqueness and persistence.

Admin mutations re-read the actor from storage; they do not trust a claimed
Admin role alone. Login distinguishes auth statuses from the shared domain
`ErrorCode` enum. [PR #71](https://github.com/ArundhathiK29/quizsphere/pull/71)
adds `checkAdminAccess` and the protected account menu. That addition is pending
at the pinned baseline. Passwords/hashes are excluded from account/menu logs.

## A-QB

Question Bank business logic handles tagged MCQ lifecycle, validation,
availability and CSV exchange through repositories/logging interfaces. SAD 3.4
Question Bank/DAL; UC-02/03, common design boundaries in SAD 4.1 and 4.4-4.5.

## D-QB

[`question_bank.hpp`](../include/question_bank.hpp),
[`question_types.hpp`](../include/question_types.hpp) and
[`question_repository.hpp`](../include/question_repository.hpp) define CRUD,
filtering, answer-free public views, import/export and reference protection.
The SQLite factory is implemented in [`data_access_layer.cpp`](../src/data_access_layer.cpp);
the legacy file factory is in [`question_repository_file.cpp`](../src/question_repository_file.cpp).
`question_bank.cpp` validates data before persisting it and classifies whole-file
versus row-level CSV failures. `options_codec.hpp` defines reversible option
escaping. `import_logger.cpp` records rejected-row metadata through its interface.

## A-QUIZ

Quiz Engine handles pooling/configuration, question/option shuffling,
practice/graded attempts, navigation and submission handoff. SAD 3.4 Quiz Engine
and DAL; SAD 4.2 Attempt Quiz/Create Quiz sequences, PDF pp. 15-16.

## D-QUIZ

[`quiz_engine.hpp`](../include/quiz_engine.hpp) defines creation, retrieval,
attempt start, answer save and submission. [`quiz_attempt.hpp`](../include/quiz_attempt.hpp)
defines Student-facing navigation/review/feedback. Display option indexes map
back to original indexes before answers enter storage or grading; a blank is
`kBlankAnswer = -1`. Practice completes without persisted graded results.
The real DAL supplies graded restart/single-open/submission safeguards.

`submitAttempt(..., timed_out=true, ...)` explicitly returns NOT_IMPLEMENTED at
the pinned main revision. Persisted start time and grading's auto-submission
flag are scaffolding, not a working clock/countdown/automatic submission flow.
FR-011/NFR-003 remain tracked under BL-38.

## A-GRADE

Grading applies configured scoring and creates persisted item/topic reports.
SAD 3.4 Grading/DAL, 4.2 Attempt Quiz and 4.3 GradeAttempt, PDF pp. 15-16.

## D-GRADE

[`grading_module.hpp`](../include/grading_module.hpp) defines `gradeAttempt`
and `getReport`; [`quiz_types.hpp`](../include/quiz_types.hpp) supplies
`ScoringScheme`, `Attempt`, `Answer`, `ItemResult`, `TopicScore`, `GradeReport`.
The grader validates answer identity/range, uses original answer indexes,
applies BR-5 per item, aggregates topics/score, and atomically finalizes through
`AttemptRepository::saveGradedAttempt`. Repeated submission and storage errors
remain distinct. A `TIME_EXPIRED` report/flag is supported by the grader but
does not implement the missing timer in the engine/UI.

Readable result rendering is implemented in pending
[PR #61](https://github.com/ArundhathiK29/quizsphere/pull/61). The formatter resolves
question/original-option text for the Student view; the baseline UI prints only
the total score. The per-attempt topic totals already in GradeReport do not
alone satisfy FR-020's Student/class averages and visible weak-topic summary.

## A-ANALYTICS

Analytics consumes graded attempts/reports through repositories to rank,
compute difficulty/topic statistics and export transcripts. SAD 3.4 Analytics/DAL
and 4.2 post-attempt sequence; UC-06/07/08.

## D-ANALYTICS

[`analytics_module.hpp`](../include/analytics_module.hpp) declares ranking,
difficulty, topic breakdown and transcript export. Its baseline implementation
is a stub, with explicit NOT_IMPLEMENTED results. Pending PRs #62/#70 add
per-quiz/topic/all-time rankings and Student read-only access; #63 adds graded-only
difficulty calculation. Rank ties use competition ranking (1, 1, 3), with scores
descending. Topic ranking must resolve the report for the exact graded attempt,
not a newer practice report for the same Student/quiz.

FR-020 topic averages/weak topics (BL-41) and FR-021 CSV result transcripts
(BL-42) remain separate unimplemented analytics work. Question CSV export is
not a result transcript. No passing stub/interface test is counted as feature
acceptance for either requirement.

## A-DAL

The Data Access layer owns account/question/quiz/attempt/report persistence;
business modules call interfaces instead of SQL/files directly. SAD 3.4-3.6,
component diagram in SAD 3.3 and shared-struct design in SAD 4.1.

## D-DAL

[`data_access_layer.hpp`](../include/data_access_layer.hpp) and the question/user
repository headers define storage contracts. SQLite normalizes question options,
quiz questions, attempt order/answers, reports/items/topics. Transactions,
indexes, integrity checks and immutable-submission triggers protect multi-table
writes. Referenced question content is protected even when only historical
attempts retain it; availability can change. See [DAL persistence](DAL_PERSISTENCE.md).

The `no_fstream_outside_repository_or_logging` check is one architectural guard;
it detects disallowed stream includes/selected file operations, not every
possible OS-level storage call. Keep the source/dependency review alongside it.

## A-UI

Role menus and narrow console entry points form the presentation layer. SAD 4.5
requires clear errors, confirmation and a countdown; missing UI features remain
visible in the RTM rather than being inferred from backend methods.

## D-UI

[`main.cpp`](../src/main.cpp) authenticates and dispatches roles,
[`console_menu.cpp`](../src/console_menu.cpp) handles questions, and
[`quiz_console.cpp`](../src/quiz_console.cpp) handles quiz creation/attempt/result
entry points. Pending #61 adds readable results, #70 adds Student rankings, and
#71 adds Admin account management/shared password input. Each must retain direct
function authorization as well as menu visibility. BL-28 covers cross-menu
negative validation; BL-33 supplies the actual usability walkthrough.

## A-BUILD

Separate core/library/test executables support testability and repeatable builds.
SAD 3.6 technology stack and 4.4 CI monitoring.

## D-BUILD

[`CMakeLists.txt`](../CMakeLists.txt), [`CI`](../.github/workflows/ci.yml) and
[`Dockerfile`](../Dockerfile) configure C++17, SQLite/OpenSSL, GoogleTest,
warnings-as-errors, cppcheck, coverage and container tests. CI covers only its
registered executable tests at the associated revision. The base image digest
is pinned on main; package-version locking and offline dependency provisioning
are separate concerns, not assumed properties of that digest.
