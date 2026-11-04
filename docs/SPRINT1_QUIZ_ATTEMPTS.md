# Sprint 1 quiz attempts (Ashwin: BL-19, BL-20, BL-21)

Start from the merged Sprint 0 DAL, quiz creation/scoring and role menus. The branch changes the Quiz Engine, adds a presentation/navigation service, and connects two narrow console entry points. Question Bank CRUD/CSV, authentication, GradingModule and Analytics implementations are preserved.

## Interfaces and answer mapping

`QuizEngine::startAttempt` validates a Student session, loads the saved quiz/questions, shuffles question IDs, and creates or resumes an open graded attempt. A saved quiz may retain a question later disabled for new quiz pools (BR-6); disabling does not erase historical quiz content. Missing questions, duplicate IDs, malformed options/configuration and storage failures are rejected. A completed graded attempt cannot be started again under the existing interface contract.

`QuizAttemptSession` in `quiz_attempt.hpp` owns navigation and display state. It shuffles each question's options using an explicit Fisher-Yates permutation seeded from the attempt ID and question ID. The same attempt reconstructs identical option order after reopening on another standard-library implementation. Distinct attempts use independent IDs; a random shuffle may naturally repeat a permutation. `AttemptQuestionView` carries displayed text and the display-to-original mapping, without the answer key. Selecting display position N is translated to the original zero-based option index before `saveAnswer`; `kBlankAnswer` clears a response. The Question Bank records are never shuffled in place.

Shared `Attempt`, `Answer`, scoring and grading structs and existing virtual signatures are unchanged. A factory overload accepts an RNG seed for reproducible question-order tests. All new public declarations are additive and included in this PR for team review.

## Navigation and persistence

Students can move next/previous, jump by question number, toggle flags, change/clear answers, and review flagged/unanswered question numbers. Boundaries and malformed numbers are rejected. Flagging retains the selected answer. A review summary precedes explicit submission confirmation; cancellation, EOF and Back never submit.

Question order, original answers and flags persist for graded mode through the existing transactional DAL. Reopening the app resumes the same open attempt rather than creating another. Option order is reconstructed from persisted identity; no schema fields or alternative storage implementation are added. The current cursor and visited indicators are session-local and reset to the first question on resume, since the shared model has no cursor/visited fields. Answers and flags are retained.

Practice uses only in-memory attempt state. It never calls attempt-save or grading APIs; completion writes neither attempt nor report, so it cannot contribute to rankings or graded statistics. Feedback reports correct/incorrect/unanswered and identifies the correct displayed option. Answer-key access is confined to this practice-only feedback path, using the same trusted internal Question Bank boundary as the existing grading design. Graded sessions cannot request that feedback.

## Console integration

The existing role menu now routes Instructor -> Create a quiz to the existing creation API and Student -> Attempt a quiz to the new session service. Student -> View my result calls `GradingModule::getReport` for the authenticated student's ID. Question Bank and Admin menu behavior are unchanged; detailed result presentation and rankings stay with Chaturya.

Attempt commands are one-based: `n` next, `p` previous, `j N` jump, `a N` answer, `c` clear, `f` flag/unflag, `r` review, `s` submit, `0` back. Practice feedback appears after choosing/clearing an answer. Graded answers are saved after each successful answer/flag change; a failed save leaves the displayed selection unchanged.

## Grading dependency

BL-10 / PR #59 is still open as of 4 November 2026. This branch calls the existing GradingModule interface and does not copy or change Chaturya's implementation. With main's grading stub, Submit displays that grading is unavailable, retains the open attempt and its saved answers, and allows navigation/resume. A successful grading implementation must atomically finalize that same attempt and report through the DAL; the engine verifies persisted finalization before treating submission as complete.

Tests use a recording grading double with a fixed report to verify original-index handoff and result persistence/retrieval after restart. This does not claim production BL-10 integration is complete. After #59 merges, run the same Student flow with the real grader, check repeat submission and persisted report retrieval, and hand off detailed result display to Chaturya. The disputed scoring-floor rule remains a team decision; no scoring arithmetic is implemented here.

No timer, automatic submission, ranking/analytics implementation or Sprint 2 work is included. BL-22 video remains deferred until the team's Sprint 1 integration is complete.

## Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 -I include include src
docker build -t quizsphere-ashwin-sprint1 .
docker run --rm quizsphere-ashwin-sprint1
```

`tests/quiz_attempt_tests.cpp` maps UT-QE-01/ST-QE-01 to permutation/mapping tests, ST-QE-03 to navigation/review tests, ST-QE-04 to practice feedback/non-persistence, and ST-QE-05 to graded restart/mode separation. It also covers menu creation, Student flows, cancellation/EOF, authorization, invalid input, corruption, tampered state, stale submitted copies, injected save failures and the unavailable-grading boundary. Existing tests are retained.

The real CLI smoke check uses a temporary database: bootstrap/register/login, add a question, create a quiz as Instructor, practice as Student with zero stored attempts/reports, save a graded answer, compare its stored original index with the displayed option text, flag it, attempt submission against the stub, then reopen the app and verify the same option order/answer/flag and a single open attempt. No production database is used.

Verified on 4 November 2026: CMake configure/build pass; CTest 120/120 pass (98 existing registrations plus 22 new attempt/console tests); Docker build including Ubuntu cppcheck passes and container CTest passes 120/120. The temporary-database CLI smoke check passes. Targeted macOS cppcheck is clean for all changed implementation files; full macOS cppcheck 2.22 exits 1 for the existing `src/auth.cpp:131` variable-scope style finding, with that source unchanged from main. `git diff --check` passes. Final production grading/result validation remains pending BL-10; BL-22 remains deferred.
