# Coverage and test evidence status

21 November 2026. This is an evidence summary; raw reports remain in main docs/evidence/part3-2026-11-19.

| Measurement | Result | Source / boundary |
| --- | --- | --- |
| Whole-production line coverage | 3874/4323 = 89.61% | cce8549061143fab649eab618051becc6ffc06e7; Linux GCC/gcovr, compiled src/ |
| Whole-production branch coverage | 5740/10914 = 52.59% | Same source/scope, includes GCC exception/compiler paths |
| Distinct GoogleTest cases | 290 | Retained Part 3 inventory at measured source; two later input-recovery cases added |
| PR #82 Linux / Docker checks | 212/212 each | Head 8c7b92b; run 44460870712, both jobs success |
| Native follow-up | 206 pass; 5 socket-start failures / 211 | Permission error 10013 prevented HTTP connections; retained logs, not called clean pass |

Earlier #78/#68 figures are historical scope-specific measurements. Later console-input corrections do not inherit a new coverage measurement. A separate 90% branch requirement, if imposed, is not met by the retained report. Never substitute line coverage for branch coverage. Final merged-main CI is a separate release check.

PERF-01: five local measured 20-question trials pass (<2000 ms); maximum 14.989645 ms in retained Linux evidence. USE-01: original three-step console target Not Accepted. Original-plan 73 IDs: 70 Pass, two Partial, one Not Accepted; formal human UAT pending. See SYSTEM_VALIDATION_REPORT.md, TEST_CASE_DOCUMENT.md and REVIEW_FOLLOWUP.md in the repository.

Merged main da6d70a6861b33169a24300bf7ae773af54f2813 passed both Linux and Docker jobs in [run 44489269211](https://github.com/ArundhathiK29/quizsphere/actions/runs/44489269211). This does not update the pinned cce8549 coverage denominator or establish a Windows rerun of #83.
