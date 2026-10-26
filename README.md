# QuizSphere

Console-based online quiz system in C++17 (SE Mini-Project, Team 12, UE24CS341A). Instructors manage a question bank, students attempt quizzes, and administrators manage accounts.

## What works today

| Area | Status |
|------|--------|
| Accounts: registration, login, roles (Student / Instructor / Admin), lockout, failed-login log | Done, tested |
| Admin operations: enable/disable, change role, remove user (authorization enforced in code) | Done, tested (no console menu yet) |
| Question bank: add/edit/delete, enable/disable, CSV import/export, console menu | Done, tested |
| Quiz engine, grading, leaderboard, analytics, student UI | In progress |

## Build and run

Requirements: a C++17 compiler, CMake 3.20+, SQLite3 and OpenSSL development packages, and GoogleTest (fetched automatically if not installed).

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build libsqlite3-dev libssl-dev libgtest-dev cppcheck

cmake -S . -B build -G Ninja -DQUIZSPHERE_BUILD_TESTS=ON
cmake --build build
./build/quizsphere
```

On Windows, install the dependencies with vcpkg (`vcpkg install sqlite3 openssl gtest`) and pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.

On first launch there is no administrator, so the program asks you to create one. After that, use **Register** to create Student or Instructor accounts (Admin accounts can never be self-registered).

| Environment variable | Default | Purpose |
|----------------------|---------|---------|
| `QUIZSPHERE_DB` | `quizsphere.db` | SQLite user database |
| `QUIZSPHERE_AUTH_LOG` | `quizsphere_auth.log` | Failed-login log (timestamp, username, reason; never passwords) |

Both files are ignored by git. If you have a `.db` file from an older build, delete it: the schema changed and the program will tell you so.

## Tests, static analysis, coverage

```bash
ctest --test-dir build --output-on-failure
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 -I include include src
```

Coverage: configure with `-DQUIZSPHERE_COVERAGE=ON`, run `ctest`, then `gcovr -r . --object-directory build --filter 'src/' --txt`.

Everything also runs in Docker (builds, runs cppcheck, then runs the tests as a non-root user):

```bash
docker build -t quizsphere .
docker run --rm quizsphere
```

GitHub Actions (`.github/workflows/ci.yml`) runs the same build, tests, cppcheck, coverage report and Docker build on every push and pull request.

## Question bank: CSV format, limits and rules

Header (exactly, in this order):
`question_id,subject_tag,difficulty,question_type,question_text,options,correct_option,is_available`

- `difficulty`: Easy / Medium / Hard (any case). `question_type`: MCQ only.
- `options`: one cell, options separated by `|`; write `\|` for a literal `|` and `\\` for a literal `\`.
- `correct_option`: 0-based index of the right option (digits only). `is_available`: true/false/yes/no/1/0.
- Fields may be quoted (commas, doubled quotes and line breaks allowed). A UTF-8 BOM and CRLF line ends are accepted.
- Export prefixes cells that start with `=`, `+`, `-` or `@` with an apostrophe (spreadsheet formula injection);
  import removes exactly that apostrophe, so a round trip is lossless.

Import: the WHOLE file is rejected (nothing changes, `CSV_STRUCTURE_INVALID`) if it cannot be opened, is empty, is
larger than 10 MB, or has a missing/wrong header. Every other problem (wrong column count, bad quoting, validation
failure, duplicate id, bad number or flag) skips only that row and reports it with its line number (header = line 1).
All valid rows are stored with one write. Rejected rows are logged to `logs/question_import.log`
(`ISO-8601 UTC time | row N | reason`).

Limits (SRS Appendix B, counted in characters): question_id 36, subject_tag 40, question_text 500, each option 200.
Control characters are rejected everywhere except line breaks and tabs inside question_text. Subject filters ignore
ASCII case only.

Hand-offs: `QuestionRepository::isReferenced` always returns false until quizzes/attempts exist; the Quiz Engine owner must
implement it before attempts are stored. The Quiz Engine should read questions through `listAvailable` /
`getQuestionForQuiz`, which never expose the correct answer. Saving writes a temporary file and renames it over the
target (never deleting the target first); it does not fsync, so a power cut can lose the latest save.

## Security design (accounts)

- Passwords are stored as salted PBKDF2-HMAC-SHA256 (600,000 iterations, random 16-byte salt per password), compared in constant time.
- All SQL uses prepared statements; usernames are validated (3-32 chars: letters, digits, `_ - .`) and unique case-insensitively.
- 5 consecutive failed logins lock the account for 5 minutes. A wrong password never reveals whether an account exists or is disabled.
- Admin-only functions re-check the caller against the database on every call, and an admin cannot disable, demote or remove their own account.
- Failed logins are logged without passwords; text typed into the username field is never written to the log.

## Layout

```
include/   public headers (.hpp)
src/       implementation; main.cpp is the console entry point
tests/     GoogleTest suites (auth, SQLite repository, password hashing, question bank)
cmake/     build-time checks (no <fstream> outside the repository/logging layer)
```

## Team

Team 12 - see the project report for roles and responsibilities.
