# Test Plan addendum: authentication actual results (BL-29)

This is the authentication Actual Result / Pass–Fail addendum to Section 13.1 and the auth RTM rows of `docs/Team12_SE_QuizSphere.pdf`. It records unit/module execution separately from pending system/UI tests; it does not replace the entire project Test Plan.

**Executed:** 10 November 2026, Windows, fresh GCC 15.1.0 coverage build. Base main: `8615120cf193294786b49fbe39a63f13e55b9864`, plus the new boundary test in this PR. **40/40 tests passed; 0 skips; process exit 0.** [Raw results](evidence/auth-coverage-2026-11-10/auth-tests.txt).

| Test Plan ID | Expected result / unit scope | Actual result | Status and exact test evidence |
|---|---|---|---|
| UT-AUTH-01 | Correct login returns identity/role and a session with UTC time | Login returned the stored Instructor ID and role; session conversion preserved identity/role and generated UTC timestamp | PASS — `Auth.UT_AUTH_01_ValidLoginReturnsIdAndRole`; `Auth.UT_AUTH_17_SessionCarriesIdentityRoleAndUtcTimestamp` |
| UT-AUTH-02 | Student and Instructor registration succeeds (FR-001) | Both permitted roles registered and returned the requested role | PASS — `Auth.UT_AUTH_02_RegistersStudentAndInstructor` |
| UT-AUTH-03 | Duplicate username is rejected | Duplicate registration, including case-only differences, returned UsernameAlreadyExists | PASS — `Auth.UT_AUTH_03_DuplicateUsernameIsRejectedCaseInsensitively`; `SqliteUserRepository.UT_AUTH_03_UsernamesAreUniqueIgnoringCase` |
| UT-AUTH-04 | Wrong password rejected; failure logged with timestamp and no password | Wrong password and unknown user returned InvalidCredentials; failed-login events carried timestamp/reason and excluded the password | PASS — `Auth.UT_AUTH_04_WrongPasswordAndUnknownUserBothReturnInvalidCredentials`; `Auth.UT_AUTH_16_FailedLoginsAreLoggedWithTimestampButNeverThePassword`; AuthEventLog suite |
| UT-AUTH-05 | Only a derived password value stored; no plaintext | Encoded salted PBKDF2-HMAC-SHA256 stored; production SQLite test verified the 600000-iteration prefix; correct/wrong verification and distinct salts passed | PASS for implemented security behavior; **specification deviation** — the PDF says plain SHA-256. `Auth.RegistrationNeverStoresThePlaintextPassword`; `PasswordHasher.UT_AUTH_05_EncodedFormatIsSaltedPbkdf2`; `PasswordHasher.SameSecretGetsDifferentSalts`; `SqliteUserRepository.RegistrationAndLoginWorkEndToEnd` |
| UT-AUTH-06 | Disabled account cannot log in; re-enabling restores login | Correct password on disabled account returned AccountDisabled; enabled account logged in again | PASS — `Auth.UT_AUTH_06_DisabledAccountIsRefusedOnlyAfterTheCorrectPassword`; `Auth.UT_AUTH_08_AdminManagesUsers`; `SqliteUserRepository.AdminLifecycleThroughTheRealRepository` |
| UT-AUTH-07 | Student denied at an Instructor-only function, independent of UI | Direct Student add/edit/delete/availability calls returned NOT_AUTHORIZED; stored question remained intact. Direct Admin operations also rejected Student/forged/disabled actors | PASS — **new** `AuthRoleBoundary.UT_AUTH_07_StudentCannotCallInstructorQuestionBankFunctions`; `Auth.UT_AUTH_07_AdminFunctionsDenyNonAdminsEvenWhenCalledDirectly` |
| UT-AUTH-08 | Admin module creates users, changes roles, disables/re-enables and removes users; non-admin denied | Module lifecycle and real SQLite repository checks passed; self-lockout actions rejected | PASS for unit/module scope — `Auth.UT_AUTH_08_AdminManagesUsers`; `Auth.UT_AUTH_08c_AdminCreatesUsersOfAnyRoleAndOthersCannot`; `Auth.UT_AUTH_08b_AdminCannotLockThemselvesOut`; `SqliteUserRepository.AdminLifecycleThroughTheRealRepository` |
| UT-AUTH-09 | Self-registration as Admin rejected; Student/Instructor only | Admin registration denied and no unauthorized account created | PASS — `Auth.UT_AUTH_09_SelfRegistrationAsAdminIsRejected` |
| UT-AUTH-08 UI clause / ST-ADM-01 | Admin console supports the lifecycle and removal requires confirmation | Not executed by this auth unit run; Admin user-management menu remains separate BL-27 work | **PENDING — BL-27**, not included in the unit PASS above |

The PDF's sample UT-AUTH-08 row combines module lifecycle with a UI confirmation requirement. Its module result and pending UI clause are separated explicitly above; no complete UC-09/UI pass is asserted. UT-AUTH-02/03 are mapped from the PDF's FR-001 traceability row and existing registration test names.

Additional passing tests cover malformed/oversized credentials, password byte handling, first-Admin bootstrap, account lockout/reset, storage/schema failures, prepared-statement handling, repository reopen persistence and safe log redaction. The full output names all executed tests.

## Coverage actual result

[Coverage report](AUTH_COVERAGE.md): **440/474 lines (92.83%)**, **436/690 GCC-reported branches taken (63.19%)** across auth.cpp, password_hasher.cpp, auth_event_log.cpp and sqlite_user_repository.cpp. No coverage threshold is specified by BL-29. The report lists unexecuted locations and includes exception/compiler-generated branches.

The older PDF is not regenerated in this PR. This linked addendum supplies BL-29's actual results; BL-30 remains responsible for final SRS/RTM wording and document alignment.
