# Document update checklist and disposition

Updated 19 November 2026 for PR #82. The complete earlier SRS 1.0.2 / SAD 1.2 / STP 1.1 edit checklist is preserved as editable Markdown in [DOCUMENT_UPDATES_HISTORICAL.md](DOCUMENT_UPDATES_HISTORICAL.md), not only Git history. It is a superseded proposal; apply the dispositions below rather than copying its proposed numbering.

| Earlier checklist item | Current source / disposition |
| --- | --- |
| SRS password hashing, account management/bootstrap, validation, lockout and stored-Admin checks | [SRS 1.0.3](SRS.md) and [deviations D-01/D-05/D-12](REQUIREMENT_DEVIATIONS.md). Preserve FR-001-022, SEC-001-006, UC-09 and BR-1-7; proposed FR-023-026/SEC-007 were not adopted. |
| Dependencies, user/question schema and ordered options | SRS environment/storage sections, [design cross-reference](IMPLEMENTATION_DESIGN.md), DAL_PERSISTENCE.md and actual schema/interfaces. |
| SAD hashing, actual Auth filenames/APIs/errors, shared Session/Role and architecture boundaries | [MODULE_INTERFACES.md](MODULE_INTERFACES.md), implementation design and deviations. Aryan owns the final SAD/diagram export under BL-36; this candidate does not claim that export is complete. |
| SAD requirement counts, revision dates and table layout | BL-36 retains these editorial checks. Use 22 original functional IDs, actual revision dates and readable tables. |
| STP references, missing FR-022/BR-7, PBKDF2, tools/environment and added auth/security scenarios | [TEST_PLAN_UPDATE.md](TEST_PLAN_UPDATE.md), [TEST_CASE_DOCUMENT.md](TEST_CASE_DOCUMENT.md), [UNIT_TEST_REPORT.md](UNIT_TEST_REPORT.md), raw named-test inventory and RTM. Original IDs retained. |
| Behavioural corrections: indistinguishable credentials, stored Admin re-check, question validation fixtures | Current source/tests and deviation register. Historical fixture corrections remain in Git history; they are not fresh product defects. |
| Performance/usability (BL-33/34), SAD (BL-36), demonstration (BL-37) | PERF-01 Pass: five local measured 20-question trials; USE-01 Not Accepted: four console decisions, no three-step claim. SAD must match these outcomes. Full Sprint 2 video remains a separate deliverable. |
| Optional browser scope | Owner-requested local adapter recorded in SRS 1.0.3 and D-13. No lecturer approval or public deployment claim; feature expansion is frozen. |

## Final handoff

[RTM](REQUIREMENTS_TRACEABILITY.md) provides architecture/design/code/test mappings; [verification](REQUIREMENTS_VERIFICATION.md), [Part 3 summary](PART3_TEST_SUMMARY.md) and [limitations](LIMITATIONS.md) distinguish executed results from outstanding acceptance. The combined PDF remains the historical source. Aryan should incorporate reviewed SRS/SAD/Test Plan changes into the final submission export, including diagrams, dates and deviations. No absent implementation or formal acceptance is silently waived. PR #82 still requires teammate re-review and approved merge, followed by final main CI and the actual v1.0 tag.
