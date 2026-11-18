# QuizSphere Software Requirements Specification

**Version:** 1.0.2 integration draft, 15 November 2026. **Authors:** Team 12.
**Status:** proposed implementation-aligned revision for teammate review.

This editable revision retains the scope and IDs of SRS v1.0.1 in the
[source PDF](Team12_SE_QuizSphere.pdf). It replaces that revision's incomplete
sample RTM with the [full traceability matrix](REQUIREMENTS_TRACEABILITY.md).
The [baseline](REQUIREMENTS_BASELINE.md) identifies the exact code/documents
reviewed. Requirements below describe required behavior; their presence here
does not assert that every feature is implemented. See the matrix for status.

## 1. Purpose and scope

QuizSphere is a local C++17 console quiz assessment and analytics application.
Students practise or take graded quizzes; Instructors manage questions and
quizzes; Admins manage accounts and can perform permitted oversight actions.
The committed scope includes account authentication, tagged MCQ storage,
question CSV import/export, configurable quizzes, shuffled timed attempts,
grading, readable results, graded rankings, item/topic analytics, CSV result
transcripts, local persistence and account administration.

Essay grading, proctoring, payments, mobile applications, LMS integration,
networked/live multi-user sessions, cloud deployment, AI question generation
and Bloom-level tagging remain outside the committed scope. A dependency that
is not implemented remains a gap; it is not silently removed from the scope.

## 2. Roles, environment and interfaces

- **Student:** register/login, attempt quizzes, review their result and rank,
  and receive the required own-attempt topic/weak-topic summary.
- **Instructor:** register/login, manage questions/quizzes, view analytics and
  export permitted result reports.
- **Admin:** authenticated account/role/status management, question-bank
  oversight and permitted analytics/export. Public registration cannot create
  an Admin. First-run bootstrap creates the initial Admin only when none exists.
- **Presentation:** numbered role menus, clear errors, cancellation/back paths,
  and confirmation before destructive actions. Role-based screens show the
  logged-in username and role; timed-attempt views provide the required visible
  countdown. Student answer selection uses
  display order; storage/grading uses original zero-based option indexes.
- **Storage:** local SQLite via repository interfaces; CSV for question exchange
  and result export. The application requires no network connection to run.
- **Build dependencies:** C++17 compiler, CMake 3.20+, SQLite3 and OpenSSL
  development libraries. GoogleTest supports tests; cppcheck and GCC/gcov/gcovr
  support the CI/static-analysis/coverage workflow; Docker provides the project
  build/test environment. GoogleTest can be fetched during configuration if it
  is not installed, so a fully offline build needs dependencies preinstalled.

## 3. Use cases

| ID | Actor and required outcome |
| --- | --- |
| UC-01 | Student/Instructor/Admin registers where permitted, then authenticates into a session with the correct role. |
| UC-02 | Instructor manages tagged questions; Admin oversight follows SEC-002. |
| UC-03 | Instructor imports/exports questions; Admin oversight follows SEC-002. |
| UC-04 | Instructor creates a quiz with pooling, time and scoring configuration; Admin creation is permitted by SEC-002. |
| UC-05 | Student attempts a quiz in the selected mode and completes/submits it. |
| UC-06 | Student views their result, graded rank and required weak-topic summary. |
| UC-07 | Instructor/Admin views item and topic analytics from graded attempts. |
| UC-08 | Instructor/Admin exports a per-student CSV result transcript. |
| UC-09 | Admin creates/edits/removes accounts and roles and manages account enablement. |

## 4. Functional requirements

Priorities retain the source SRS values. FR-003 is the documented algorithm
revision; the other rows preserve the original obligations in concise wording.

