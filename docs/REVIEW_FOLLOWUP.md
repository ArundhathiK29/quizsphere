# PR #82 review follow-up (19 November 2026)

Responds to the read-through of head 8aa653e. This is a correction within the existing PR; no new feature scope is added.

## Confirmed input issue

Question Bank previously treated every false readConsoleLine result as EOF. An invalid control byte, invalid UTF-8 or oversized line could exit its menu. The optional ConsoleLineStatus output now distinguishes Accepted, Rejected and EndOfInput while existing callers retain the bool cancellation contract. Question Bank drains the rejected line and re-prompts for the same value; actual EOF still exits. Password and timed-confirmation validation are unchanged.

Two new tests cover status transitions and an invalid line followed by a valid list command (including a 100,000-byte line). Both pass. Windows raw ReadFile still uses the console code page; use UTF-8 (chcp 65001) for non-ASCII text. Interactive Unicode compatibility on other code pages is not claimed or silently treated as fixed.

## Documentation reconciliation

- LIMITATIONS and RTM now agree with the recorded five PERF-01 trials and USE-01 Not Accepted outcome.
- MODULE_INTERFACES and deviation rows describe the integrated timer, results, Admin console and analytics, rather than stale pending PRs/stubs.
- DOCUMENT_UPDATES_HISTORICAL preserves the complete prior editable checklist. DOCUMENT_UPDATES maps every category to current sources and explicitly rejects the obsolete proposed FR-023-026/SEC-007 renumbering.
- BL-33/34/36 must use these same outcomes; BL-36 final SAD/export and BL-37 demo remain team deliverables.
- Part 3 reports remain pinned to measured source cce8549. Their 89.61% line / 52.59% branch coverage is historical evidence, not remeasured coverage of this source correction. The report generator preserves this qualification.

## Verification

Native Windows build succeeds. Full native CTest run: **206 passed, five failed to start their HTTP connection out of 211 entries**. All five report Windows socket permission error 10013, so this session cannot validate those HTTP timer scenarios. The two new regression entries and all non-socket entries pass. Raw [Windows log](evidence/review-followup-2026-11-19/windows.txt) retains failures rather than calling this a clean full-suite pass. WSL access was also denied. Fresh GitHub Linux/Docker CI is required for this source change; earlier green CI on 8aa653e is not substituted for it.

## Other review points and merge gate

The SQLite partial unique index single_graded_submission on attempts(quiz,student) WHERE mode=1 AND submitted=1 enforces one submitted graded attempt per Student/quiz; open-attempt triggers separately protect open graded attempts. findReport enumeration is not a claim of large-dataset performance. The bounded 10 MiB import allocation, dense server style and possible would-block request drop remain review observations; this correction does not claim to have changed them.

The optional web scope is recorded in SRS 1.0.3/D-13. Test evidence/video are retained for grading, not expanded with another feature. Preserve contribution history using a merge commit. Ashwin's changes-requested review still requires his re-review; this document is not an approval or waiver of acceptance exceptions.
