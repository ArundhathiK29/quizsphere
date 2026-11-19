# Known limitations and release boundary

Updated 19 November 2026. The team is freezing feature expansion for this review candidate.

## Product limitations

- Local desktop demonstration: console plus optional `127.0.0.1` browser UI. No public hosting, TLS deployment, cloud accounts, multi-machine concurrent service or load-test claim.
- MCQ questions only; no essay grading, proctoring or LMS integration.
- Browser UI covers Student/Instructor auth, question management, quiz creation, attempts, results and rankings. Account administration, CSV question import/export, class-topic comparison and transcript export are available through the console/core rather than full browser screens. Dedicated difficulty-index presentation is not implemented; the tested core API is available.
- Per-question zero floor follows retained BR-5: a wrong item's negative mark is floored to zero before summing. This limits the practical effect of a configured penalty; it is not conventional negative marking.
- In-memory browser sessions expire and require login after restart. Submitted graded data remains in SQLite; practice sessions are deliberately not persisted.
- Windows console input uses the console input code page; non-ASCII input may be rejected unless the terminal uses UTF-8 (chcp 65001). Rejected Question Bank lines are drained and re-prompted; Unicode terminal compatibility across code pages is not claimed.
- Full Internet security hardening, independent penetration testing, broad accessibility certification and every OS/compiler combination are not claimed.

## Evidence limitations

- Line and branch coverage are reported with compiler/scope. Coverage is not proof of correctness; above-90% whole-project branch coverage has not been established and is not claimed. A newly requested numerical target is not waived by this document.
- PERF-01 has five measured 20-question load/start trials on the recorded local machine (see SYSTEM_VALIDATION_REPORT.md), satisfying the <2 second target in that scope. This does not establish performance on every target machine or multi-user load. USE-01 remains Not Accepted; no first-time-user study or formal UAT approval is claimed.
- A green test suite proves the named checks at its tested revision; it is not equivalent to lecturer acceptance or a complete final submission.

## Before final release

- A teammate must review this integration candidate. Preserve its existing commits when merging.
- Aryan must finish/reconcile the final SAD/deviation document (BL-36) and upload the full Sprint 2 demo (BL-37). The earlier Sprint 1 video is included.
- Merge the reviewed work, verify final main CI, then create the `v1.0` tag at that exact final main commit. BL-31 stays open until the tag exists; this candidate prepares its README but does not pretend the freeze has occurred.

Other limitations above are disclosure of scope/evidence, not a request to build more features before this review.


## Part 3 measured acceptance status (19 November 2026)

The [updated Test Case Document](TEST_CASE_DOCUMENT.md) retains 73 original-plan IDs: 70 Pass, two Partial (complete alternative flat-file DAL parity and dedicated Admin item-analysis display), and one Not Accepted (original console three-step usability target). Formal team/instructor UAT approval is pending. Five measured 20-question load/start trials satisfy PERF-01 on the recorded machine. At measured source cce8549, line coverage is 3874/4323 (89.61%); branch coverage is 5740/10914 (52.59%), including GCC exception/compiler paths. A separate 90% branch threshold, if imposed, is not met. These disclosures do not silently waive original exit criteria.