| ID | Requirement | Priority | Verification |
| --- | --- | --- | --- |
| FR-001 | Allow registration with a unique username and a Student or Instructor role; reject public Admin registration. | High | Functional/negative tests. |
| FR-002 | Authenticate against the stored password hash before starting a role-bearing session. | High | Login and authorization tests. |
| FR-003 | Store newly created account passwords as salted PBKDF2-HMAC-SHA256 values using the production default of 600,000 iterations, a random 16-byte salt and a 32-byte derived key; never store plaintext. Verify through the encoded format with constant-time derived-hash comparison. | High | Hash-format, salt, malformed-value, wrong-password tests and code review. |
| FR-004 | Permit question add/edit/delete with subject, difficulty and question-type tags, subject to SEC-002 and reference protection. | High | Question lifecycle and persistence tests. |
| FR-005 | Reject a question with a missing subject/correct answer or fewer than two options. | High | Invalid-question tests. |
| FR-006 | Import question CSV in bulk, skipping/reporting invalid data rows. Reject a structurally invalid input file without changing stored questions. | Medium | Valid import, row errors, file-structure errors and rollback tests. |
| FR-007 | Export the full or filtered question bank to CSV. | Medium | Full/filtered export and round-trip tests. |
| FR-008 | Create a quiz with positive time limit/count and optional subject/difficulty filters over available questions. | High | Configuration/pooling and restart tests. |
| FR-009 | Configure marks per question and an optional non-negative wrong-answer penalty per quiz. | High | Scoring validation/persistence tests. |
| FR-010 | Independently shuffle question and option order for each attempt, retaining identity and original-answer mapping. | High | Shuffle/mapping and resume tests. |
| FR-011 | Enforce the quiz time limit and auto-submit when it expires. | High | Timeout system/integration tests; manual grading alone does not satisfy this. |
| FR-012 | Allow question navigation and flag-for-review before submission. | Medium | Navigation, selection and review tests. |
| FR-013 | Allow Student selection of practice or graded mode before starting. | High | Mode-selection and separation tests. |
| FR-014 | Show correct-answer feedback immediately during practice; practice must not contribute to graded rankings or analytics. | Medium | Practice feedback/non-persistence and analytics-exclusion tests. |
| FR-015 | Permit one graded submission per Student/quiz and retain it permanently. | High | Duplicate/open-attempt/concurrent-storage and restart tests. |
| FR-016 | Auto-grade submitted answers using the quiz scoring configuration, including the wrong-answer penalty and BR-5 floor. | High | Correct/wrong/blank, floor, validation and real-DAL integration tests. |
| FR-017 | Show each question, Student answer, correct answer and awarded marks in a readable itemised Student result. | High | Result formatting, answer-index resolution and restart/display tests. |
| FR-018 | Rank graded attempts per quiz, per topic and all time. Students must be able to view their rank under UC-06. | Medium | Ranking, ties, practice exclusion and Student-view tests. |
| FR-019 | Compute each question's difficulty index as the percentage of eligible attempts answering correctly; BR-4 restricts eligibility to graded attempts. | Medium | Known-data, blank/unseen/unknown-question and real-DAL tests. |
| FR-020 | Compute average performance by subject tag for a Student and a class. Show the Student's own topic breakdown/weakest topics immediately after a graded attempt alongside FR-017. | Medium | Student/class aggregation and post-attempt weak-topic system tests. |
| FR-021 | Export a per-student result transcript to CSV on request. | Low | Transcript content, permissions, escaping and write/overwrite tests. |
| FR-022 | Allow Admin account enable/disable; refuse a disabled account's login with ACCOUNT_DISABLED after credential verification. | Medium | Authorization, disable/re-enable, login and persistence tests. |

## 5. Non-functional requirements

| ID | Required measurable behavior | Verification |
| --- | --- | --- |
| NFR-001 | A 20-question quiz loads/starts within 2 seconds on the target machine. | PERF-01 timed run with machine, dataset and measurements recorded. |
| NFR-002 | A new Student can log in and start an assigned quiz within 3 menu steps. | USE-01 walkthrough recording what counts as a menu step. |
| NFR-003 | Timeout auto-submission uses the same grading rules as manual submission. | REL-01 comparison and timeout integration tests. |
| NFR-004 | Auth, Question Bank, Quiz Engine, Grading and Analytics have separate headers/source modules. | MNT-01 interface/dependency review. |
| NFR-005 | Every module's core logic is callable/testable independently of the console. | Direct module tests plus module-specific coverage evidence. No numeric coverage threshold is introduced here. |
| NFR-006 | Submitted graded answers and scores cannot be edited. | Grading/DAL tamper, restart and atomicity tests; final integration attack matrix. |

## 6. Security requirements

