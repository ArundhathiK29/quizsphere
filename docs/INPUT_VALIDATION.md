# BL-28 input validation and negative evidence

The integration candidate adds shared bounded console input and strict validation before mutations. Rejected lines return no partial value and are drained through their newline, so their suffix cannot become another command. The failed field cancels the current action/menu without saving it. Password values are not echoed.

| Boundary | Verification |
| --- | --- |
| Oversized console lines (4096 bytes) / no command suffix injection | ST_SEC_01_BoundsDrainsAndDoesNotExecuteAnOversizedSuffix |
| NUL/ESC/control bytes, invalid/overlong UTF-8 and Unicode code-point limits | ST_SEC_02_RejectsControlsAndInvalidUtf8PreservesValidUnicode |
| Password 1024 accepted / 1025 rejected; no truncated secret | ST_SEC_03_PasswordBoundaryNeverReturnsAPartialSecret |
| Question menu malformed enum/index and oversized fields; no saved questions | ST_SEC_04_QuestionMenuRejectsMalformedAndOversizedDataBeforeStorage |
| Quiz title/subject domain validation and malformed/negative/nonfinite numeric input | ST_SEC_05_QuizCreationValidatesDomainTextAndMenuNumbers |
| Admin and analytics menus reject oversized commands | ST_SEC_06_AdminAndAnalyticsRejectOversizedCommands |
| Empty, oversized, embedded-NUL and control-character paths | ST_SEC_07_ImportExportRejectsMalformedPathsWithoutTruncation |
| Actual console auth/roles, Instructor, Student, Admin and export negatives; SQLite unchanged | ST_SEC_08_ActualConsoleValidation (tests/security_console_flow.py) |
| Timed confirmation preserves embedded controls for rejection; standard CRLF still works | ST_SEC_09_TimedConfirmationControls (tests/timed_confirmation_flow.py) |
| Browser malformed text and boolean fields cannot change availability | WebApplicationTest.InvalidTextAndBooleanFieldsNeverMutateQuestionAvailability |

Source: `src/console_input.cpp`, `src/input_validation.cpp`, `src/timed_console_input.cpp`, menu callers, quiz engine, file repository and web adapter. Timed input remains deadline-aware while draining overlong commands; it does not turn overflow into submission confirmation. Import reads are bounded to 10 MiB. The existing CSV structure/row rollback tests remain in place.

These are meaningful rejection/storage tests; no claim of exhaustive fuzzing or a formal security audit is made.

PR #82 review correction: timed pipe input previously discarded embedded CR bytes. It now strips only a final CR belonging to CRLF, rejecting embedded CR/ESC/NUL instead of turning malformed input into `yes`. Windows terminal editing keys remain editing actions; unsupported control input invalidates the line.
