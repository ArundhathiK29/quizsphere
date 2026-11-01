#include "data_access_layer.hpp"
#include "sqlite_user_repository.hpp"
#include "question_repository.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>

#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace quizsphere {
namespace {

class DB {
public:
    sqlite3* db = nullptr;
    std::string error;

    explicit DB(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            error = db ? sqlite3_errmsg(db) : "Database open failed";
            sqlite3_close(db);
            db = nullptr;
            return;
        }

        sqlite3_busy_timeout(db, 5000);
        const char* sql =
            "PRAGMA foreign_keys=ON;"
            "BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS questions("
            "id TEXT PRIMARY KEY NOT NULL,subject TEXT NOT NULL,difficulty INTEGER NOT NULL CHECK(difficulty BETWEEN 0 AND 2),"
            "text TEXT NOT NULL,correct INTEGER NOT NULL CHECK(correct>=0),available INTEGER NOT NULL CHECK(available IN(0,1)));"
            "CREATE TABLE IF NOT EXISTS question_options("
            "qid TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,pos INTEGER NOT NULL CHECK(pos>=0),"
            "text TEXT NOT NULL,PRIMARY KEY(qid,pos));"
            "CREATE TABLE IF NOT EXISTS quizzes("
            "id TEXT PRIMARY KEY,title TEXT,creator TEXT,created TEXT,"
            "minutes INTEGER,marks REAL,negative REAL);"
            "CREATE TABLE IF NOT EXISTS quiz_q("
            "quiz TEXT REFERENCES quizzes(id),pos INTEGER,qid TEXT REFERENCES questions(id),"
            "PRIMARY KEY(quiz,pos));"
            "CREATE TABLE IF NOT EXISTS attempts("
            "id TEXT PRIMARY KEY,quiz TEXT REFERENCES quizzes(id),student TEXT,mode INTEGER,"
            "started TEXT,submitted_at TEXT,submitted INTEGER,"
            "auto INTEGER,score REAL);"
            "CREATE INDEX IF NOT EXISTS attempt_lookup "
            "ON attempts(quiz,student);"
            "CREATE TABLE IF NOT EXISTS attempt_q("
            "attempt TEXT REFERENCES attempts(id),pos INTEGER,qid TEXT,"
            "PRIMARY KEY(attempt,pos));"
            "CREATE TABLE IF NOT EXISTS answers("
            "attempt TEXT REFERENCES attempts(id),qid TEXT,selected INTEGER,flagged INTEGER,"
            "PRIMARY KEY(attempt,qid));"
            "CREATE TABLE IF NOT EXISTS reports("
            "attempt TEXT PRIMARY KEY REFERENCES attempts(id),score REAL,max_score REAL,auto INTEGER);"
            "CREATE TABLE IF NOT EXISTS items("
            "attempt TEXT REFERENCES attempts(id),pos INTEGER,qid TEXT,selected INTEGER,"
            "correct_option INTEGER,correct INTEGER,blank INTEGER,marks REAL,"
            "PRIMARY KEY(attempt,pos));"
            "CREATE TABLE IF NOT EXISTS topics("
            "attempt TEXT REFERENCES attempts(id),pos INTEGER,tag TEXT,correct INTEGER,total INTEGER,"
            "marks REAL,PRIMARY KEY(attempt,pos));"
            "CREATE INDEX IF NOT EXISTS quiz_question_lookup ON quiz_q(qid);"
            "CREATE INDEX IF NOT EXISTS attempt_question_lookup ON attempt_q(qid);"
            "CREATE UNIQUE INDEX IF NOT EXISTS single_graded_submission ON attempts(quiz,student) WHERE mode=1 AND submitted=1;"
            "COMMIT;";

