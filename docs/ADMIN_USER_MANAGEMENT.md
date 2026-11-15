# Admin user management — BL-27

Log in as an Admin and choose **2. Manage users**.

| Choice | Action |
| --- | --- |
| 1 | Find an account by username; show its role and enabled/disabled status. |
| 2 | Create a Student, Instructor or Admin account with a repeated password. |
| 3 | Change an account's role. The new role is used on its next login. |
| 4 | Enable or disable an account. Disabled accounts are refused login. |
| 5 | Remove an account after explicit confirmation. |
| 0 | Return to the Admin menu. |

The target username is case-insensitive. Blank usernames and role/status choice
0 cancel that action. Every account change previews the target/action and asks
for **YES**; other answers cancel. End-of-input exits without applying an
unconfirmed change.

Usernames use the existing 3-32 character rule: letters, digits, `_`, `-`, `.`.
Passwords use the existing 8-1024 byte policy, must match their confirmation,
and are stored through the existing salted PBKDF2 password hasher. Password
input preserves spaces and does not print the secret or stored hash. Real
terminal input uses the same echo suppression as login and registration.

## Authorization and account protection

- Both the menu and the underlying account operations enforce SEC-003. The
  actor must be an enabled Admin in current storage, not just claim an Admin
  role in a session. A disabled, demoted, deleted, malformed or forged actor is
  denied; a storage failure is reported as an error.
- Student and Instructor menus do not expose user management. Direct calls to
  the management menu are also denied for non-admins.
- The signed-in Admin cannot change their own role/status or remove their own
  account. Existing auth safeguards perform those checks before a write.
- New Admins can be created through this authenticated menu; public registration
  remains limited to Student and Instructor roles.
- Account storage stays behind `IUserRepository`/the auth functions. The menu
  does not access SQLite or files directly.

## Requirement and test mapping

| Requirement/test | Evidence |
| --- | --- |
| UC-09 / BL-27 | Account lookup, creation, role/status changes and confirmed removal through the Admin console. |
| SEC-003 / UT-AUTH-08 | Existing auth mutation tests, current stored Admin access check, forged/non-admin/revoked-session menu tests. |
| FR-022 | Disable/enable menu tests assert refused/restored login. |
| UT-AUTH-08 confirmation | Creation/role/status/removal cancellation, EOF and self-protection tests. |
| ST-ADM-01 | Automated actual CLI flow across seven process launches: create, role changes, disable/enable, Student/Instructor boundaries, cancellation, removal and persistence. |
| SEC-001 | Stream tests and menu tests check that passwords/hashes are not echoed; existing password hashing tests remain in the suite. |

Run the focused tests after building:

```bash
ctest --test-dir build -R 'ConsoleInput|AdminEntryAccess|AdminUserMenu|ST_ADM_01' --output-on-failure
```

Run the full project suite with `ctest --test-dir build --output-on-failure`.
The CLI system test uses disposable accounts in the build directory and writes
its console transcripts there. It removes its temporary input files after each
process. No real user database is used by the tests.

BL-27 is ready for closure only after teammate review, successful checks and
merging the implementation into `main`.
