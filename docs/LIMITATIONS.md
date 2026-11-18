# Known limitations and release boundary

Updated 18 November 2026. The team is freezing feature expansion for this review candidate.

## Product limitations

- Local desktop demonstration: console plus optional `127.0.0.1` browser UI. No public hosting, TLS deployment, cloud accounts, multi-machine concurrent service or load-test claim.
- MCQ questions only; no essay grading, proctoring or LMS integration.
- Browser UI covers Student/Instructor auth, question management, quiz creation, attempts, results and rankings. Account administration, CSV question import/export, class-topic comparison and transcript export are available through the console/core rather than full browser screens. Dedicated difficulty-index presentation is not implemented; the tested core API is available.
- Per-question zero floor follows retained BR-5: a wrong item's negative mark is floored to zero before summing. This limits the practical effect of a configured penalty; it is not conventional negative marking.
- In-memory browser sessions expire and require login after restart. Submitted graded data remains in SQLite; practice sessions are deliberately not persisted.
- Full Internet security hardening, independent penetration testing, broad accessibility certification and every OS/compiler combination are not claimed.

## Evidence limitations

- Line and branch coverage are reported with compiler/scope. Coverage is not proof of correctness; above-90% whole-project branch coverage has not been established and is not claimed. A newly requested numerical target is not waived by this document.
- Existing performance and usability evidence is retained, but this work does not invent a new formal target-machine PERF-01 result or a first-time-user study. The original SRS targets remain visible in the RTM.
- A green test suite proves the named checks at its tested revision; it is not equivalent to lecturer acceptance or a complete final submission.

## Before final release

- A teammate must review this integration candidate. Preserve its existing commits when merging.
- Aryan must finish/reconcile the final SAD/deviation document (BL-36) and upload the full Sprint 2 demo (BL-37). The earlier Sprint 1 video is included.
- Merge the reviewed work, verify final main CI, then create the `v1.0` tag at that exact final main commit. BL-31 stays open until the tag exists; this candidate prepares its README but does not pretend the freeze has occurred.

Other limitations above are disclosure of scope/evidence, not a request to build more features before this review.
