# Sprint 2 Quiz Engine Test Plan results

BL-38, BL-39 and BL-40. Tested implementation `4da5192` on main baseline
`b188ddb`. The supplied Team12 QuizSphere Test Plan is a non-fillable PDF.
The entries below are ready to apply to its editable source. IDs that appear
only in the traceability matrix are marked accordingly.

## Test case results

| ID | Actual Result | Pass/Fail | Execution evidence |
|---|---|---|---|
| UT-QE-01 | Repeated attempts produced different question and option orders. Every question appeared once and each displayed selection mapped to its original index. | Pass | `UT_QE_01_ShuffledQuestionsKeepEveryIdentityExactlyOnce`, `ST_QE_01_DisplayedAnswersMapToOriginalOptions` and the console smoke test |
| UT-QE-08 | A zero or negative time limit was rejected. Invalid configurations left no saved quiz. | Pass | `RejectsNonPositiveTimeLimit` and `UT_QE_03_InvalidConfigurationNeverPersistsAPartialQuiz` |
| ST-QE-02 | The countdown was visible. While waiting for confirmation with no Enter, the real console auto-submitted and stored 4/8 with one blank. | Pass | `ST_QE_02_ConsoleTimeoutAtQuestionOrSubmitConfirmation`, `IncompleteInputDoesNotBlockDeadlineAndNextLineCanBeRead` and the console smoke test |
| UT-GRADE-04 | A second graded attempt returned `ATTEMPT_ALREADY_SUBMITTED` after reopening SQLite. The original report remained intact. | Pass | `GradedResultSurvivesClosingAndReopeningTheDatabase`; console retake after process restart |
| UT-GRADE-09 | The real grader saved the report with `auto_submitted=true` and returned `TIME_EXPIRED`. A correct answer earned 4 and a blank earned 0. | Pass | `UT_QE_06_DeadlineDecreasesSurvivesRestartAndGradesOriginalChoices` and `ExpiredRestartFinalizesBeforeReadingAnAnswer` |

## Additional traceability matrix results

| ID | Actual Result | Pass/Fail | Execution evidence |
|---|---|---|---|
| UT-QE-02 | Subject and difficulty filters applied and unavailable questions were excluded. | Pass | `UT_QE_02_UT_QE_04_FilteredSelectionAndScoringSurviveRestart` and `DoesNotIncludeUnavailableQuestions` |
| UT-QE-03 | Insufficient pools and invalid configurations saved no quiz. | Pass | `RejectsInsufficientAvailableQuestions` and `UT_QE_03_InvalidConfigurationNeverPersistsAPartialQuiz` |
| UT-QE-04 | Marks and penalty survived SQLite restart. Invalid and non-finite marks and penalties were rejected. | Pass | `UT_QE_02_UT_QE_04_FilteredSelectionAndScoringSurviveRestart` and scoring validation cases |
| UT-QE-05 | A zero penalty remained disabled after restart. | Pass | `UT_QE_05_ZeroPenaltyRemainsDisabledAfterRestart` |
| UT-QE-06 | An unfinished attempt kept its original deadline, ID, question and option order, answer and flag across restart. The countdown fell from 60 to 39 seconds and resumed at 39 seconds. | Pass | `UT_QE_06_DeadlineDecreasesSurvivesRestartAndGradesOriginalChoices` and `WallClockRollbackDoesNotResetRunningDeadline` |
| UT-QE-07 | A zero or negative question count was rejected. | Pass | `RejectsNonPositiveQuestionCount` |
| ST-QE-01 | Displayed answers mapped to original indices, including through real grading. | Pass | `ST_QE_01_DisplayedAnswersMapToOriginalOptions` and the console smoke test |
| ST-QE-03 | Next, previous, jump and flagged review preserved selected answers. | Pass | `ST_QE_03_NavigationAndReviewKeepOptionsAndAnswersStable` and the console smoke test |
| ST-QE-04 | Practice gave immediate feedback and produced no attempt or report rows. | Pass | `ST_QE_04_PracticeFeedbackAndCompletionNeverPersist` and the console smoke test |
| ST-QE-05 | Graded resume kept the same ID, question and option order, answers and flags. | Pass | `ST_QE_05_GradedResumeKeepsQuestionOrderOptionsAnswersAndFlags` and the console smoke test |
| ST-QE-06 | Concurrent creation admitted one open attempt. Finalizing it while another connection tried a retake never opened a second one. Legacy ambiguity was rejected without changing stored attempts. | Pass | `ConcurrentOpenGradedWritesHaveExactlyOneWinner`, `ST_QE_06_ConcurrentFinalizationAndNewAttemptCannotAllowARetake`, `AmbiguousLegacyOpenRowsArePreservedAndRejectedOnLookup` |
| REL-01 | A timeout graded through the real grader. An injected write failure left the answers intact and no report. Once storage recovered, reopening retried the expired attempt successfully. Concurrent manual and timeout submission finalized once. | Pass | `REL_01_FailedTimeoutGradeLocksAnswersAndCanRetryAfterRestart` and `ManualAndTimeoutOnSeparateConnectionsFinalizeOnce` |

## Environment and execution

- macOS arm64, AppleClang 16.0, native SQLite 3.43.2 and GoogleTest 1.14.0.
  Fresh configure, build and CTest passed **161/161**. Focused SQLite, timer
  and locking CTest passed **13/13**. The two race cases passed 25 runs each.
- Ubuntu 24.04 in Docker, GCC 13, repository Dockerfile. Build and cppcheck
  passed. CTest passed **161/161** in both the standard and coverage builds.
- The console smoke test used disposable accounts and SQLite. Before one
  restart, it aged the stored start to represent time spent offline. The
  restarted executable then waited on its real clock with stdin open and
  received no input during expiry. The separate unit test used an injected
  clock to verify restart against the unchanged stored start.
- The three submitted-attempt protection tests from PR #74 passed against this
  branch's core library in isolation.
- The Windows timer and input sources cross-compiled with MinGW-w64 GCC 13.2
  and warnings as errors. Windows console runtime remains unverified.

[Console smoke results](evidence/quiz-engine-sprint2/console-smoke.txt) ·
[Raw gcovr reports](evidence/quiz-engine-sprint2/coverage.txt)

## Coverage

The Ubuntu coverage build used GCC 13 and gcovr 7.0. The reports include
production `src/` files from CTest. Test, dependency and header files are
outside that filter. GCC branch counts include C++ exception paths.

| Scope | Lines executed | Branches taken |
|---|---:|---:|
| All production sources | 2,924 / 3,484 (83.9%) | 3,642 / 7,590 (48.0%) |
| Quiz engine | 287 / 301 (95.3%) | 427 / 742 (57.5%) |
| Attempt session | 143 / 145 (98.6%) | 113 / 216 (52.3%) |
| Quiz console | 163 / 170 (95.9%) | 354 / 658 (53.8%) |
| Timed console input on Linux | 25 / 25 (100%) | 26 / 44 (59.1%) |
| SQLite DAL | 593 / 696 (85.2%) | 785 / 1,883 (41.7%) |

The CTest coverage run did not launch `main.cpp`. The console smoke evidence
covers the executable separately. Other gaps include console read errors,
rare SQLite prepare and commit failures, and Windows-specific input.

```sh
cmake -S . -B build-coverage -G Ninja -DQUIZSPHERE_BUILD_TESTS=ON -DQUIZSPHERE_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage --output-on-failure
gcovr -r . --object-directory build-coverage --filter 'src/' --txt
gcovr -r . --object-directory build-coverage --filter 'src/' --txt-metric branch --txt
```
