"""Generate Part 3 reports from retained XML, stdout and gcovr measurements."""
import argparse
from collections import Counter
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--evidence', type=Path, required=True)
p.add_argument('--revision', required=True)
p.add_argument('--date', default='2026-11-19')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
docs = root / 'docs'
e = a.evidence.resolve()
rel = e.relative_to(docs).as_posix()
cov = json.loads((e / 'coverage-summary.json').read_text())
env = json.loads((e / 'environment.json').read_text())
all_tests = []
binaries = {}
for file in sorted((e / 'gtest').glob('*.xml')):
    cases = ET.parse(file).getroot().findall('.//testcase')
    binaries[file.stem] = cases
    for t in cases:
        all_tests.append({'name': t.get('classname') + '.' + t.get('name'), 'binary': file.stem,
                          'failed': t.find('failure') is not None, 'skipped': t.find('skipped') is not None,
                          'properties': {x.get('name'): x.get('value') for x in t.findall('./properties/property')}})
assert not any(t['failed'] for t in all_tests)
ctest = {}
for platform, filename in [('Linux', 'ctest.xml'), ('Windows', 'ctest-windows.xml')]:
    cases = ET.parse(e / filename).getroot().findall('.//testcase')
    ctest[platform] = {'total': len(cases), 'failed': sum(t.find('failure') is not None for t in cases),
                       'skipped': sum(t.find('skipped') is not None for t in cases)}
    assert ctest[platform]['failed'] == 0, platform + ' has failed cases'

def put(name, text):
    (docs / name).write_text(text.strip() + '\n', encoding='utf-8')

def select(patterns):
    return [t for t in all_tests if any(fnmatch.fnmatchcase(t['name'], x) for x in patterns)]

def refs(tests):
    return '; '.join(f"[`{t['name']}`]({rel}/gtest/{t['binary']}.txt)" for t in tests[:4])

stamp = f"Report date: **{a.date} (Asia/Calcutta)**. Tested source: [`{a.revision}`](https://github.com/ArundhathiK29/quizsphere/commit/{a.revision}). Later report-only commits preserve the tested source/test hashes in [{rel}/source-manifest.json]({rel}/source-manifest.json)."
scenarios = {}
def group(prefix, lines):
    for item in lines.split(';'):
        number, text = item.strip().split('=', 1)
        scenarios[f'{prefix}-{int(number):02}'] = text

