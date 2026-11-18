# Requirements review verification and handoff

Reviewed: **15 November 2026** for BL-30. See [baseline](REQUIREMENTS_BASELINE.md)
and [review manifest](evidence/requirements-review-2026-11-15.json).

## Document checks

- All original requirement families are retained: FR-001-022, NFR-001-006,
  SEC-001-006, BR-1-7 and UC-01-09: **50 unique traceability rows**.
- Every row has architecture, design, code, tests/verification and status.
  Unimplemented features link their real stub/interface and planned test IDs;
  planned tests are not represented as executable passes.
- Referenced implementation identifiers and GoogleTest names were checked
  against their actual files. Pending code/tests link immutable PR revisions,
  not nonexistent relative files in main. The reference audit examined 54
  distinct source/test file-revision combinations.
- The historical PDF matches its recorded Git blob/SHA-256. Its full relevant
  requirement text was extracted and the functional/NFR/security tables were
  visually inspected. The PDF is not edited by this PR.
- Local document links/anchors, ID counts, table columns, unchanged product-code
  files and whitespace are checked before publishing. These are documentation
  integrity checks, not new functional test execution.

## Execution evidence and limits

| Evidence | Revision / source | What it establishes |
| --- | --- | --- |
| Main CI | `0d9519ab9c93ff0ccb047ba6cee1a86893579855`; [run 43730792622](https://github.com/ArundhathiK29/quizsphere/actions/runs/43730792622) | Build-test and Docker each passed 148/148 CTest entries. This is the merged implementation baseline. |
| Auth coverage | Pending [#68](https://github.com/ArundhathiK29/quizsphere/pull/68), `1fb68a4ae6e455745f0d4b1b2c0da3eede168885`; [scoped report](https://github.com/ArundhathiK29/quizsphere/blob/1fb68a4ae6e455745f0d4b1b2c0da3eede168885/docs/AUTH_COVERAGE.md) | The dated four-source measurement ran 40 focused tests: 440/474 lines, 436/690 GCC-reported branches. It is not whole-project or new Admin UI coverage. |
| Leaderboard follow-up | Pending [#70](https://github.com/ArundhathiK29/quizsphere/pull/70), `0a8cf8d746e877d7104ad712f12e573469760fe7`; [run 43734826113](https://github.com/ArundhathiK29/quizsphere/actions/runs/43734826113) | Build-test/Docker each passed 151/151 entries on the leaderboard stack. Includes exact graded-report selection and Student view cases. It is not merged-main evidence. |
| Admin console | Pending [#71](https://github.com/ArundhathiK29/quizsphere/pull/71), `887b54bbe0819668accf73973995ba46e5f074da`; [run 43738064178](https://github.com/ArundhathiK29/quizsphere/actions/runs/43738064178) | Build-test/Docker each passed 166/166 entries. Includes actual CLI lifecycle across restarts. It is not merged-main evidence. |
| Old auth/quiz records | [Auth execution](AUTH_VERIFICATION.md), [quiz-attempt execution](SPRINT1_QUIZ_ATTEMPTS.md) | Historical 3/4 November runs remain dated records, not current suite counts or final integrated acceptance. |

Some CTest entries contain multiple GoogleTest cases. Counts from different
branches/registration styles are not additive and are not a feature-completion
percentage. Green CI does not supply PERF-01 measurements, USE-01 walkthroughs,
an unimplemented timer, class analytics, transcript export or a final demo video.
This documentation task does not rerun/claim a fresh local Docker build.

## Before final BL-30 closure

1. Refresh the baseline commit and reviewed PR states after required merges;
   change each RTM row only when its actual implementation/evidence is on that
   baseline. #70 merges into #62's branch first; it does not itself deliver
   rankings to main.
2. Retain the completed floor correction; verify readable results, rankings,
   difficulty and Admin menu against their final merged contracts. Check #68's
   coverage scope against changed source hashes; rerun changed coverage rather
   than copying old percentages onto new code.
3. Link BL-38 timer/REL-01, BL-41 Student/class weak-topic reports and BL-42 CSV
   transcripts once delivered. Resolve the UC-08/header permission discrepancy
   D-08 with the feature owner; preserve explicit permission tests.
4. Attach BL-28 cross-menu negative evidence, BL-33 USE-01 and BL-34 PERF-01
   results; keep BL-39/43 lock/immutability verification and module coverage
   evidence linked to the appropriate requirement rows.
5. Coordinate the SAD/diagram update with Aryan's BL-36 and replace outdated
   Test Plan expected/actual references. Export the reviewed submission documents
   from their current editable sources, retaining the source PDF as history.
6. Have a teammate review the completed documentation. Close BL-30 when its
   final integrated mapping/deviations match the submission. BL-31 release tag
   and final README remain a separate final freeze step.

This list is a documentation/evidence handoff for existing backlog obligations;
it creates no new functional requirements and does not reopen resolved fixes.