        if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(db);
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            db = nullptr;
        }
        if (db && !installIntegrity()) {
            error = sqlite3_errmsg(db);
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            db = nullptr;
        }
    }

    bool installIntegrity() {
        if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        const std::vector<std::pair<std::string, std::string>> relations = {
            {"quiz_q", "NEW.quiz IS NULL OR NOT EXISTS(SELECT 1 FROM quizzes WHERE id=NEW.quiz) OR NEW.qid IS NULL OR NOT EXISTS(SELECT 1 FROM questions WHERE id=NEW.qid)"},
            {"attempts", "NEW.quiz IS NULL OR NOT EXISTS(SELECT 1 FROM quizzes WHERE id=NEW.quiz)"},
            {"attempt_q", "NEW.attempt IS NULL OR NOT EXISTS(SELECT 1 FROM attempts WHERE id=NEW.attempt) OR NOT EXISTS(SELECT 1 FROM quiz_q q JOIN attempts a ON a.quiz=q.quiz WHERE a.id=NEW.attempt AND q.qid=NEW.qid)"},
            {"answers", "NOT EXISTS(SELECT 1 FROM attempt_q WHERE attempt=NEW.attempt AND qid=NEW.qid)"},
            {"reports", "NOT EXISTS(SELECT 1 FROM attempts WHERE id=NEW.attempt)"},
            {"items", "NOT EXISTS(SELECT 1 FROM attempts WHERE id=NEW.attempt)"},
            {"topics", "NOT EXISTS(SELECT 1 FROM attempts WHERE id=NEW.attempt)"}
        };
        // Options may be inserted when restoring an original legacy question referenced by an old quiz.
        // Existing content changes are guarded by saveAll inside the same transaction.
        if (sqlite3_exec(db, "DROP TRIGGER IF EXISTS protect_options_INSERT", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        for (const std::string event : {"UPDATE", "DELETE"}) {
            const std::string row = "OLD";
            const std::string trigger = "CREATE TRIGGER IF NOT EXISTS protect_options_v2_" + event + " BEFORE " + event +
                " ON question_options WHEN EXISTS(SELECT 1 FROM quiz_q WHERE qid=" + row +
                ".qid) OR EXISTS(SELECT 1 FROM attempt_q WHERE qid=" + row +
                ".qid) BEGIN SELECT RAISE(ABORT,'Question in use'); END;";
            if (sqlite3_exec(db, trigger.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        }
        for (const auto& relation : relations) {
            for (const std::string event : {"INSERT", "UPDATE"}) {
                const std::string trigger = "CREATE TRIGGER IF NOT EXISTS integrity_" + relation.first + "_" + event +
                    " BEFORE " + event + " ON " + relation.first + " WHEN " + relation.second +
                    " BEGIN SELECT RAISE(ABORT,'Invalid storage reference'); END;";
                if (sqlite3_exec(db, trigger.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) return false;
            }
        }
        for (const std::string table : {"answers", "attempt_q", "reports", "items", "topics"}) {
            for (const std::string event : {"INSERT", "UPDATE", "DELETE"}) {
                const std::string row = event == "INSERT" ? "NEW" : "OLD";
                const std::string trigger = "CREATE TRIGGER IF NOT EXISTS finalized_" + table + "_" + event +
                    " BEFORE " + event + " ON " + table +
                    " WHEN EXISTS(SELECT 1 FROM attempts WHERE id=" + row + ".attempt AND submitted=1) "
                    "BEGIN SELECT RAISE(ABORT,'Finalized data cannot change'); END;";
                if (sqlite3_exec(db, trigger.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) return false;
            }
        }
        const char* protection =
            "CREATE TRIGGER IF NOT EXISTS protect_question_delete_v2 BEFORE DELETE ON questions "
            "WHEN EXISTS(SELECT 1 FROM quiz_q WHERE qid=OLD.id) OR EXISTS(SELECT 1 FROM attempt_q WHERE qid=OLD.id) BEGIN SELECT RAISE(ABORT,'Question in use'); END;"
            "CREATE TRIGGER IF NOT EXISTS protect_question_edit_v2 BEFORE UPDATE ON questions "
            "WHEN (EXISTS(SELECT 1 FROM quiz_q WHERE qid=OLD.id) OR EXISTS(SELECT 1 FROM attempt_q WHERE qid=OLD.id)) AND "
            "(NEW.id!=OLD.id OR NEW.subject!=OLD.subject OR NEW.difficulty!=OLD.difficulty OR NEW.text!=OLD.text OR NEW.correct!=OLD.correct) "
            "BEGIN SELECT RAISE(ABORT,'Question in use'); END;"
            "CREATE TRIGGER IF NOT EXISTS protect_quiz_delete BEFORE DELETE ON quizzes "
            "WHEN EXISTS(SELECT 1 FROM attempts WHERE quiz=OLD.id) OR EXISTS(SELECT 1 FROM quiz_q WHERE quiz=OLD.id) "
            "BEGIN SELECT RAISE(ABORT,'Quiz in use'); END;"
            "CREATE TRIGGER IF NOT EXISTS protect_attempt_delete BEFORE DELETE ON attempts "
            "WHEN OLD.submitted=1 OR EXISTS(SELECT 1 FROM attempt_q WHERE attempt=OLD.id) OR EXISTS(SELECT 1 FROM reports WHERE attempt=OLD.id) "
            "BEGIN SELECT RAISE(ABORT,'Attempt in use'); END;"
            "CREATE TRIGGER IF NOT EXISTS protect_submitted_attempt BEFORE UPDATE ON attempts WHEN OLD.submitted=1 "
            "BEGIN SELECT RAISE(ABORT,'Attempt already submitted'); END;"
            "COMMIT;";
        return sqlite3_exec(db, protection, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;
    ~DB() { sqlite3_close(db); }
    bool ok() const { return db != nullptr; }
};

class Stmt {
public:
    sqlite3_stmt* s = nullptr;

    Stmt(sqlite3* db, const char* sql) {
        sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    ~Stmt() { sqlite3_finalize(s); }

    bool ok() const { return s != nullptr; }

    void text(int i, const std::string& v) {
        sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT);
    }

    void num(int i, int v) { sqlite3_bind_int(s, i, v); }
    void real(int i, double v) { sqlite3_bind_double(s, i, v); }

    std::string str(int i) {
        const auto* p = sqlite3_column_text(s, i);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
};

Result fail(sqlite3* db, const std::string& what) {
    return {ErrorCode::IO_ERROR,
            what + ": " + (db ? sqlite3_errmsg(db) : "database closed")};
}

Result missing(ErrorCode code, const std::string& what) {
    return {code, what + " not found"};
}

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

Result saveOrder(sqlite3* db, const std::string& id,
                 const std::vector<std::string>& order,
                 const char* table) {
    std::string sql = std::string("INSERT INTO ") + table +
                      " VALUES(?1,?2,?3)";
    Stmt s(db, sql.c_str());
    if (!s.ok()) return fail(db, "Prepare question order");

    for (std::size_t i = 0; i < order.size(); ++i) {
        sqlite3_reset(s.s);
        sqlite3_clear_bindings(s.s);
        s.text(1, id);
        s.num(2, static_cast<int>(i));
        s.text(3, order[i]);

        if (sqlite3_step(s.s) != SQLITE_DONE)
            return fail(db, "Save question order");
    }

    return {};
}

Result saveAnswers(sqlite3* db, const Attempt& a) {
    Stmt clear(db, "DELETE FROM answers WHERE attempt=?1");
    if (!clear.ok()) return fail(db, "Prepare answer replacement");
    clear.text(1, a.attempt_id);
    if (sqlite3_step(clear.s) != SQLITE_DONE) return fail(db, "Clear answers");
    Stmt s(db,
        "INSERT INTO answers VALUES(?1,?2,?3,?4) "
        "ON CONFLICT(attempt,qid) DO UPDATE SET "
        "selected=excluded.selected,flagged=excluded.flagged");

    if (!s.ok()) return fail(db, "Prepare answers");

    for (const auto& x : a.answers) {
        sqlite3_reset(s.s);
        sqlite3_clear_bindings(s.s);
        s.text(1, a.attempt_id);
        s.text(2, x.question_id);
        s.num(3, x.selected_option);
        s.num(4, x.flagged_for_review);

        if (sqlite3_step(s.s) != SQLITE_DONE)
            return fail(db, "Save answer");
    }

    return {};
}

Result loadAttempt(sqlite3* db, const std::string& id, Attempt* out) {
    if (!out)
        return {ErrorCode::VALIDATION_FAILED, "Null attempt output"};

    Stmt s(db, "SELECT quiz,student,mode,started,submitted_at,"
               "submitted,auto,score FROM attempts WHERE id=?1");

    if (!s.ok()) return fail(db, "Prepare attempt lookup");
    s.text(1, id);

    const int lookup_rc = sqlite3_step(s.s);
    if (lookup_rc == SQLITE_DONE) return missing(ErrorCode::ATTEMPT_NOT_FOUND, "Attempt");
    if (lookup_rc != SQLITE_ROW) return fail(db, "Read attempt");

    Attempt a;
    a.attempt_id = id;
    a.quiz_id = s.str(0);
    a.student_id = s.str(1);
    a.mode = sqlite3_column_int(s.s, 2) == 0
                 ? QuizMode::Practice : QuizMode::Graded;
    a.started_at = s.str(3);
    a.submitted_at = s.str(4);
    a.submitted = sqlite3_column_int(s.s, 5);
    a.auto_submitted = sqlite3_column_int(s.s, 6);
    a.score = sqlite3_column_double(s.s, 7);

    Stmt q(db, "SELECT qid FROM attempt_q WHERE attempt=?1 ORDER BY pos");
    if (!q.ok()) return fail(db, "Prepare attempt questions");

    q.text(1, id);
    int order_rc;
    while ((order_rc = sqlite3_step(q.s)) == SQLITE_ROW)
        a.question_order.push_back(q.str(0));
    if (order_rc != SQLITE_DONE) return fail(db, "Read question order");

    Stmt ans(db, "SELECT qid,selected,flagged FROM answers "
                 "WHERE attempt=?1 ORDER BY rowid");

    if (!ans.ok()) return fail(db, "Prepare answer lookup");

    ans.text(1, id);
    int ans_rc;
    while ((ans_rc = sqlite3_step(ans.s)) == SQLITE_ROW) {
        Answer x;
        x.question_id = ans.str(0);
        x.selected_option = sqlite3_column_int(ans.s, 1);
        x.flagged_for_review = sqlite3_column_int(ans.s, 2);
        a.answers.push_back(x);
    }

    if (ans_rc != SQLITE_DONE) return fail(db, "Read stored rows");

    *out = std::move(a);
    return {};
}

Result writeAttempt(sqlite3* db, const Attempt& a, bool update) {
    const std::unordered_set<std::string> order(a.question_order.begin(), a.question_order.end());
    if (order.empty() || order.size() != a.question_order.size() ||
        (a.mode != QuizMode::Practice && a.mode != QuizMode::Graded))
        return {ErrorCode::VALIDATION_FAILED, "Invalid attempt questions or mode"};
    std::unordered_set<std::string> answered;
    if (std::any_of(a.answers.begin(), a.answers.end(), [&](const Answer& answer) {
            return !order.count(answer.question_id) || !answered.insert(answer.question_id).second ||
                   answer.selected_option < kBlankAnswer;
        })) return {ErrorCode::VALIDATION_FAILED, "Invalid or duplicate attempt answer"};
    for (const auto& answer : a.answers) {
        Stmt options(db, "SELECT COUNT(*) FROM question_options WHERE qid=?1");
        if (!options.ok()) return fail(db, "Prepare answer validation");
        options.text(1, answer.question_id);
        if (sqlite3_step(options.s) != SQLITE_ROW) return fail(db, "Validate answer");
        if (answer.selected_option != kBlankAnswer && answer.selected_option >= sqlite3_column_int(options.s, 0))
            return {ErrorCode::VALIDATION_FAILED, "Answer option is out of range"};
    }
    const char* sql = update
        ? "UPDATE attempts SET quiz=?1,student=?2,mode=?3,started=?4,"
          "submitted_at=?5,submitted=?6,auto=?7,score=?8 WHERE id=?9"
        : "INSERT INTO attempts VALUES(?9,?1,?2,?3,?4,?5,?6,?7,?8)";

    Stmt s(db, sql);
    if (!s.ok()) return fail(db, "Prepare attempt save");

    s.text(1, a.quiz_id);
    s.text(2, a.student_id);
    s.num(3, a.mode == QuizMode::Practice ? 0 : 1);
    s.text(4, a.started_at);
    s.text(5, a.submitted_at);
    s.num(6, false);
    s.num(7, a.auto_submitted);
    s.real(8, a.score);
    s.text(9, a.attempt_id);

    if (sqlite3_step(s.s) != SQLITE_DONE)
        return fail(db, "Save attempt");

    if (update && sqlite3_changes(db) == 0)
        return missing(ErrorCode::ATTEMPT_NOT_FOUND, "Attempt");

    Stmt del(db, "DELETE FROM attempt_q WHERE attempt=?1");
    if (!del.ok()) return fail(db, "Prepare question order update");

    del.text(1, a.attempt_id);
    if (sqlite3_step(del.s) != SQLITE_DONE)
        return fail(db, "Clear question order");

    auto r = saveOrder(db, a.attempt_id, a.question_order, "attempt_q");
    if (!r.ok()) return r;

    return saveAnswers(db, a);
}

class SqliteQuestionRepo final : public QuestionRepository {
    DB db;
    std::unique_ptr<QuestionRepository> files = createFileQuestionRepository();
public:
    explicit SqliteQuestionRepo(const std::string& path) : db(path) {}
    bool ok() const { return db.ok(); }
    const std::string& error() const { return db.error; }

    Result loadAll(std::vector<Question>* out) override {
        if (!out) return {ErrorCode::VALIDATION_FAILED, "Null question output"};
        Stmt s(db.db, "SELECT id,subject,difficulty,text,correct,available FROM questions ORDER BY id");
        if (!s.ok()) return fail(db.db, "Prepare questions");
        std::vector<Question> loaded;
        int rc;
        while ((rc = sqlite3_step(s.s)) == SQLITE_ROW) {
            Question q;
            q.question_id = s.str(0);
            q.subject_tag = s.str(1);
            q.difficulty = static_cast<Difficulty>(sqlite3_column_int(s.s, 2));
            q.question_text = s.str(3);
            q.correct_option = static_cast<std::size_t>(sqlite3_column_int(s.s, 4));
            q.is_available = sqlite3_column_int(s.s, 5) != 0;
            Stmt options(db.db, "SELECT text FROM question_options WHERE qid=?1 ORDER BY pos");
            if (!options.ok()) return fail(db.db, "Prepare options");
            options.text(1, q.question_id);
            int option_rc;
            while ((option_rc = sqlite3_step(options.s)) == SQLITE_ROW) q.options.push_back(options.str(0));
            if (option_rc != SQLITE_DONE) return fail(db.db, "Read options");
            if (q.options.size() < 2 || q.correct_option >= q.options.size())
                return {ErrorCode::IO_ERROR, "Invalid stored question options"};
            loaded.push_back(std::move(q));
        }
        if (rc != SQLITE_DONE) return fail(db.db, "Read questions");
        *out = std::move(loaded);
        return {};
    }

    bool isReferenced(const std::string& id) const override {
        Stmt s(db.db, "SELECT 1 FROM quiz_q WHERE qid=?1 UNION ALL SELECT 1 FROM attempt_q WHERE qid=?1 LIMIT 1");
        if (!s.ok()) return true;  // A storage failure must not permit an unsafe edit.
        s.text(1, id);
        return sqlite3_step(s.s) != SQLITE_DONE;
    }

    Result saveAll(const std::vector<Question>& questions) override {
        std::unordered_set<std::string> ids;
        for (const auto& q : questions) {
            if (q.question_id.empty() || q.subject_tag.empty() || q.question_text.empty() ||
                q.options.size() < 2 || q.correct_option >= q.options.size() ||
                q.question_type != QuestionType::MCQ || static_cast<int>(q.difficulty) < 0 ||
                static_cast<int>(q.difficulty) > 2 || !ids.insert(q.question_id).second ||
                std::any_of(q.options.begin(), q.options.end(), [](const std::string& x) { return x.empty(); }))
                return {ErrorCode::VALIDATION_FAILED, "Invalid question data"};
        }
        if (!exec(db.db, "BEGIN IMMEDIATE")) return fail(db.db, "Begin questions");
        auto rollback = [this](const Result& result) { exec(db.db, "ROLLBACK"); return result; };
        std::vector<Question> old;
        auto result = loadAll(&old);
        if (!result.ok()) return rollback(result);
        for (const auto& q : old) {
            const auto next = std::find_if(questions.begin(), questions.end(), [&](const Question& x) { return x.question_id == q.question_id; });
            if (isReferenced(q.question_id) && (next == questions.end() || next->subject_tag != q.subject_tag ||
                next->difficulty != q.difficulty || next->question_text != q.question_text ||
                next->correct_option != q.correct_option || next->options != q.options))
                return rollback({ErrorCode::QUESTION_IN_USE, "Referenced question cannot be edited or deleted"});
            if (next == questions.end()) {
                Stmt del(db.db, "DELETE FROM questions WHERE id=?1");
                if (!del.ok()) return rollback(fail(db.db, "Prepare question deletion"));
                del.text(1, q.question_id);
                if (sqlite3_step(del.s) != SQLITE_DONE) return rollback(fail(db.db, "Delete question"));
            }
        }
        for (const auto& q : questions) {
            Stmt s(db.db, "INSERT INTO questions VALUES(?1,?2,?3,?4,?5,?6) ON CONFLICT(id) DO UPDATE SET "
                          "subject=excluded.subject,difficulty=excluded.difficulty,text=excluded.text,correct=excluded.correct,available=excluded.available");
            if (!s.ok()) return rollback(fail(db.db, "Prepare question save"));
            s.text(1, q.question_id); s.text(2, q.subject_tag); s.num(3, static_cast<int>(q.difficulty));
            s.text(4, q.question_text); s.num(5, static_cast<int>(q.correct_option)); s.num(6, q.is_available);
            if (sqlite3_step(s.s) != SQLITE_DONE) return rollback(fail(db.db, "Save question"));
            const auto previous = std::find_if(old.begin(), old.end(), [&](const Question& x) { return x.question_id == q.question_id; });
            if (previous != old.end() && isReferenced(q.question_id)) continue;  // Existing options are immutable once pooled.
            Stmt clear(db.db, "DELETE FROM question_options WHERE qid=?1");
            if (!clear.ok()) return rollback(fail(db.db, "Prepare option replacement"));
            clear.text(1, q.question_id);
            if (sqlite3_step(clear.s) != SQLITE_DONE) return rollback(fail(db.db, "Clear options"));
            Stmt option(db.db, "INSERT INTO question_options VALUES(?1,?2,?3)");
            if (!option.ok()) return rollback(fail(db.db, "Prepare option save"));
            for (std::size_t i=0; i<q.options.size(); ++i) {
                sqlite3_reset(option.s); sqlite3_clear_bindings(option.s);
                option.text(1, q.question_id); option.num(2, static_cast<int>(i)); option.text(3, q.options[i]);
                if (sqlite3_step(option.s) != SQLITE_DONE) return rollback(fail(db.db, "Save option"));
            }
        }
        if (!exec(db.db, "COMMIT")) return rollback(fail(db.db, "Commit questions"));
        return {};
    }

    Result readTextFile(const std::string& path, std::string* content) const override {
        return files->readTextFile(path, content);
    }
    Result writeTextFileAtomic(const std::string& path, const std::string& content, bool overwrite) const override {
        return files->writeTextFileAtomic(path, content, overwrite);
    }
};

class QuizRepo final : public QuizRepository {
    DB db;

public:
    explicit QuizRepo(const std::string& p) : db(p) {}

    bool ok() const { return db.ok(); }
    const std::string& error() const { return db.error; }

    Result saveQuiz(const Quiz& q) override {
        if (!db.ok()) return fail(db.db, "Save quiz");

        if (q.quiz_id.empty() || q.title.empty() ||
            std::all_of(q.title.begin(), q.title.end(), [](unsigned char c) { return std::isspace(c) != 0; }) ||
            q.time_limit_minutes <= 0 ||
            !std::isfinite(q.scoring.marks_per_question) ||
            q.scoring.marks_per_question <= 0 ||
            !std::isfinite(q.scoring.negative_mark) ||
            q.scoring.negative_mark < 0)
            return {ErrorCode::VALIDATION_FAILED, "Invalid quiz"};

        if (q.question_ids.empty() || std::unordered_set<std::string>(q.question_ids.begin(), q.question_ids.end()).size() != q.question_ids.size())
            return {ErrorCode::VALIDATION_FAILED, "Quiz requires distinct questions"};
        if (!exec(db.db, "BEGIN IMMEDIATE"))
            return fail(db.db, "Begin quiz transaction");

        Stmt s(db.db,
            "INSERT INTO quizzes VALUES(?1,?2,?3,?4,?5,?6,?7) "
            "ON CONFLICT(id) DO UPDATE SET title=excluded.title,"
            "creator=excluded.creator,created=excluded.created,"
            "minutes=excluded.minutes,marks=excluded.marks,"
            "negative=excluded.negative");

        if (!s.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare quiz");
        }

        s.text(1, q.quiz_id);
        s.text(2, q.title);
        s.text(3, q.created_by);
        s.text(4, q.created_at);
        s.num(5, q.time_limit_minutes);
        s.real(6, q.scoring.marks_per_question);
        s.real(7, q.scoring.negative_mark);

        if (sqlite3_step(s.s) != SQLITE_DONE) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Save quiz");
        }

        Stmt d(db.db, "DELETE FROM quiz_q WHERE quiz=?1");

        if (!d.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare quiz questions");
        }

        d.text(1, q.quiz_id);

        if (sqlite3_step(d.s) != SQLITE_DONE) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Clear quiz questions");
        }

        auto r = saveOrder(db.db, q.quiz_id, q.question_ids, "quiz_q");

        if (!r.ok()) {
            exec(db.db, "ROLLBACK");
            return r;
        }

        if (!exec(db.db, "COMMIT")) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Commit quiz");
        }

        return {};
    }

    Result findQuiz(const std::string& id, Quiz* out) const override {
        if (!db.ok()) return fail(db.db, "Find quiz");

        if (!out)
            return {ErrorCode::VALIDATION_FAILED, "Null quiz output"};

        Stmt s(db.db,
            "SELECT title,creator,created,minutes,marks,negative "
            "FROM quizzes WHERE id=?1");

        if (!s.ok()) return fail(db.db, "Prepare quiz lookup");

        s.text(1, id);

        const int lookup_rc = sqlite3_step(s.s);
        if (lookup_rc == SQLITE_DONE) return missing(ErrorCode::QUIZ_NOT_FOUND, "Quiz");
        if (lookup_rc != SQLITE_ROW) return fail(db.db, "Read quiz");

        Quiz q;
        q.quiz_id = id;
        q.title = s.str(0);
        q.created_by = s.str(1);
        q.created_at = s.str(2);
        q.time_limit_minutes = sqlite3_column_int(s.s, 3);
        q.scoring.marks_per_question = sqlite3_column_double(s.s, 4);
        q.scoring.negative_mark = sqlite3_column_double(s.s, 5);

        Stmt qs(db.db,
            "SELECT qid FROM quiz_q WHERE quiz=?1 ORDER BY pos");

        if (!qs.ok()) return fail(db.db, "Prepare quiz question lookup");

        qs.text(1, id);

        int order_rc;
        while ((order_rc = sqlite3_step(qs.s)) == SQLITE_ROW)
            q.question_ids.push_back(qs.str(0));
        if (order_rc != SQLITE_DONE) return fail(db.db, "Read quiz questions");

        *out = std::move(q);
        return {};
    }

    Result listQuizzes(std::vector<Quiz>* out) const override {
        if (!out)
            return {ErrorCode::VALIDATION_FAILED, "Null quiz list"};

        Stmt s(db.db, "SELECT id FROM quizzes ORDER BY created");

        if (!s.ok()) return fail(db.db, "Prepare quiz list");

        std::vector<Quiz> result;

        int list_rc;
        while ((list_rc = sqlite3_step(s.s)) == SQLITE_ROW) {
            Quiz q;
            auto r = findQuiz(s.str(0), &q);

            if (!r.ok()) return r;

            result.push_back(std::move(q));
        }

        if (list_rc != SQLITE_DONE) return fail(db.db, "Read stored list");
        *out = std::move(result);
        return {};
    }
};

class AttemptRepo final : public AttemptRepository {
    DB db;

public:
    explicit AttemptRepo(const std::string& p) : db(p) {}

    bool ok() const { return db.ok(); }
    const std::string& error() const { return db.error; }

    // FIX: insert a new attempt or update an existing open attempt.
    // Submitted attempts must never be modified through this method.
    Result saveInProgressAttempt(const Attempt& a) override {
        if (!db.ok())
            return fail(db.db, "Save attempt");

        if (a.attempt_id.empty() || a.quiz_id.empty() || a.student_id.empty() || a.started_at.empty() ||
            !a.submitted_at.empty() || !std::isfinite(a.score))
            return {ErrorCode::VALIDATION_FAILED, "Invalid open attempt"};
        if (a.submitted) {
            return {
                ErrorCode::VALIDATION_FAILED,
                "An in-progress attempt cannot be submitted through this method."
            };
        }

        if (!exec(db.db, "BEGIN IMMEDIATE"))
            return fail(db.db, "Begin attempt");

        bool update = false;

        // Scope the statement so it is finalized before writeAttempt().
        {
            Stmt existing(
                db.db,
                "SELECT submitted,quiz,student,mode,started FROM attempts WHERE id=?1"
            );

            if (!existing.ok()) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Prepare attempt lookup");
            }

            existing.text(1, a.attempt_id);
            const int rc = sqlite3_step(existing.s);

            if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Check existing attempt");
            }

            if (rc == SQLITE_ROW) {
                if (sqlite3_column_int(existing.s, 0) != 0) {
                    exec(db.db, "ROLLBACK");
                    return {
                        ErrorCode::ATTEMPT_ALREADY_SUBMITTED,
                        "Attempt already submitted"
                    };
                }

                if (existing.str(1) != a.quiz_id || existing.str(2) != a.student_id ||
                    sqlite3_column_int(existing.s, 3) != (a.mode == QuizMode::Practice ? 0 : 1) || existing.str(4) != a.started_at) {
                    exec(db.db, "ROLLBACK");
                    return {ErrorCode::VALIDATION_FAILED, "Attempt identity cannot change"};
                }
                update = true;
            }
        }

        auto result = writeAttempt(db.db, a, update);

        if (!result.ok()) {
            exec(db.db, "ROLLBACK");
            return result;
        }

        if (!exec(db.db, "COMMIT")) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Commit attempt");
        }

        return {};
    }

    Result findAttempt(const std::string& id, Attempt* out) const override {
        return db.ok()
            ? loadAttempt(db.db, id, out)
            : fail(db.db, "Find attempt");
    }

    Result findOpenAttempt(const std::string& quiz,
                           const std::string& student,
                           Attempt* out) const override {
        if (!db.ok()) return fail(db.db, "Find open attempt");

        if (!out)
            return {ErrorCode::VALIDATION_FAILED, "Null attempt output"};

        Stmt s(db.db,
            "SELECT id FROM attempts WHERE quiz=?1 AND student=?2 "
            "AND submitted=0 ORDER BY started DESC LIMIT 1");

        if (!s.ok()) return fail(db.db, "Prepare open attempt lookup");

        s.text(1, quiz);
        s.text(2, student);

        const int lookup_rc = sqlite3_step(s.s);
        if (lookup_rc == SQLITE_DONE) return missing(ErrorCode::ATTEMPT_NOT_FOUND, "Open attempt");
        if (lookup_rc != SQLITE_ROW) return fail(db.db, "Read open attempt");

        return loadAttempt(db.db, s.str(0), out);
    }

    Result saveGradedAttempt(const Attempt& a,
                             const GradeReport& g) override {
        if (!db.ok()) return fail(db.db, "Save graded attempt");
        if (!a.submitted || a.submitted_at.empty() || a.started_at.empty() || a.quiz_id.empty() ||
            a.attempt_id.empty() || a.student_id.empty() ||
            !std::isfinite(a.score) || !std::isfinite(g.score) || !std::isfinite(g.max_score) ||
            g.max_score < 0 || a.score != g.score || a.auto_submitted != g.auto_submitted ||
            std::any_of(g.itemised_breakdown.begin(), g.itemised_breakdown.end(), [](const ItemResult& x) {
                return x.question_id.empty() || !std::isfinite(x.marks_awarded);
            }) || std::any_of(g.topic_breakdown.begin(), g.topic_breakdown.end(), [](const TopicScore& x) {
                return !std::isfinite(x.marks) || x.correct < 0 || x.total < x.correct;
            }))
            return {ErrorCode::VALIDATION_FAILED, "Invalid finalized attempt/report"};

        if (!exec(db.db, "BEGIN IMMEDIATE"))
            return fail(db.db, "Begin grade transaction");

        if (a.mode == QuizMode::Graded) {
            Stmt d(db.db,
                "SELECT 1 FROM attempts WHERE quiz=?1 AND student=?2 "
                "AND mode=1 AND submitted=1 LIMIT 1");

            if (!d.ok()) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Check duplicate");
            }

            d.text(1, a.quiz_id);
            d.text(2, a.student_id);

            if (sqlite3_step(d.s) == SQLITE_ROW) {
                exec(db.db, "ROLLBACK");
                return {
                    ErrorCode::ATTEMPT_ALREADY_SUBMITTED,
                    "Graded attempt exists"
                };
            }
        }

        bool existingAttempt = false;

        {
            Stmt old(db.db,
                "SELECT submitted,quiz,student,mode,started FROM attempts WHERE id=?1");

            if (!old.ok()) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Check attempt");
            }

            old.text(1, a.attempt_id);
            const int rc = sqlite3_step(old.s);

            if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Check attempt");
            }

            if (rc == SQLITE_ROW) {
                if (sqlite3_column_int(old.s, 0) != 0) {
                    exec(db.db, "ROLLBACK");
                    return {
                        ErrorCode::ATTEMPT_ALREADY_SUBMITTED,
                        "Attempt already submitted"
                    };
                }

                if (old.str(1) != a.quiz_id || old.str(2) != a.student_id ||
                    sqlite3_column_int(old.s, 3) != (a.mode == QuizMode::Practice ? 0 : 1) || old.str(4) != a.started_at) {
                    exec(db.db, "ROLLBACK");
                    return {ErrorCode::VALIDATION_FAILED, "Attempt identity cannot change"};
                }
                existingAttempt = true;
            }
        }

        auto r = writeAttempt(db.db, a, existingAttempt);

        if (!r.ok()) {
            exec(db.db, "ROLLBACK");
            return r;
        }

        Stmt rep(db.db, "INSERT INTO reports VALUES(?1,?2,?3,?4)");

        if (!rep.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare report");
        }

        rep.text(1, a.attempt_id);
        rep.real(2, g.score);
        rep.real(3, g.max_score);
        rep.num(4, g.auto_submitted);

        if (sqlite3_step(rep.s) != SQLITE_DONE) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Save report");
        }

        Stmt it(db.db,
            "INSERT INTO items VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");

        if (!it.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare items");
        }

        for (std::size_t i = 0; i < g.itemised_breakdown.size(); ++i) {
            const auto& x = g.itemised_breakdown[i];

            sqlite3_reset(it.s);
            sqlite3_clear_bindings(it.s);

            it.text(1, a.attempt_id);
            it.num(2, static_cast<int>(i));
            it.text(3, x.question_id);
            it.num(4, x.selected_option);
            it.num(5, static_cast<int>(x.correct_option));
            it.num(6, x.correct);
            it.num(7, x.blank);
            it.real(8, x.marks_awarded);

            if (sqlite3_step(it.s) != SQLITE_DONE) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Save item result");
            }
        }

        Stmt tp(db.db,
            "INSERT INTO topics VALUES(?1,?2,?3,?4,?5,?6)");

        if (!tp.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare topics");
        }

        for (std::size_t i = 0; i < g.topic_breakdown.size(); ++i) {
            const auto& x = g.topic_breakdown[i];

            sqlite3_reset(tp.s);
            sqlite3_clear_bindings(tp.s);

            tp.text(1, a.attempt_id);
            tp.num(2, static_cast<int>(i));
            tp.text(3, x.subject_tag);
            tp.num(4, x.correct);
            tp.num(5, x.total);
            tp.real(6, x.marks);

            if (sqlite3_step(tp.s) != SQLITE_DONE) {
                exec(db.db, "ROLLBACK");
                return fail(db.db, "Save topic result");
            }
        }

        Stmt finalize(db.db, "UPDATE attempts SET submitted=1 WHERE id=?1");
        if (!finalize.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare finalization");
        }
        finalize.text(1, a.attempt_id);
        if (sqlite3_step(finalize.s) != SQLITE_DONE) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Finalize attempt");
        }

        if (!exec(db.db, "COMMIT")) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Commit grade");
        }

        return {};
    }

    Result findReport(const std::string& quiz,
                      const std::string& student,
                      GradeReport* out) const override {
        if (!db.ok()) return fail(db.db, "Find report");

        if (!out)
            return {ErrorCode::VALIDATION_FAILED, "Null report output"};

        Stmt s(db.db,
            "SELECT r.attempt,r.score,r.max_score,r.auto FROM reports r "
            "JOIN attempts a ON a.id=r.attempt WHERE a.quiz=?1 "
            "AND a.student=?2 ORDER BY a.submitted_at DESC LIMIT 1");

        if (!s.ok()) return fail(db.db, "Prepare report lookup");

        s.text(1, quiz);
        s.text(2, student);

        const int lookup_rc = sqlite3_step(s.s);
        if (lookup_rc == SQLITE_DONE) return missing(ErrorCode::ATTEMPT_NOT_FOUND, "Report");
        if (lookup_rc != SQLITE_ROW) return fail(db.db, "Read report");

        std::string id = s.str(0);

        GradeReport g;
        g.score = sqlite3_column_double(s.s, 1);
        g.max_score = sqlite3_column_double(s.s, 2);
        g.auto_submitted = sqlite3_column_int(s.s, 3);

        Stmt it(db.db,
            "SELECT qid,selected,correct_option,correct,blank,marks "
            "FROM items WHERE attempt=?1 ORDER BY pos");

        if (!it.ok()) return fail(db.db, "Prepare items lookup");

        it.text(1, id);

        int it_rc;
        while ((it_rc = sqlite3_step(it.s)) == SQLITE_ROW) {
            ItemResult x;
            x.question_id = it.str(0);
            x.selected_option = sqlite3_column_int(it.s, 1);
            x.correct_option =
                static_cast<std::size_t>(sqlite3_column_int(it.s, 2));
            x.correct = sqlite3_column_int(it.s, 3);
            x.blank = sqlite3_column_int(it.s, 4);
            x.marks_awarded = sqlite3_column_double(it.s, 5);
            g.itemised_breakdown.push_back(x);
        }

        if (it_rc != SQLITE_DONE) return fail(db.db, "Read stored rows");

        Stmt tp(db.db,
            "SELECT tag,correct,total,marks FROM topics "
            "WHERE attempt=?1 ORDER BY pos");

        if (!tp.ok()) return fail(db.db, "Prepare topics lookup");

        tp.text(1, id);

        int tp_rc;
        while ((tp_rc = sqlite3_step(tp.s)) == SQLITE_ROW) {
            TopicScore x;
            x.subject_tag = tp.str(0);
            x.correct = sqlite3_column_int(tp.s, 1);
            x.total = sqlite3_column_int(tp.s, 2);
            x.marks = sqlite3_column_double(tp.s, 3);
            g.topic_breakdown.push_back(x);
        }

        if (tp_rc != SQLITE_DONE) return fail(db.db, "Read stored rows");

        *out = std::move(g);
        return {};
    }

    Result listGradedAttempts(const std::string& quiz,
                              std::vector<Attempt>* out) const override {
        return list(
            "SELECT id FROM attempts WHERE quiz=?1 AND submitted=1 "
            "AND mode=1 ORDER BY submitted_at",
            quiz, out);
    }

    Result listAttemptsForStudent(const std::string& student,
                                  std::vector<Attempt>* out) const override {
        return list(
            "SELECT id FROM attempts WHERE student=?1 ORDER BY started",
            student, out);
    }