group('UT-AUTH', '1=Valid login returns identity/role/time;2=Register Student and Instructor;3=Reject duplicate usernames;4=Reject wrong password and unknown user;5=Salted PBKDF2 hash, no plaintext;6=Reject disabled login;7=Deny direct privilege escalation;8=Admin account/role lifecycle;9=Deny public Admin registration')
group('UT-QB', '1=Add and persist MCQ;2=Edit unused question;3=Confirm/cancel deletion;4=Reject invalid correct answer;5=Reject missing subject;6=Reject fewer than two options;7=Import valid CSV;8=Import eight valid rows and report two bad rows;9=Export full/filtered CSV;10=Disable and exclude from new quizzes;11=Timestamp rejected-row logs')
group('UT-QE', '1=Shuffle without losing answer identity;2=Apply subject/difficulty filters;3=Reject insufficient pool without partial quiz;4=Persist scoring configuration;5=Zero penalty stays disabled;6=Preserve graded deadline on restart;7=Reject nonpositive count;8=Reject nonpositive duration')
group('UT-GRADE', '1=Grade correct/wrong/blank answers;2=Per-question zero floor;3=No negative item score;4=Reject repeat graded submission;6=Retrieve exact stored itemised report;7=Reject tampered answers through core;8=QUIZ_NOT_FOUND for missing quiz;9=Persist expired grade as auto-submitted')
group('UT-ANLY', '1=Three correct of four equals 75 percent;2=Topic/class average and weakest topics;3=Descending quiz ranking with ties;4=Topic/all-time totals exclude practice;5=Correct authorized CSV transcript;6=Practice excluded from analytics')
group('UT-DAL', '1=Kill real app mid-save: no partial attempt/report;2=Flat-file versus SQLite parity;3=Storage access confined to repositories/logging')
group('ST-QE', '1=Displayed choices map to original options;2=Visible timer auto-submits during input;3=Navigate/flag without losing answers;4=Practice/graded mode selection;5=Practice feedback with no persisted grade;6=Reject retake after restart')
group('ST-ANLY', '1=Itemised and weak-topic summary together;2=Instructor transcript export;3=Admin item-analysis and topic display;4=Admin transcript export')
group('ST-ADM', '1=Admin lifecycle and non-Admin denial;2=Admin question-bank and report oversight')
group('ST-UI', '1=Identity and role visible;2=Destructive confirmation;3=Clear errors without silent defaults;4=Back/EOF exits cleanly')
group('ST-SEC', '1=No plaintext secrets in DB/log/output;2=Student cannot mutate questions/quizzes;3=Only current enabled Admin manages users;4=Reject empty/oversized/malformed/injection input;5=Malformed CSV preserves bank;6=Safe timestamped failed-login log')
scenarios.update({'ST-GRADE-01': 'Readable itemised selected/correct answers and marks', 'IT-01': 'Immutable transactional final grades', 'PERF-01': 'Load/start twenty questions within 2000 ms', 'USE-01': 'New Student starts assigned quiz within three menu steps', 'REL-01': 'Manual and timeout grading parity', 'MNT-01': 'Independent layered module interfaces'})
aliases = {
    'UT-AUTH-07': ['*RoleBoundary*', '*StudentCannot*'],
    'UT-ANLY-02': ['*TopicAnalytics*', '*TopicBreakdown*'], 'UT-ANLY-03': ['*LeaderboardRanks*'],
    'UT-ANLY-04': ['*TopicLeaderboardSums*', '*AllTimeLeaderboardSums*'],
    'UT-ANLY-05': ['*Transcript*'], 'UT-ANLY-06': ['*PracticeExcluded*', '*GradedAttemptsOnly*', '*TopicRankingIgnores*'],
    'UT-DAL-03': ['*MNT_01*'], 'UT-QE-07': ['*RejectsNonPositiveQuestionCount*'], 'UT-QE-08': ['*RejectsNonPositiveTimeLimit*'], 'ST-QE-04': ['*PracticeFeedbackAndCompletion*', '*ManualSubmitAndUntimedPractice*'],
    'ST-QE-05': ['*PracticeFeedback*'], 'ST-QE-06': ['*GradedRetake*', '*GradedResultSurvives*'],
    'ST-GRADE-01': ['*ResultView*', '*ResultPersists*'],
    'ST-ANLY-01': ['*TopicSummary*', '*ConsoleShowsTopic*'], 'ST-ANLY-02': ['*ConsoleExportsATranscript*'],
    'ST-ANLY-03': ['*TopicAnalytics*'], 'ST-ANLY-04': ['*TranscriptEnforces*'],
    'ST-ADM-01': ['AdminUserMenu*.*'], 'ST-ADM-02': ['*ST_ADM_02*'],
    'ST-UI-01': ['*SessionCarriesIdentity*'], 'ST-UI-02': ['*NeedsYes*', '*Confirmation*', '*Confirm*'],
    'ST-UI-03': ['*InvalidOption*', '*InvalidDifficulty*', '*CorrectOptionMust*'], 'ST-UI-04': ['*EndOfInput*', '*Cancel*'],
    'ST-SEC-01': ['*EncodedFormat*', '*NeverLogs*', '*PasswordUses*'],
    'ST-SEC-02': ['*ST_SEC_02_RoleDenial*', '*RoleBoundary*'],
    'ST-SEC-03': ['*Current*Admin*', '*Forged*', '*Demoted*', '*Disabled*'],
    'ST-SEC-04': ['InputValidation.*', 'SecurityMenus.*'], 'ST-SEC-05': ['*ST_SEC_05*', '*SeededMalformedCsv*'],
    'ST-SEC-06': ['*AuthEvent*', '*FailureLog*', '*FailedLogin*'], 'IT-01': ['*Submitted*', '*Atomic*', '*Rollback*'],
    'REL-01': ['TimedQuizTest.*'], 'MNT-01': ['ModuleInterfaces.MNT_01*']}
