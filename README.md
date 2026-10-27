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
