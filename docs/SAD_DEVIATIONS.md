# SAD deviation register

Version 1.4 review candidate, 21 November 2026. Main da6d70a6861b33169a24300bf7ae773af54f2813. BL-36 owner: Aryan. This integrated documentation candidate requires teammate review; its presence does not imply approval.

| ID | Earlier design | Current design / disposition |
| --- | --- | --- |
| SAD-D01 | Plain SHA-256 | Salted PBKDF2-HMAC-SHA256 via OpenSSL, 600000 creation iterations, random 16-byte salt, constant-time verification. |
| SAD-D02 | auth_module filenames and simplified functions | Actual auth/hasher/log/account repositories, interfaces/factories; match MODULE_INTERFACES.md. |
| SAD-D03 | Incomplete fields/file sketches | SQLite ordered question/options and quiz/attempt/report schema; explicit legacy import. Complete alternative flat-file DAL remains absent. |
| SAD-D04 | Ambiguous negative-score floor | BR-5 floors each item at zero before topic/total aggregation. |
| SAD-D05 | Planned timer/simple resume | Integrated UTC/monotonic deadline, timed input, real auto-grading and recovery; no background service while closed. |
| SAD-D06 | Simplified duplicate prevention | Transactions, unique submitted-grade index and open-attempt triggers; immutable submitted records. |
| SAD-D07 | Latest report lookup | Exact submitted Graded attempt/report excludes newer retained Practice reports, including getReport regression fix. |
| SAD-D08 | Admin menu pending | Integrated complete Admin console; browser Admin screens absent. |
| SAD-D09 | Topics/transcript pending | Integrated Student/class topics and Instructor/Admin transcript console; Student-own backend extension denies other Students. Dedicated Admin item-analysis display remains absent. |
| SAD-D10 | Proposed extra requirements | Preserve FR-001-022, SEC-001-006 and UC-01-09; historical checklist retained with disposition. |
| SAD-D11 | Coverage without scope | At cce8549: 89.61% lines, 52.59% branches; no fresh follow-up measurement or 90% claim. |
| SAD-D12 | Console-only presentation | Owner-requested local browser adapter integrated in #82 and SRS 1.0.3; no lecturer-approved scope/public hosting claim. |
| SAD-D13 | All validation accepted | PERF-01 Pass within measured local scope; USE-01 Not Accepted; DAL parity/item-analysis Partial; formal human UAT pending. |
| SAD-D14 | Input rejection equals EOF | Question Bank distinguishes rejected line from EOF and retries; embedded timed controls rejected. Non-UTF-8 Windows console compatibility limited. |
| SAD-D15 | Browser attempt pointer authoritative | SQLite reconciles cross-session completion; unfinished graded replacement denied; slow HTTP waits continue deadline ticks. |

These deviations disclose differences and acceptance exceptions; they do not approve waivers. BL-36 document review, the owner-accepted #37 recordings, and the BL-31 final tag remain release work. See RELEASE_CHECKLIST.md.

SAD-D16: the repository owner accepted the existing #37 console/UI recordings on 21 November without an additional analytics/export recording. Original recording coverage is not falsely asserted; lecturer waiver approval is not claimed.
