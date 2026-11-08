# Authentication verification — Sprint 1

Owner: Arundhathi K  
Execution date: 2026-11-03 (Asia/Calcutta)  
Tested commit: `978a25fde52c368e156186804e95055825ee8c1d`

## Result and scope

**PASS: 39 tests from four suites; 0 failed, 0 skipped; process exit code 0.**

A fresh auth_tests target was configured and compiled on Windows from the pinned main snapshot, including the current quizsphere_core library. All 52 downloaded text files matched the commit’s Git blob hashes before the test run. No product code was changed.

Suites: `Auth` (19), `PasswordHasher` (6), `AuthEventLog` (4), `SqliteUserRepository` (10). Full output: [auth test log](evidence/auth-tests-2026-11-03.txt).

This verifies existing authentication/account behavior for BL-11–BL-14 and related registration tests. It does not claim completion of the console Admin menu, cross-module quiz authorization, all ST-SEC system tests, or the full product test plan. Those need their own integration evidence.

## Backlog-to-test mapping

All named tests below passed in the recorded run.

| Issue / requirement | Behavior verified | Exact GoogleTest evidence |
|---|---|---|
| [BL-11](https://github.com/ArundhathiK29/quizsphere/issues/11), FR-002 | Successful login returns the stored identity and role; wrong password and unknown user return InvalidCredentials | `Auth.UT_AUTH_01_ValidLoginReturnsIdAndRole`; `Auth.UT_AUTH_04_WrongPasswordAndUnknownUserBothReturnInvalidCredentials` |
| BL-11, FR-003 / SEC-001 | Salted PBKDF2 encoding, distinct salts, password verification, and production-format storage through SQLite | `PasswordHasher.UT_AUTH_05_EncodedFormatIsSaltedPbkdf2`; `PasswordHasher.SameSecretGetsDifferentSalts`; `PasswordHasher.RejectsWrongPassword`; `SqliteUserRepository.RegistrationAndLoginWorkEndToEnd` |
| [BL-12](https://github.com/ArundhathiK29/quizsphere/issues/12), SEC-003 | Direct Admin-operation calls reject Student actors, a forged Admin role for a stored Student, malformed/unknown actor IDs and disabled Admins | `Auth.UT_AUTH_07_AdminFunctionsDenyNonAdminsEvenWhenCalledDirectly`; `Auth.UT_AUTH_08c_AdminCreatesUsersOfAnyRoleAndOthersCannot` |
| [BL-13](https://github.com/ArundhathiK29/quizsphere/issues/13), FR-022 | Disabled account is refused after the correct password; re-enabling restores login | `Auth.UT_AUTH_06_DisabledAccountIsRefusedOnlyAfterTheCorrectPassword`; `Auth.UT_AUTH_08_AdminManagesUsers` |
| BL-13, real repository | Disable, role change and removal operate through SQLite | `SqliteUserRepository.AdminLifecycleThroughTheRealRepository` |
| [BL-14](https://github.com/ArundhathiK29/quizsphere/issues/14), SEC-006 | Failed-login event carries timestamp/username/reason; password argument is not logged; invalid username input is redacted; file logging appends | `Auth.UT_AUTH_16_FailedLoginsAreLoggedWithTimestampButNeverThePassword`; `AuthEventLog.UT_AUTH_16_WritesTimestampedLineWithoutAnyPassword`; `AuthEventLog.NeverWritesFreeFormTextFromTheUsernameField`; `AuthEventLog.AppendsRatherThanOverwrites` |

BL-12 also names SEC-002 and broader system/security cases. This auth-only run establishes Admin account-operation checks; it does not independently verify every Question Bank/Quiz Engine operation.

Additional passing tests cover registration role restrictions, case-insensitive username uniqueness, username/password validation, first-Admin bootstrap, lockout/counter reset, self-lockout protection, UTC session fields, repository persistence across reopening and handling of storage/schema errors. The full log lists every executed test.

## Implementation facts

- Password storage uses **PBKDF2-HMAC-SHA256**, not a single unsalted SHA-256 digest. Production hashing defaults to 600,000 iterations, a random 16-byte salt and a 32-byte derived key.
- Stored format: `pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>`. Verification compares derived key bytes using OpenSSL's constant-time comparison.
- Hashing unit tests use a lower allowed iteration count for speed. `SqliteUserRepository.RegistrationAndLoginWorkEndToEnd` separately checks the production `pbkdf2-sha256$600000$` prefix.
- Self-registration permits Student/Instructor only. `bootstrapAdmin` creates the first Admin; subsequent Admin creation is restricted to an existing Admin.
- Default login policy locks an account for 300 seconds after five consecutive failures; successful login resets the counter. Unit tests inject time rather than waiting five minutes.
- Admin account operations re-read the actor from storage; the caller must still be an enabled Admin. Self-disable, role change and removal are denied.
- Failed-login logs record UTC timestamp, username and reason. Well-formed username values may be logged; invalid username input becomes `<invalid>`. The logger cannot identify a secret accidentally entered into the username field if that string also satisfies the username format.
- SQLite user queries use prepared statements. Repository tests cover case-insensitive uniqueness, SQL-like strings as data, and reopening persisted data.

Implementation: [auth.hpp](../include/auth.hpp), [auth.cpp](../src/auth.cpp), [password_hasher.cpp](../src/password_hasher.cpp), [auth_event_log.cpp](../src/auth_event_log.cpp), [sqlite_user_repository.cpp](../src/sqlite_user_repository.cpp).

## Reproduce the auth run

Configure/build the project using the README instructions, then run only the auth suites:

```powershell
.\build\auth_tests.exe "--gtest_filter=Auth.*:PasswordHasher.*:AuthEventLog.*:SqliteUserRepository.*"
```

Linux equivalent:

```bash
./build/auth_tests --gtest_filter='Auth.*:PasswordHasher.*:AuthEventLog.*:SqliteUserRepository.*'
```

The recorded fresh Windows build used GCC 15.1.0, SQLite 3.50.3, OpenSSL 3.5.1 and the installed GoogleTest package. Its executable was `work/auth-2026-11-03/build/auth_tests.exe` in the review workspace; the executable/source pairing is the pinned commit above. Configuration and the auth_tests build both exited successfully. See [build and source evidence](evidence/auth-build-2026-11-03.txt). TEMP/TMP pointed to an isolated writable review test directory. Tests use fake repositories or dedicated SQLite test fixtures, rather than the application's user database. Only the test output is committed; no database, account dump, password or generated application log is included.

## Documentation alignment

The README already describes PBKDF2 correctly. The older combined SRS/SAD/Test Plan PDF still contains plain SHA-256 descriptions. [DOCUMENT_UPDATES.md](DOCUMENT_UPDATES.md) proposes the corresponding document-source changes. This verification document records actual implemented behavior; it does not claim that the PDF has been regenerated or that those proposed requirement revisions have been approved.
