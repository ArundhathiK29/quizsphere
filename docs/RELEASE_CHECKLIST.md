# Final release checklist and accepted scope

Audit date: 21 November 2026. Reviewed production baseline: da6d70a6861b33169a24300bf7ae773af54f2813, containing merged #82 and #83. Both main CI jobs passed in [run 44489269211](https://github.com/ArundhathiK29/quizsphere/actions/runs/44489269211).

## Delivered work

- [x] Integrated Admin/user management, validation, authentication tests, SRS/RTM and optional loopback browser adapter (#82).
- [x] Corrected cross-session auto-submit test setup (#83); the test waits for SQLite automatic completion without HTTP polling that could hide a stalled timer.
- [x] Reviewed #36 handoff and prepared editable [SAD](SAD.md), [deviations](SAD_DEVIATIONS.md) and Mermaid diagrams for version control in this documentation PR.
- [x] Retained all Part 3 reports and original measured source/coverage provenance.
- [x] Linked both submitted Sprint 2 recordings from [demos/README.md](demos/README.md). The repository owner explicitly accepts those clips without a supplementary analytics/export recording.

## Final steps before closing BL-31 as completed

- [ ] A teammate reviews and merges this documentation PR using a merge commit, preserving contributions.
- [ ] Final main CI succeeds at the merged documentation commit.
- [ ] Create an annotated v1.0 tag at that exact final main commit; do not move an existing published tag.
- [ ] Verify the tag target and mark BL-31 completed with its tag and CI links.

Git commands for the authorised maintainer, after the first two steps:

```sh
git switch main
git pull --ff-only
git status --short
git tag -a v1.0 -m "QuizSphere v1.0 - reviewed local application; scope and evidence limitations documented"
git push origin v1.0
```

The working tree should be clean and the pulled SHA must match successful final main CI. Do not publish a tag from an old checkout merely because the application starts.

## Acceptance disclosures retained in the release

The original three-step console usability target is Not Accepted; complete alternative flat-file DAL parity and dedicated Admin item-analysis presentation are Partial. Formal human/lecturer UAT approval is not documented. Pinned Part 3 production coverage is 89.61% lines and 52.59% GCC-reported branches at cce8549; this is not a fresh measurement of later source changes or an above-90% branch pass. The owner accepts the existing video submission; analytics/export video coverage and lecturer waiver approval are not claimed. These disclosures remain in [LIMITATIONS.md](LIMITATIONS.md).

No issue is marked completed solely to obtain a zero-open count. BL-31's earlier presentation closure was administrative and its remaining tag task is explicit here.
