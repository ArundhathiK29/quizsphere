# SQLite persistence (BL-06)

The application stores questions in the database selected by `QUIZSPHERE_DB`, alongside accounts, quizzes, attempts and results. Use `createSqliteQuestionRepository(path, &error)` with the same file path passed to `createSqliteDataAccessLayer(path, &error)`. The existing QuestionRepository virtual methods and shared domain structs are unchanged; the new factory is an additive declaration for review with Aryan. CSV reads and exports reuse the file repository's existing implementation.

Question options have their own ordered table. Subject, difficulty, text, answer index and availability round-trip through SQLite. Saving a question-bank snapshot, a quiz with its ordered questions, or an attempt with its answers is transactional. Saving an open attempt replaces the complete answer set, so removing an answer stays removed after restarting. Attempt ownership, quiz, mode and start time cannot change. Submitted attempts cannot be reopened or overwritten; repeated finalization returns `ATTEMPT_ALREADY_SUBMITTED` and preserves the first result.

Questions pooled by a saved quiz cannot be edited or deleted. Availability may change, without changing historical content. The SQLite repository checks references in its write transaction as well as through `isReferenced`, so calling `saveAll` directly cannot bypass the Question Bank guard. Quiz saves must reference stored questions; attempt questions must belong to their quiz. Store questions before creating quizzes. User IDs remain the shared interface's opaque string identifiers.

New tables declare foreign keys. Existing quiz/attempt tables from PR #56 are preserved, with additive indexes and triggers that enforce references on subsequent writes. Old rows are not deleted or fabricated: quizzes that previously referenced file-backed questions can still be read. Import the corresponding original questions before writing those associations again. A legacy file containing duplicate finalized graded attempts is rejected with the SQLite uniqueness error rather than silently discarding results. `:memory:` factories each own a separate database; use a shared file path when wiring multiple repositories together.

## Migrating existing questions

Back up the database and `data/questions.tsv`. Run once with an empty SQLite question bank:

```bash
QUIZSPHERE_IMPORT_QUESTIONS=data/questions.tsv ./build/quizsphere
```

The import validates the original bank and saves the snapshot atomically. The original TSV is retained. Unset the variable on subsequent launches. A nonempty SQLite question bank rejects the import rather than replacing its data.

## Verification

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

`data_access_layer_tests` covers question CRUD and tags, restart persistence for all entities, removed answers, immutable submitted results, real quiz references, invalid relationships, and injected failures during quiz, question-option, answer and grade writes. Existing fixtures now seed real question rows before pooling them.

Test-plan mapping: UT-DAL-01 is atomic attempt/answer storage (fault-injection regression plus existing graded-write rollback); UT-DAL-02 is file/SQLite question parity; UT-DAL-03 is the existing `no_fstream_outside_repository_or_logging` check plus storage-boundary review. Fault injection does not claim the separate process-termination scenario has been executed.

## Integration review

PR #59's real-DAL grading fixture currently writes questions to a file repository. It must seed the SQLite question repository at the same database path before saving quizzes under the stricter reference contract. Grading-specific repeat-submission behavior remains Chaturya's responsibility. PR #58 changes Question Bank fields and startup wiring: Aryan should retain the SQLite factory and adapt any added metadata in the same persistence transaction when integrating that PR.

Scoring rules are unchanged by this DAL work. Arundhathi and Chaturya still need to reconcile the documented per-question floor with PR #60's attempt-total floor, including topic totals. No grading implementation or console menu behavior is changed here.
