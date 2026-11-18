# Requirements baseline and document control

Review date: **15 November 2026**. Scope: BL-30, SRS revision and traceability.

## Sources

The retained [combined source PDF](Team12_SE_QuizSphere.pdf) contains SRS v1.0.1,
SAD v1.1 and the Test Plan. It has 25 PDF pages. Its SHA-256 is
`10ff850ad48533e29d99525a4542c33b216aaf29a1f2e1d9532282b8cc215891`;
Git blob `5038982b1e9749f9a5f9532cd4126bccff550e6c` matches the repository copy.
References below use the PDF's physical page number, since section page numbers
restart inside the combined file.

| Source | Relevant locations |
| --- | --- |
| SRS v1.0.1 | PDF pp. 1-9; functional requirements pp. 5-6; NFR/security p. 6; business rules p. 7; sample RTM p. 9. |
| SAD v1.1 | PDF pp. 10-17; components/layers sections 3.4-3.5, pp. 12-13; sample traceability/security sections 3.8-3.9, pp. 13-14; design sections 4.1-4.5, pp. 15-17. |
| Test Plan | PDF pp. 18-25; requirement/test mapping pp. 22-23; representative expected results pp. 24-25. |
| Implemented baseline | Main commit `0d9519ab9c93ff0ccb047ba6cee1a86893579855`. |
| Work awaiting integration | Pinned PR revisions in [the review manifest](evidence/requirements-review-2026-11-15.json). |

## Current editable documents

- [SRS 1.0.2 integration draft](SRS.md): implementation-aligned requirements,
  retaining the original IDs and measurable targets.
- [Traceability matrix](REQUIREMENTS_TRACEABILITY.md): architecture, design,
  actual code and test references, with implementation/evidence status.
- [Implementation design cross-reference](IMPLEMENTATION_DESIGN.md): stable
  module/contract references for the matrix; supports Aryan's separate BL-36
  SAD update without claiming to replace the submitted diagrams.
- [Deviation register](REQUIREMENT_DEVIATIONS.md): changed implementation
  decisions, document corrections and outstanding implementation gaps.
- [Verification and handoff](REQUIREMENTS_VERIFICATION.md): evidence boundaries
  and checks needed before the final release baseline is frozen.

The PDF is retained unchanged as the historical submission. The editable SRS
revision is proposed for teammate review in this documentation PR; its wording
is not evidence that a pending feature has been merged or accepted. A future
combined submission export must use the reviewed editable revision and the
updated SAD/Test Plan, rather than silently treating the old PDF as current.

## Stable requirement IDs

There are **22** functional requirements, **6** non-functional requirements,
**6** security requirements and **7** business rules. UC-01 through UC-09 remain
the use-case identifiers. The business-rule labels BR-1 through BR-7 follow
their original SRS order.

FR-017 means itemised results; FR-018 means leaderboards. Conflicting code
comments or old proposal text do not renumber these requirements. Earlier
suggestions to introduce FR-023 through FR-026 and SEC-007 are not adopted by
this revision: their existing implementation details are recorded as
clarifications under FR-001/002/003/022 and SEC-003/004/006, and UC-09.

## Reading status

**Merged** means the code is present in the pinned main revision. **Partial**
means a subset exists, but the full requirement still has an implementation or
verification gap. **Pending PR** links to a specific unmerged implementation.
**Planned** names a backlog/test-plan obligation without claiming executable
coverage. A green CI job establishes only the tests that ran at that revision;
it does not turn planned tests, performance targets or a walkthrough into a pass.