private:
    Result list(const char* sql,
                const std::string& key,
                std::vector<Attempt>* out) const {
        if (!db.ok()) return fail(db.db, "List attempts");

        if (!out)
            return {ErrorCode::VALIDATION_FAILED, "Null attempt list"};

        Stmt s(db.db, sql);

        if (!s.ok()) return fail(db.db, "Prepare attempt list");

        s.text(1, key);

        std::vector<Attempt> result;

        int list_rc;
        while ((list_rc = sqlite3_step(s.s)) == SQLITE_ROW) {
            Attempt a;
            auto r = loadAttempt(db.db, s.str(0), &a);

            if (!r.ok()) return r;

            result.push_back(std::move(a));
        }

        if (list_rc != SQLITE_DONE) return fail(db.db, "Read stored list");
        *out = std::move(result);
        return {};
    }
};

} // namespace

std::unique_ptr<QuestionRepository> createSqliteQuestionRepository(const std::string& path, std::string* error) {
    auto repository = std::make_unique<SqliteQuestionRepo>(path);
    if (!repository->ok()) {
        if (error) *error = repository->error();
        return nullptr;
    }
    if (error) error->clear();
    return repository;
}

std::unique_ptr<DataAccessLayer> createSqliteDataAccessLayer(
    const std::string& path, std::string* error) {
    auto users = std::make_unique<SQLiteUserRepository>(path);

    if (!users->isOpen()) {
        if (error) *error = users->error();
        return nullptr;
    }

    auto quizzes = std::make_unique<QuizRepo>(path);

    if (!quizzes->ok()) {
        if (error) *error = quizzes->error();
        return nullptr;
    }

    auto attempts = std::make_unique<AttemptRepo>(path);

    if (!attempts->ok()) {
        if (error) *error = attempts->error();
        return nullptr;
    }

    auto layer = std::make_unique<DataAccessLayer>();
    layer->users = std::move(users);
    layer->quizzes = std::move(quizzes);
    layer->attempts = std::move(attempts);

    if (error) error->clear();

    return layer;
}

} // namespace quizsphere