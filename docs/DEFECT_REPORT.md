# Defect and Regression Report - Part 3

Report date: 19 November 2026 (Asia/Calcutta). Defects were reproduced and corrected on the review branch of PR #82. A green existing suite did not cover the newly reported states; regression coverage was added before claiming resolution. Independent teammate re-review remains required.

| ID | Defect and original evidence | Correction | Regression evidence |
| --- | --- | --- | --- |
| P3-D01 | Incomplete, drip-fed HTTP header/body stalled auto-submission on 2a570dd; stored submitted=0 and no report after the deadline. | Single-thread nonblocking network waits continue expiry processing; absolute two-second connection budget. | web_timer_slow-header; web_timer_slow-body |
| P3-D02 | Starting practice replaced an active graded timer on 2a570dd. | Reject replacement until graded completion; same graded quiz can resume. | web_timer_practice-switch |
| P3-D03 | Actual y<CR>es<LF> confirmation submitted unexpectedly on 2a570dd. | Preserve embedded control bytes for rejection; only trailing CR in ordinary CRLF is stripped. | ST_SEC_09_TimedConfirmationControls |
| P3-D04 | A second session retained an open attempt after another session submitted it on 93f306c; new activity was wrongly blocked. | Query authoritative SQLite completion before cached browser state/guards; display exact completed result and reject stale mutation. | web_timer_cross-session-manual; web_timer_cross-session-auto |
| P3-D05 | Repetition exposed intermittent Windows command loss from mixed buffered/raw input. The first attempted CRT-only correction also failed with a pipe kept open. | Use one raw Windows handle path consistently for normal and timed real input; test streams retain their normal stream path. | Thirty repeated actual confirmation runs and five repeated process-kill scripts passed after the final transport correction; complete Windows suite rerun. |
| P3-D06 | Newer retained legacy practice report replaced the graded result. | Select exact submitted Graded attempt/report; propagate storage errors. | GradedReportIgnoresNewerPracticeAndSurvivesRestart; practice-only and missing-report regressions |

Before/after logs are retained under evidence/part3-2026-11-19. The final suite results and measured coverage are in the Part 3 summary. Reproductions use synthetic disposable data; do not demonstrate a process kill on a real presentation database.

Remaining acceptance exceptions (rather than hidden functional Pass entries): original console three-step usability target, full alternative flat-file DAL parity, dedicated Admin item-analysis display, and formal human/lecturer UAT. These remain visible in the updated Test Case Document and System Validation Report. No signatures or blanket zero-defect guarantee is fabricated.
