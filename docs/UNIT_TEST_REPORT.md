# Unit Test Report

Report date: **2026-11-19 (Asia/Calcutta)**. Tested source: [`cce8549061143fab649eab618051becc6ffc06e7`](https://github.com/ArundhathiK29/quizsphere/commit/cce8549061143fab649eab618051becc6ffc06e7). Later report-only commits preserve the tested source/test hashes in [evidence/part3-2026-11-19/source-manifest.json](evidence/part3-2026-11-19/source-manifest.json).

Environment: Linux-6.18.58.1-microsoft-standard-WSL2-x86_64-with-glibc2.39; 12th Gen Intel(R) Core(TM) i5-1235U; g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0. Module-level GoogleTest suites run without the interactive console. The inventory explicitly includes mixed unit/SQLite integration executables; it is not a claim that every individual case is a pure isolated unit test.

| Executable | Cases | Failures | Skips | Raw evidence |
| --- | ---: | ---: | ---: | --- |
| admin_user_menu_tests | 14 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/admin_user_menu_tests.txt) |
| analytics_module_sqlite_integration_tests | 2 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/analytics_module_sqlite_integration_tests.txt) |
| analytics_module_tests | 34 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/analytics_module_tests.txt) |
| auth_tests | 47 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/auth_tests.txt) |
| data_access_layer_tests | 33 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/data_access_layer_tests.txt) |
| grading_module_sqlite_integration_tests | 3 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/grading_module_sqlite_integration_tests.txt) |
| grading_module_tests | 23 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/grading_module_tests.txt) |
| input_validation_tests | 7 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/input_validation_tests.txt) |
| part3_acceptance_tests | 3 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/part3_acceptance_tests.txt) |
| question_bank_tests | 45 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/question_bank_tests.txt) |
| quiz_engine_tests | 57 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/quiz_engine_tests.txt) |
| result_view_integration_tests | 3 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/result_view_integration_tests.txt) |
| result_view_tests | 6 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/result_view_tests.txt) |
| submitted_attempt_protection_tests | 3 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/submitted_attempt_protection_tests.txt) |
| topic_analytics_integration_tests | 2 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/topic_analytics_integration_tests.txt) |
| transcript_export_integration_tests | 2 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/transcript_export_integration_tests.txt) |
| web_application_tests | 6 | 0 | 0 | [log](evidence/part3-2026-11-19/gtest/web_application_tests.txt) |
| **Total** | **290** | **0** | **0** | XML/stdout retained |

CTest separately reports 210 Linux entries and 209 Windows entries. Some entries launch a full GoogleTest binary, some are individually discovered cases and some are process scripts. **Do not add these totals or describe CTest entries as the unique unit-test count.** The existing POSIX input test accounts for the platform difference.

All executable assertions passed. The test-case document maps original requirement IDs to the actual cases. Test JSON/XML, stdout, compiler and file hashes are retained in [evidence/part3-2026-11-19](evidence/part3-2026-11-19/environment.json). Coverage is reported separately.