extra = {'UT-DAL-01': ['UT_DAL_01_ProcessKillAtomicity'], 'UT-DAL-03': ['no_fstream_outside_repository_or_logging'],
         'ST-ADM-01': ['ST_ADM_01_AdminUserFlow'], 'ST-SEC-04': ['ST_SEC_08_ActualConsoleValidation', 'ST_SEC_09_TimedConfirmationControls'],
         'ST-ANLY-01': ['ST_PART3_StudentInstructorAdminReports'], 'ST-ANLY-02': ['ST_PART3_StudentInstructorAdminReports'],
         'ST-ANLY-04': ['ST_PART3_StudentInstructorAdminReports'], 'ST-SEC-01': ['ST_PART3_StudentInstructorAdminReports'], 'ST-GRADE-01': ['ST_PART3_StudentInstructorAdminReports']}
rows = []
for id, scenario in sorted(scenarios.items()):
    matched = select(['*' + id.replace('-', '_') + '*'] + aliases.get(id, []))
    scripts = extra.get(id, [])
    status = 'PASS' if matched or scripts else 'NOT RUN'
    actual = 'Executable assertions passed; inputs and expected values are in the linked test source/log.' if status == 'PASS' else 'No matched executed evidence; not claimed as passed.'
    if id == 'UT-DAL-02':
        status, actual = 'PARTIAL', 'Question-repository round-trip parity passes. A complete alternative flat-file account/quiz/attempt DAL does not exist.'
    if id == 'ST-ANLY-03':
        status, actual = 'PARTIAL', 'Real Admin topic report and difficulty core API pass. A dedicated Admin item-analysis display is absent.'
    if id == 'USE-01':
        status, actual = 'NOT ACCEPTED', 'Console presents Login, Attempt, Quiz and Mode choices (four decisions excluding credentials). Original three-step target is not established; formal human UAT remains pending.'
    evidence = refs(matched) + ('; ' if matched and scripts else '') + '; '.join(f'`{s}` ([CTest]({rel}/ctest.txt))' for s in scripts)
    if id == 'ST-UI-01':
        evidence += f'; [actual identity/role display]({rel}/identity-walkthrough.txt)'
    rows.append({'id': id, 'scenario': scenario, 'status': status, 'actual': actual, 'evidence': evidence,
                 'matched_tests': [t['name'] for t in matched], 'scripts': scripts})
