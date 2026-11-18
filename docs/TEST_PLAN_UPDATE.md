# Test Plan update 1.1 - Part 3

Report date: **2026-11-19 (Asia/Calcutta)**. Tested source: [`cce8549061143fab649eab618051becc6ffc06e7`](https://github.com/ArundhathiK29/quizsphere/commit/cce8549061143fab649eab618051becc6ffc06e7). Later report-only commits preserve the tested source/test hashes in [evidence/part3-2026-11-19/source-manifest.json](evidence/part3-2026-11-19/source-manifest.json).

Original STP: combined PDF pp. 18-25, retained unchanged. Updates cover timer, exact reports, analytics/transcripts, Admin console, input validation and optional local browser adapter. Original IDs and targets are preserved. Partial implementation and formal acceptance gaps stay visible rather than being removed from the test plan.

Reproduce:

```sh
cmake -S . -B build-cov -DQUIZSPHERE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cov -j 4
ctest --test-dir build-cov --output-on-failure --output-junit evidence/ctest.xml
python3 tests/web_http_smoke.py build-cov/quizsphere_web
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 -q -I include include src
gcovr -r . --object-directory build-cov --filter 'src/' --json-summary evidence/coverage-summary.json --xml evidence/coverage.xml --html evidence/coverage.html --txt evidence/coverage-lines.txt
gcovr -r . --object-directory build-cov --filter 'src/' --txt-metric branch --txt evidence/coverage-branches.txt
```

Use a fresh build or remove only its .gcda files after building to get a clean measurement. Run each built *tests executable with --gtest_output=xml:evidence/gtest/TARGET.xml and retain stdout for the individual-case inventory. These additional inventory executions are not additional unique cases. Python 3/gcovr/GoogleTest are testing dependencies, not product runtime requirements. All scripts use disposable databases. Do not kill a real application/user database for crash testing.

The supplied scripts/generate_part3_reports.py regenerates Markdown from retained evidence using --evidence and --revision. Original exit requirements are not waived: USE-01, full alternative-DAL parity, item-analysis presentation and human UAT retain exceptions. Formal signatures cannot be created by automation. Final main, SAD/demo and v1.0 freeze remain separate review/release tasks.
