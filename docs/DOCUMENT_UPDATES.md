# Document updates needed to match the code

The PDF (`Team12_SE_QuizSphere.pdf`, SRS v1.0.1 + SAD v1.1 + STP) describes an earlier design. The code now differs in a few places (mostly for the better), and the Test Plan is behind the SRS/SAD. Apply these edits in the source of the documents, bump the versions, and regenerate the PDF.

Suggested versions: **SRS 1.0.2, SAD 1.2, STP 1.1**, all dated the same day.

## 1. SRS 1.0.1 -> 1.0.2

**FR-003 (replace)**
> The system shall store every password only as a salted PBKDF2-HMAC-SHA256 hash (at least 600,000 iterations, unique random 16-byte salt per password) and never in plaintext; verification shall use a constant-time comparison.

**New requirements** (renumber if you prefer; the IDs below are the ones used in the test table in section 3)

| ID | Requirement | Priority | Verification |
|----|-------------|----------|--------------|
| FR-023 | The system shall let an Administrator create a user of any role, change a user's role, and remove a user. An Administrator shall not be able to disable, demote or remove their own account. | Medium | Functional / Authorization test |
| FR-024 | On first run, when no Administrator exists, the system shall let the operator create the first Administrator. Administrator accounts shall otherwise only be created by an existing Administrator (never by self-registration, FR-001). | High | Functional / Negative test |
| FR-025 | Usernames shall be 3-32 characters (letters, digits, `_`, `-`, `.`) and unique ignoring case; passwords shall be 8-1024 characters. | High | Unit / Negative test |
| FR-026 | After 5 consecutive failed logins an account shall be locked for 5 minutes (ACCOUNT_LOCKED login error); a successful login resets the counter. | Medium | Unit / Negative test |

**SEC-006 (append):** "Text entered in the username field is only logged if it is a well-formed username; otherwise it is logged as `<invalid>`."

**New SEC-007:** "Administrator-only operations shall verify the caller's role against stored data on every call, not only in the UI." (verification: authorization test)

**Section 9 Business Rules:** list all 7 rules with IDs BR-1 to BR-7 (BR-7 = only an Administrator can disable a user account). The Test Plan currently stops at BR-6.

**Section 2.6 Dependencies:** add "SQLite3 and OpenSSL (libcrypto) development libraries".

**Appendix B:** add the `question_type` and `options` fields that FR-004/FR-005 need, and a `users` table: `id`, `username` (unique, case-insensitive), `password_hash`, `role` (Student/Instructor/Admin), `enabled`, `failed_attempts`, `locked_until`.

**Revision history:** add a 1.0.2 row describing the above.

## 2. SAD 1.1 -> 1.2

- **Every "SHA-256" mention** (Section 1.2 item 1, Section 3.4 Auth Module bullet, the Information Disclosure row of the threat table, and the Section 3.8 traceability row for FR-003): replace with "salted PBKDF2-HMAC-SHA256".
- **"the SRS's 21 individually numbered functional requirements"** (Section 3.7/3.8 and the v1.1 revision note): now 26.
- **Auth Module file names**: replace `auth_module.*` (also in the FR-022 traceability row) with `auth.*`, `password_hasher.*`, `auth_event_log.*`, `sqlite_user_repository.*`.
- **Auth Module interface**: errors now include `ACCOUNT_LOCKED` and `PERMISSION_DENIED`; functions are `registerUser`, `bootstrapAdmin`, `loginUser`, `makeSession`, `createUserAsAdmin`, `setAccountEnabled`, `setUserRole`, `removeUser`. `Session` and `Role` are the shared types in `common_types.hpp`.
- **Technology stack**: add OpenSSL (libcrypto) next to SQLite3; build is CMake + Ninja, CI is GitHub Actions (build, ctest, cppcheck, gcovr coverage, Docker build).
- **Revision history**: v1.0 and v1.1 currently carry the same date (01-10-2026) while the document date is 02-10-2026; give each its own date.
- **Layout**: widen the first column of the traceability and NFR tables so "FR-00 / 2", "FR-02 / 2" and "Maintainabilit / y" no longer wrap, and widen "Version" in the revision tables.

## 3. Test Plan 1.0 -> 1.1

- **Header and references**: "aligned with SRS v1.0.2 and SAD v1.2" (currently "v1.0" for both).
- **Delete** the assumption "SAD error code ACCOUNT_DISABLED has no matching SRS requirement yet" (FR-022 exists).
- **Features table / RTM**: add FR-022 to FR-026 (FR-022 is missing from both), and change "BR-1 to BR-6" to "BR-1 to BR-7".
- **Out-of-scope list**: "any third-party SHA-256 implementation" -> "OpenSSL's PBKDF2 implementation".
- **Coverage tool**: "gcov/lcov" -> "gcov via gcovr".
- **Environment**: add OpenSSL to the software list.
- **UT-AUTH-05 expected result** -> "Stored value has the form `pbkdf2-sha256$600000$<salt>$<hash>`; no plaintext."
- **UT-AUTH-06**: remove "(SAD design behaviour)"; it traces to FR-022.
- **UT-AUTH-08 steps** -> "Admin creates a user, changes a role, removes a user" (the removal confirmation is a console-menu step, not part of the Auth Module).
- **New rows** (names are the actual GoogleTest names in `tests/auth_tests.cpp`, so the plan and CI output match):

| ID | Scenario | Expected result | Traces to |
|----|----------|-----------------|-----------|
| UT-AUTH-10 | First Admin bootstrap | First call succeeds; second returns ADMIN_ALREADY_EXISTS | FR-024 |
| UT-AUTH-11 | Password policy | < 8 and > 1024 characters rejected | FR-025 |
| UT-AUTH-12 | Username validation | Spaces, quotes, `;`, non-ASCII, length 2 and 33 rejected | FR-025, SEC-004 |
| UT-AUTH-13 | Lockout | After N failures even the correct password gives LOCKED; works again after the lock time | FR-026 |
| UT-AUTH-14 | Counter reset | A success between failures resets the count | FR-026 |
| UT-AUTH-15 | Storage failure | Storage errors are returned, never reported as invalid credentials | NFR-003 |
| UT-AUTH-16 | Failed-login log | Timestamp + username + reason; never the password; free text becomes `<invalid>` | SEC-006 |
| UT-AUTH-17 | Session contents | user_id, role, UTC logged_in_at | FR-002 |
| UT-AUTH-08b | Admin cannot lock themselves out | Self disable/demote/remove denied | FR-023 |
| UT-AUTH-08c | Admin creates users | Admin can create any role; non-admin and forged-admin sessions denied | FR-023, SEC-003 |
| UT-AUTH-07 (extend) | Direct privilege call | Also covers forged Admin session, unknown/malformed actor id, disabled admin | SEC-003 |

Repository tests (`tests/sqlite_user_repository_tests.cpp`) additionally cover case-insensitive uniqueness (UT-AUTH-03), SQL-injection strings stored/queried as data (SEC-004), persistence across reopening, 64-bit lock timestamps, and refusal of an old-schema database.

## 4. What the code does that the documents should now say

- Login failure for a wrong password and for an unknown user is indistinguishable, including in timing; a disabled account is only revealed after the correct password.
- The Admin check re-reads the caller from the database, so a stale or forged `Session` cannot be used for admin actions.
- `question_bank_tests.cpp` had three assertions that contradicted the module's own validation rules (single-line tags/options, trimming) and one that could not pass as root; these were fixed in the integration branch, not the question-bank code.
