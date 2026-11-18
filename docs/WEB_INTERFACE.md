# Local browser interface

Issue #79 · Owner Arundhathi · Optional extension approved by the repository owner on 18 November 2026.

The browser interface adds Student and Instructor screens over the existing C++17 modules. Authentication, question validation, quiz pooling, shuffled answer mapping, scoring, persistent results, graded retake prevention and deadlines remain in the shared core. The original `quizsphere` console executable remains available.

## Build and run

Use the same CMake, C++17, SQLite3, OpenSSL and GoogleTest dependencies as the console application. No Node, Python, frontend package installation or Internet connection is needed to run this UI. The frontend uses plain HTML, CSS and JavaScript embedded in the binary by CMake.

```sh
cmake -S . -B build -DQUIZSPHERE_BUILD_WEB=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Linux/macOS:

```sh
./build/quizsphere_web --port 8765 --db browser-demo.sqlite
```

Windows PowerShell with the repository's MinGW setup:

```powershell
.\build\quizsphere_web.exe --port 8765 --db browser-demo.sqlite
```

Open **http://127.0.0.1:8765/**. Keep that terminal running; stop with Ctrl+C. For multi-configuration generators the executable may be in `build/Debug` or `build/Release`. The default database is `QUIZSPHERE_DB` when set, otherwise `quizsphere.db`. Use a separate demo file while reviewing this extension. Do not concurrently run console and browser processes against the same database: the core's Question Bank holds a process-local loaded view.

To build only the original console product, set `-DQUIZSPHERE_BUILD_WEB=OFF`. A first native console setup can still create the initial Administrator; this browser version intentionally covers only the agreed Student and Instructor workflows. Public browser registration cannot create an Admin.

## Student flow

1. Create a Student account and log in. The existing username/password policies apply.
2. Choose Practice for immediate answer feedback without a persisted report, or Start graded for a saved timed attempt.
3. Answer, clear answers, jump between questions and flag questions. Displayed choices map back to original option indexes in the core.
4. Review the answered/flagged counts and confirm submission. Graded results show readable selected/correct answers and per-topic totals. Practice completion stores no result.
5. The server ticks active graded deadlines even while the browser is idle or a client is sending incomplete HTTP headers/bodies. Network waits are nonblocking, service expiry at intervals of at most 50 ms between application operations, and share one two-second absolute connection budget; new chunks do not reset it. Expiry calls the real grader with committed answers. The browser polls the current attempt once per second and shows the final result. Closing/restarting the server requires login and starting the same graded quiz to resume; the persisted original deadline still applies.
Starting practice or another quiz while a graded attempt is still open is rejected with a message to finish the graded quiz first; resuming that same graded quiz remains available. Practice is allowed again after finalization.
6. Use My result after restart. A second graded attempt is refused. Rankings support per quiz, per topic and all time; the current student's numeric identity is marked “you”.

Leaving to the quiz list keeps the current attempt active in that browser session. Logging out or session expiry releases in-memory state; an unfinished graded attempt remains persistent and is checked when resumed. Practice state is lost on logout/server restart. A page reload can use Start graded to resume; this version does not automatically reopen the attempt screen.

## Instructor flow

Create an Instructor account and log in. Question bank supports add/edit/delete and enable/disable. Enter one answer option per line and choose the correct option number. Core reference protection blocks editing/deleting questions used by a saved quiz or historical attempt. Disabling remains available.

Create quiz accepts title, positive count/duration, optional subject/difficulty filters, marks and wrong-answer penalty. Only available matching questions are pooled. The existing BR-5 per-item zero-floor behaviour is unchanged. Instructors can browse quiz and ranking views. CSV import/export, full Admin console controls and class-topic/transcript screens remain in the console/core; this first browser version does not claim to expose every console action.

## Transport and security boundary

`quizsphere_web` binds only to IPv4 loopback `127.0.0.1`. It is a small local presentation server, not a production Internet-facing service. It serves one bounded request at a time and closes each connection. Do not change the bind address or place it behind a public tunnel without a separate security/deployment review.

- Host and POST Origin must match `127.0.0.1:<port>` or `localhost:<port>`. No cross-origin permission is issued.
- Sessions use OpenSSL-generated 256-bit opaque cookie IDs, HttpOnly and SameSite=Strict. Cookies expire after 30 minutes; server sessions also expire after 30 idle minutes. Server restart invalidates all cookies.
- Authenticated POST operations require the per-session CSRF token. Current stored role/enablement is checked on requests; changed, deleted or disabled accounts lose access.
- Headers and form bodies are bounded; duplicate/malformed fields, NULs and invalid numeric values are rejected. Graded question payloads omit the answer key and original-index mapping until a completed report is requested by its own student.
- UI text is escaped; scripts/styles load only from the same local origin under a restrictive content security policy. API responses are not cached. Authentication logs go to `<database>.auth.log`; passwords/hashes are not returned.
- No TLS, public hosting, distributed sessions, background operating-system service or simultaneous process support is claimed. A local machine administrator remains able to inspect local data/processes.

## Validation

Five direct adapter integration tests cover authentication/CSRF/roles, protection of graded keys, persistence and retakes after restart, practice isolation, malformed values and protected question mutation. These use the real Auth, Question Bank, Quiz Engine, Grading and SQLite implementations.

The standard-library Python script below is an optional developer/CI check, not a runtime dependency. It starts a disposable server/database and verifies actual HTTP host/origin/CSRF checks, role denial, question/quiz creation, practice feedback/isolation, original-index mapping, manual grading, server restart, retake blocking, idle auto-submit and rankings. It ages a disposable persisted start after stopping the server to model time spent offline, then resumes against the real clock.

```sh
python3 tests/web_http_smoke.py build/quizsphere_web
```

Local Windows build and all **165 CTest entries** passed; actual HTTP smoke checks passed. Browser QA verified Student login, practice feedback/confirmation, Instructor login, question save, quiz creation, desktop layout and a 390px mobile viewport. A discovered form-ID shadowing bug was fixed and the actual save flow rechecked. Linux CI and Docker results must be attached to the PR; a local Windows pass does not imply those results.

## Scope and document changes

The original SRS §2.4/§4.1 and SAD describe a console-only product. Issue #79 is an explicitly approved owner extension, not proof of lecturer acceptance. At final integration, add the browser presentation and transport to the SRS/SAD/RTM while retaining the original 22 FR identifiers. Link browser verification as additional evidence rather than rewriting completed console evidence. Aryan's BL-36 handoff documents the pinned console baseline and this extension boundary separately.

The UI branch is based on main `95a8197d4bc23dbb9ae8642e91fc5a17c4952ae7`. It uses current main's rankings and per-attempt topic totals. Chaturya's unmerged #75/#77 class-topic/transcript changes remain independently owned; their integration should be reviewed after those PRs merge.


## PR #82 review regressions

The three blocking findings on `2a570dd` were independently reproduced against that binary before applying fixes:

| Finding | Failure before the fix | Executable regression |
| --- | --- | --- |
| Incomplete HTTP traffic stalls expiry | Dripping unfinished headers/body left SQLite `submitted=0`, `auto=0`, no report after the deadline. | `web_timer_slow-header`, `web_timer_slow-body` |
| Practice replaces the monitored graded attempt | Starting practice during an open graded attempt returned 200 and replaced the active timer. | `web_timer_practice-switch` |
| Embedded CR becomes affirmative confirmation | Actual piped `y<CR>es<LF>` finalized the quiz instead of rejecting the malformed confirmation. | `ST_SEC_09_TimedConfirmationControls` |

These run through CTest on Windows/Linux and inside Docker. HTTP probes inspect SQLite before completing the hostile request or sending any follow-up API call, so a request-triggered submission cannot hide timer starvation. They also verify the absolute connection budget and that practice works after grading. The actual-console probe rejects embedded CR/ESC/NUL while an exact `yes<CR><LF>` still submits.

No new product features were added in these fixes. Passing regressions require the reviewer to re-check the updated PR head; they do not dismiss the existing changes-requested review automatically.