| ID | Requirement | Verification |
| --- | --- | --- |
| SEC-001 | Never store or log plaintext account passwords. Synthetic test credentials are fixtures, not production account storage. | Hasher, auth, log and UI-output checks. |
| SEC-002 | Enforce Instructor/Admin authorization for question and quiz creation/edit/deletion at the function boundary. | Direct unauthorized-call and UI tests. |
| SEC-003 | Restrict authenticated management of other accounts/roles to Admins. Re-read the actor's current stored role/status on each management operation. FR-001 self-registration and first-run bootstrap are explicit separate paths. | Non-admin, forged/stale-session and Admin lifecycle tests. |
| SEC-004 | Validate/sanitise every user field before processing/storage; malformed numbers/enums must not silently select defaults. | Empty, oversized, malformed and out-of-range negative tests across all menus. |
| SEC-005 | Validate CSV before committing; structural file failures leave the bank unchanged, while invalid data rows are skipped/reported under FR-006. | File/row classification, byte-identical preservation and rollback tests. |
| SEC-006 | Timestamp failed-login logs without recording the attempted password or hash. Malformed username text is replaced by an invalid-input marker. | Timestamp, password-free and free-form-input log tests. |

## 7. Business rules

| ID | Rule |
| --- | --- |
| BR-1 | Pool only questions currently marked available into a new quiz. |
| BR-2 | Reject a zero/non-positive question count or quiz time limit. |
| BR-3 | Reject another graded submission for the same Student/quiz. |
| BR-4 | Practice is repeatable and does not enter graded rankings/analytics. |
| BR-5 | Apply the negative-scoring floor to **each question**, before topic/total aggregation. Correct awards marks per question; wrong awards max(0, -penalty); blank awards 0. Do not substitute an attempt-total floor. |
| BR-6 | Only Instructor/Admin can disable a question; disabling affects eligibility for new quizzes. |
| BR-7 | Only Admin can enable/disable an account, and a disabled account is refused login. |

BR-1/BR-2 labels follow the SRS list order; some backlog/header comments group
the count/time constraints differently. Rule wording, not a conflicting comment,
governs the mapping. Reference protection also prevents content edits/deletion
of questions retained by saved quizzes/historical attempts.

## 8. Data and validation contracts

- **Question:** stable ID, subject, Easy/Medium/Hard difficulty, MCQ type, text,
  ordered options, original correct-option index, availability. The supported
  question type is MCQ; subjective grading remains out of scope. The original
  sample field layout omitted options/type even though FR-004/005 require them.
- **Question text limits:** ID 36 characters, subject 40, text 500, each option
  200; the implementation counts validated UTF-8 code points. Option count is at
  least two; correct index must address an actual option. Control-character
  rules and supported CSV escaping are defined in the Question Bank interface.
- **Account:** numeric ID, case-insensitively unique username, encoded hash,
  role, enabled flag, failure count and lock deadline. Usernames are 3-32 ASCII
  letters/digits/`_`/`-`/`.`; passwords are 8-1024 bytes, preserving spaces.
  Five consecutive failures lock the account for 300 seconds by default;
  successful login resets failure state. Verification accepts only the
  implemented encoded-hash iteration bounds, not arbitrary work factors.
- **Quiz/Attempt/Answer:** stable IDs, role-bearing ownership, mode, timestamps,
  ordered question IDs and original-option answers (`kBlankAnswer = -1`).
  Restart must retain a graded attempt's ownership, order, responses and flags.
- **GradeReport:** score/max score, ordered item results, topic totals and
  auto-submission flag. Distinguish this domain report from the general
  `Result`/`ErrorCode` return type. Multi-table finalization is atomic.
- **Admin safety:** bootstrap only the initial Admin; new Admins thereafter
  require authenticated Admin creation. The signed-in Admin cannot disable,
  demote or remove their own account. Removal needs explicit UI confirmation.

These clarify existing supported contracts; they do not introduce FR-023+
IDs or waive unfinished functionality. See the deviation register for changes
from the PDF and the RTM for code/test locations.

## 9. Verification and release control

The matrix covers all 22 FRs, 6 NFRs, 6 SEC requirements, 7 BRs and 9 use cases.
Planned test IDs are explicitly labelled when no executable/evidence exists.
Passed unit tests must not be substituted for an unexecuted performance run,
walkthrough, timeout flow or final integrated feature.

Before the final submission, reconcile the matrix with the merged release,
attach outstanding measurements/coverage/system evidence, update the SAD and
Test Plan references, then export reviewed submission documents. Release tagging
remains BL-31; this revision does not claim that freeze has happened.

## 10. Revision history

| Version | Date | Change |
| --- | --- | --- |
| 1.0.1 | Retained source baseline | SRS in the historical combined PDF; FR-001 through FR-022. |
| 1.0.2 draft | 2026-11-15 | Record actual hashing/dependencies, clarify mode/index/storage/Admin contracts, preserve measurable targets and original IDs, replace sample traceability with complete implementation/evidence mappings. Pending features remain visible. |