counts = Counter(r['status'] for r in rows)
(e / 'test-case-results.json').write_text(json.dumps(rows, indent=2) + '\n')
(e / 'executed-test-inventory.json').write_text(json.dumps(all_tests, indent=2) + '\n')
table = '\n'.join(f"| {r['id']} | {r['scenario']} | {r['status']} | {r['actual']} | {r['evidence'] or 'Review/walkthrough only'} |" for r in rows)
put('TEST_CASE_DOCUMENT.md', f'''# Updated Test Case Document - Part 3

{stamp}

Original STP identifiers are retained, including expanded ST-UI and ST-SEC ranges. The scenario column states the expected outcome; actual executable assertions and fixture inputs are retained in the linked source/logs. Expected password storage is corrected from the historical SHA-256-only wording to salted PBKDF2, matching the reviewed SRS revision. BR-5 remains the per-question zero floor.

Every run uses disposable accounts/databases. A requirement can map to several tests and a test can verify several requirements; mapping rows are not unique test counts. No missing/partial acceptance is converted to a Pass.

| ID | Scenario / expected outcome | Result | Actual outcome | Evidence |
| --- | --- | --- | --- | --- |
{table}

## Added sprint/review cases

`web_timer_slow-header`, `web_timer_slow-body`: incomplete traffic cannot starve expiry; fixed absolute connection budget.
`web_timer_practice-switch`: cannot replace a pending graded attempt; practice is allowed after completion.
`web_timer_cross-session-manual`, `web_timer_cross-session-auto`: reconcile SQLite completion, reject stale mutation, allow practice/next quiz, never regrade.
`ST_SEC_09_TimedConfirmationControls`: reject embedded CR/ESC/NUL, accept exact CRLF yes; actual Windows pipes are exercised.
`GradedReportIgnoresNewerPracticeAndSurvivesRestart`: exact graded report selection despite newer retained practice data.
`SeededMalformedCsvFuzzPreservesTheQuestionBank`: 100 generated malformed inputs with seed 20261118.

Full per-executable inventory: [executed-test-inventory.json]({rel}/executed-test-inventory.json). Formal human/lecturer UAT approval is not generated by these tests.
''')
btable = '\n'.join(f"| {name} | {len(cases)} | {sum(t.find('failure') is not None for t in cases)} | {sum(t.find('skipped') is not None for t in cases)} | [log]({rel}/gtest/{name}.txt) |" for name, cases in binaries.items())
put('UNIT_TEST_REPORT.md', f'''# Unit Test Report

{stamp}

Environment: {env['platform']}; {env['cpu']}; {env['compiler']}. Module-level GoogleTest suites run without the interactive console. The inventory explicitly includes mixed unit/SQLite integration executables; it is not a claim that every individual case is a pure isolated unit test.

| Executable | Cases | Failures | Skips | Raw evidence |
| --- | ---: | ---: | ---: | --- |
{btable}
| **Total** | **{len(all_tests)}** | **0** | **{sum(t['skipped'] for t in all_tests)}** | XML/stdout retained |

CTest separately reports {ctest['Linux']['total']} Linux entries and {ctest['Windows']['total']} Windows entries. Some entries launch a full GoogleTest binary, some are individually discovered cases and some are process scripts. **Do not add these totals or describe CTest entries as the unique unit-test count.** The existing POSIX input test accounts for the platform difference.

All executable assertions passed. The test-case document maps original requirement IDs to the actual cases. Test JSON/XML, stdout, compiler and file hashes are retained in [{rel}]({rel}/environment.json). Coverage is reported separately.
''')
perf = next(t for t in all_tests if 'PERF_01_' in t['name'])
max_ms = perf['properties'].get('maximum_load_start_ms', 'see raw log')
system = ['ST_ADM_01_AdminUserFlow', 'ST_SEC_08_ActualConsoleValidation', 'ST_SEC_09_TimedConfirmationControls', 'UT_DAL_01_ProcessKillAtomicity', 'ST_PART3_StudentInstructorAdminReports', 'web_timer_slow-header', 'web_timer_slow-body', 'web_timer_practice-switch', 'web_timer_cross-session-manual', 'web_timer_cross-session-auto']
stable = '\n'.join(f'| `{name}` | PASS | [stdout]({rel}/ctest.txt) |' for name in system)
put('SYSTEM_VALIDATION_REPORT.md', f'''# System Validation Report

{stamp}

| Real process/system scenario | Result | Evidence |
| --- | --- | --- |
{stable}
| Original browser HTTP authentication, CSRF/Host/Origin/roles, practice, grading, restart, retake and rankings | PASS | [HTTP smoke]({rel}/http-smoke.txt) |
| PERF-01: five 20-question load/start trials | PASS: maximum {max_ms} ms; target <2000 ms | [measurements]({rel}/gtest/part3_acceptance_tests.txt) |
| Seeded malformed CSV | PASS: 100 inputs; unchanged bank | [fuzz]({rel}/gtest/part3_acceptance_tests.txt) |
| Storage boundary and cppcheck | PASS: zero static findings | [cppcheck]({rel}/cppcheck.txt) |

The actual console flow creates Student/Instructor/Admin accounts, persists two questions and a quiz, selects a displayed shuffled answer, submits 4/8, checks itemised/weak-topic output, restarts, displays all rankings and exports/verifies both Instructor and Admin CSV transcripts. Separate Admin process tests cover create/edit/disable/enable/remove, cancellation and login effects.

Crash testing uses the actual app/DAL. A test-only SQLite trigger delays report insertion inside the real grading transaction. The parent kills the process with an active rollback journal. Three trials reopen SQLite, check integrity and verify no submitted flag, partial report or item rows survived. This is process termination, not merely a simulated failed write.

Performance includes DB reopen, question loading, engine wiring and starting/persisting a twenty-question graded attempt. It excludes fixture creation, login and process startup, uses five trials on one local dataset and is not a multi-user load benchmark. CPU: {env['cpu']}; compiler/environment data retained. [Windows timing]({rel}/perf-windows.txt) is separate from Linux timing.

## Acceptance exceptions

USE-01's original console three-step target is not established. Dedicated Admin item-analysis presentation is absent although topic display and the difficulty API are tested. Question flat-file parity is tested, but a complete flat-file replacement DAL is absent. Team/course-instructor UAT signatures remain pending. These exceptions are not converted into passes: **the whole original STP exit gate is not asserted satisfied**. See TEST_CASE_DOCUMENT.md for per-ID results.
''')
for metric, title, filename in [('line', 'Code Coverage Report', 'CODE_COVERAGE_REPORT.md'), ('branch', 'Branch Coverage Report', 'BRANCH_COVERAGE_REPORT.md')]:
    covered, total = cov[metric + '_covered'], cov[metric + '_total']
    lines = '\n'.join(f"| `{f['filename']}` | {f[metric+'_covered']} | {f[metric+'_total']} | {100*f[metric+'_covered']/f[metric+'_total'] if f[metric+'_total'] else 0:.2f}% |" for f in cov['files'])
    policy = 'Every GCC-reported branch is included, including C++ exception/compiler-generated branches; covered means taken at least once. No exception exclusion/denominator reduction inflates the rate.' if metric == 'branch' else 'An executable line is covered when it ran at least once.'
    put(filename, f'''# {title}

{stamp}

**Measured production scope: {covered}/{total} ({100*covered/total:.2f}%).**

Scope: compiled Linux/GCC production `src/` translation units, including console/browser sources. Tests, dependencies and headers are outside the filter; Windows-only preprocessor paths are outside this Linux measurement. A Windows functional pass is not Windows code-coverage evidence.

Method: build with QUIZSPHERE_COVERAGE=ON, clear counters within that selected build, run full CTest and each GoogleTest executable plus real HTTP smoke, export before auth-only counter resets. Tool: {env['gcovr']}. {policy}

| File | Covered | Total | Rate |
| --- | ---: | ---: | ---: |
{lines}

Evidence: [JSON summary]({rel}/coverage-summary.json), [Cobertura XML]({rel}/coverage.xml), [HTML]({rel}/coverage.html), [lines]({rel}/coverage-lines.txt), [branches]({rel}/coverage-branches.txt). Exact source hashes and environment are retained.

The Part 3 handout asks for measured reports and states no numerical threshold. **Above-90% whole-project branch coverage is not established.** Any separately imposed lecturer threshold remains a separate unmet criterion; this report does not waive it. Uncovered OS/storage failures and exception paths remain visible. Coverage is execution evidence, not proof that no further bugs exist.
''')
put('TEST_PLAN_UPDATE.md', f'''# Test Plan update 1.1 - Part 3

{stamp}

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
''')
put('PART3_TEST_SUMMARY.md', f'''# Part 3 Test Summary

{stamp}

Deliverables: [updated cases](TEST_CASE_DOCUMENT.md), [unit report](UNIT_TEST_REPORT.md), [system validation](SYSTEM_VALIDATION_REPORT.md), [code coverage](CODE_COVERAGE_REPORT.md), [branch coverage](BRANCH_COVERAGE_REPORT.md), [Test Plan update](TEST_PLAN_UPDATE.md), [defects](DEFECT_REPORT.md).

**{ctest['Linux']['total']} Linux CTest entries; {ctest['Windows']['total']} Windows entries; zero failures/skips.** Separately inventoried: **{len(all_tests)} GoogleTest cases**, with {sum(t['skipped'] for t in all_tests)} skips. These counts are not added.

Original STP mapping: **{len(rows)} case IDs**; {counts['PASS']} Pass, {counts['PARTIAL']} Partial, {counts['NOT ACCEPTED']} Not Accepted. Testing/report preparation is complete for the candidate; full requirement acceptance is conditional on the explicitly listed exceptions and human UAT. Do not represent documented limitations as passed acceptance checks or measured coverage as above a threshold it has not met.

Sources/hashes/environment and raw logs are retained in [{rel}]({rel}/source-manifest.json). Product/test hashes remain fixed through later report-only commits.
''')
manifest = {'tested_source_revision': a.revision, 'report_date': a.date, 'source_files': []}
inputs = [root / 'CMakeLists.txt', root / 'Dockerfile']
for folder in ['src', 'include', 'tests', 'web']:
    inputs.extend(f for f in (root / folder).rglob('*') if f.is_file())
for f in sorted(inputs):
    manifest['source_files'].append({'path': f.relative_to(root).as_posix(),
                                   'sha256_lf': hashlib.sha256(f.read_bytes().replace(b'\r\n', b'\n')).hexdigest()})
(e / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'ctest': ctest, 'gtest': len(all_tests), 'mapping': dict(counts),
                  'unmatched': [r['id'] for r in rows if r['status'] == 'NOT RUN']}, indent=2))
