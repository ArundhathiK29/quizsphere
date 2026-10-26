#include "data_access_layer.hpp"
#include "sqlite_user_repository.hpp"

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

        const char* sql =
            "PRAGMA foreign_keys=ON;"
            "CREATE TABLE IF NOT EXISTS quizzes("
            "id TEXT PRIMARY KEY,title TEXT,creator TEXT,created TEXT,"
            "minutes INTEGER,marks REAL,negative REAL);"
            "CREATE TABLE IF NOT EXISTS quiz_q("
            "quiz TEXT,pos INTEGER,qid TEXT,"
            "PRIMARY KEY(quiz,pos));"
            "CREATE TABLE IF NOT EXISTS attempts("
            "id TEXT PRIMARY KEY,quiz TEXT,student TEXT,mode INTEGER,"
            "started TEXT,submitted_at TEXT,submitted INTEGER,"
            "auto INTEGER,score REAL);"
            "CREATE INDEX IF NOT EXISTS attempt_lookup "
            "ON attempts(quiz,student);"
            "CREATE TABLE IF NOT EXISTS attempt_q("
            "attempt TEXT,pos INTEGER,qid TEXT,"
            "PRIMARY KEY(attempt,pos));"
            "CREATE TABLE IF NOT EXISTS answers("
            "attempt TEXT,qid TEXT,selected INTEGER,flagged INTEGER,"
            "PRIMARY KEY(attempt,qid));"
            "CREATE TABLE IF NOT EXISTS reports("
            "attempt TEXT PRIMARY KEY,score REAL,max_score REAL,auto INTEGER);"
            "CREATE TABLE IF NOT EXISTS items("
            "attempt TEXT,pos INTEGER,qid TEXT,selected INTEGER,"
            "correct_option INTEGER,correct INTEGER,blank INTEGER,marks REAL,"
            "PRIMARY KEY(attempt,pos));"
            "CREATE TABLE IF NOT EXISTS topics("
            "attempt TEXT,pos INTEGER,tag TEXT,correct INTEGER,total INTEGER,"
            "marks REAL,PRIMARY KEY(attempt,pos));";

        if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(db);
            sqlite3_close(db);
            db = nullptr;
        }
    }

    ~DB() { sqlite3_close(db); }
    bool ok() const { return db != nullptr; }
};

class Stmt {
public:
    sqlite3_stmt* s = nullptr;

    Stmt(sqlite3* db, const char* sql) {
        sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
    }
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

    if (sqlite3_step(s.s) != SQLITE_ROW)
        return missing(ErrorCode::ATTEMPT_NOT_FOUND, "Attempt");

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
    while (sqlite3_step(q.s) == SQLITE_ROW)
        a.question_order.push_back(q.str(0));

    Stmt ans(db, "SELECT qid,selected,flagged FROM answers "
                 "WHERE attempt=?1 ORDER BY rowid");
    if (!ans.ok()) return fail(db, "Prepare answer lookup");
    ans.text(1, id);
    while (sqlite3_step(ans.s) == SQLITE_ROW) {
        Answer x;
        x.question_id = ans.str(0);
        x.selected_option = sqlite3_column_int(ans.s, 1);
        x.flagged_for_review = sqlite3_column_int(ans.s, 2);
        a.answers.push_back(x);
    }

    *out = std::move(a);
    return {};
}

Result writeAttempt(sqlite3* db, const Attempt& a, bool update) {
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
    s.num(6, a.submitted);
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

class QuizRepo final : public QuizRepository {
    DB db;
public:
    explicit QuizRepo(const std::string& p) : db(p) {}
    bool ok() const { return db.ok(); }
    std::string error() const { return db.error; }

    Result saveQuiz(const Quiz& q) override {
        if (!db.ok()) return fail(db.db, "Save quiz");
        if (q.quiz_id.empty() || q.title.empty() ||
            q.time_limit_minutes <= 0 ||
            q.scoring.marks_per_question < 0 ||
            q.scoring.negative_mark < 0)
            return {ErrorCode::VALIDATION_FAILED, "Invalid quiz"};

        if (!exec(db.db, "BEGIN IMMEDIATE"))
            return fail(db.db, "Begin quiz transaction");

        Stmt s(db.db, "INSERT INTO quizzes VALUES(?1,?2,?3,?4,?5,?6,?7) "
            "ON CONFLICT(id) DO UPDATE SET title=excluded.title,"
            "creator=excluded.creator,created=excluded.created,"
            "minutes=excluded.minutes,marks=excluded.marks,"
            "negative=excluded.negative");
        if (!s.ok()) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Prepare quiz");
        }
        s.text(1,q.quiz_id); s.text(2,q.title); s.text(3,q.created_by);
        s.text(4,q.created_at); s.num(5,q.time_limit_minutes);
        s.real(6,q.scoring.marks_per_question);
        s.real(7,q.scoring.negative_mark);
        if (sqlite3_step(s.s) != SQLITE_DONE) {
            exec(db.db, "ROLLBACK");
            return fail(db.db, "Save quiz");
        }

        Stmt d(db.db, "DELETE FROM quiz_q WHERE quiz=?1");
        if (!d.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Prepare quiz questions"); }
        d.text(1,q.quiz_id);
        if (sqlite3_step(d.s) != SQLITE_DONE) {
            exec(db.db,"ROLLBACK"); return fail(db.db,"Clear quiz questions");
        }
        auto r = saveOrder(db.db,q.quiz_id,q.question_ids,"quiz_q");
        if (!r.ok()) { exec(db.db,"ROLLBACK"); return r; }
        if (!exec(db.db,"COMMIT")) {
            exec(db.db,"ROLLBACK"); return fail(db.db,"Commit quiz");
        }
        return {};
    }

    Result findQuiz(const std::string& id, Quiz* out) const override {
        if (!db.ok()) return fail(db.db,"Find quiz");
        if (!out) return {ErrorCode::VALIDATION_FAILED,"Null quiz output"};
        Stmt s(db.db,"SELECT title,creator,created,minutes,marks,negative "
                     "FROM quizzes WHERE id=?1");
        if (!s.ok()) return fail(db.db,"Prepare quiz lookup");
        s.text(1,id);
        if (sqlite3_step(s.s)!=SQLITE_ROW)
            return missing(ErrorCode::QUIZ_NOT_FOUND,"Quiz");

        Quiz q;
        q.quiz_id=id; q.title=s.str(0); q.created_by=s.str(1);
        q.created_at=s.str(2); q.time_limit_minutes=sqlite3_column_int(s.s,3);
        q.scoring.marks_per_question=sqlite3_column_double(s.s,4);
        q.scoring.negative_mark=sqlite3_column_double(s.s,5);
        Stmt qs(db.db,"SELECT qid FROM quiz_q WHERE quiz=?1 ORDER BY pos");
        if (!qs.ok()) return fail(db.db,"Prepare quiz question lookup");
        qs.text(1,id);
        while (sqlite3_step(qs.s)==SQLITE_ROW) q.question_ids.push_back(qs.str(0));
        *out=std::move(q);
        return {};
    }

    Result listQuizzes(std::vector<Quiz>* out) const override {
        if (!out) return {ErrorCode::VALIDATION_FAILED,"Null quiz list"};
        Stmt s(db.db,"SELECT id FROM quizzes ORDER BY created");
        if (!s.ok()) return fail(db.db,"Prepare quiz list");
        std::vector<Quiz> result;
        while (sqlite3_step(s.s)==SQLITE_ROW) {
            Quiz q;
            auto r=findQuiz(s.str(0),&q);
            if (!r.ok()) return r;
            result.push_back(std::move(q));
        }
        *out=std::move(result);
        return {};
    }
};

class AttemptRepo final : public AttemptRepository {
    DB db;
public:
    explicit AttemptRepo(const std::string& p):db(p){}
    bool ok() const { return db.ok(); }
    std::string error() const { return db.error; }

    Result saveInProgressAttempt(const Attempt& a) override {
        if (!db.ok()) return fail(db.db,"Save attempt");
        if (!exec(db.db,"BEGIN IMMEDIATE")) return fail(db.db,"Begin attempt");
        auto r=writeAttempt(db.db,a,false);
        if (!r.ok()) { exec(db.db,"ROLLBACK"); return r; }
        if (!exec(db.db,"COMMIT")) {
            exec(db.db,"ROLLBACK"); return fail(db.db,"Commit attempt");
        }
        return {};
    }

    Result findAttempt(const std::string& id, Attempt* out) const override {
        return db.ok() ? loadAttempt(db.db,id,out) : fail(db.db,"Find attempt");
    }

    Result findOpenAttempt(const std::string& quiz,const std::string& student,
                           Attempt* out) const override {
        Stmt s(db.db,"SELECT id FROM attempts WHERE quiz=?1 AND student=?2 "
                     "AND submitted=0 ORDER BY started DESC LIMIT 1");
        if (!db.ok()) return fail(db.db,"Find open attempt");
        if (!s.ok()) return fail(db.db,"Prepare open attempt lookup");
        s.text(1,quiz); s.text(2,student);
        if (sqlite3_step(s.s)!=SQLITE_ROW)
            return missing(ErrorCode::ATTEMPT_NOT_FOUND,"Open attempt");
        return loadAttempt(db.db,s.str(0),out);
    }

    Result saveGradedAttempt(const Attempt& a,const GradeReport& g) override {
        if (!db.ok()) return fail(db.db,"Save graded attempt");
        if (!exec(db.db,"BEGIN IMMEDIATE")) return fail(db.db,"Begin grade transaction");

        if (a.mode==QuizMode::Graded) {
            Stmt d(db.db,"SELECT 1 FROM attempts WHERE quiz=?1 AND student=?2 "
                         "AND mode=1 AND submitted=1 LIMIT 1");
            if (!d.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Check duplicate"); }
            d.text(1,a.quiz_id); d.text(2,a.student_id);
            if (sqlite3_step(d.s)==SQLITE_ROW) {
                exec(db.db,"ROLLBACK");
                return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED,"Graded attempt exists"};
            }
        }

        Stmt old(db.db,"SELECT submitted FROM attempts WHERE id=?1");
        if (!old.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Check attempt"); }
        old.text(1,a.attempt_id);
        int rc=sqlite3_step(old.s);
        if (rc==SQLITE_ROW && sqlite3_column_int(old.s,0)) {
            exec(db.db,"ROLLBACK");
            return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED,"Attempt already submitted"};
        }

        auto r=writeAttempt(db.db,a,rc==SQLITE_ROW);
        if (!r.ok()) { exec(db.db,"ROLLBACK"); return r; }

        Stmt rep(db.db,"INSERT INTO reports VALUES(?1,?2,?3,?4)");
        if (!rep.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Prepare report"); }
        rep.text(1,a.attempt_id); rep.real(2,g.score);
        rep.real(3,g.max_score); rep.num(4,g.auto_submitted);
        if (sqlite3_step(rep.s)!=SQLITE_DONE) {
            exec(db.db,"ROLLBACK"); return fail(db.db,"Save report");
        }

        Stmt it(db.db,"INSERT INTO items VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
        if (!it.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Prepare items"); }
        for (std::size_t i=0;i<g.itemised_breakdown.size();++i) {
            const auto& x=g.itemised_breakdown[i];
            sqlite3_reset(it.s); sqlite3_clear_bindings(it.s);
            it.text(1,a.attempt_id); it.num(2,static_cast<int>(i));
            it.text(3,x.question_id); it.num(4,x.selected_option);
            it.num(5,static_cast<int>(x.correct_option)); it.num(6,x.correct);
            it.num(7,x.blank); it.real(8,x.marks_awarded);
            if (sqlite3_step(it.s)!=SQLITE_DONE) {
                exec(db.db,"ROLLBACK"); return fail(db.db,"Save item result");
            }
        }

        Stmt tp(db.db,"INSERT INTO topics VALUES(?1,?2,?3,?4,?5,?6)");
        if (!tp.ok()) { exec(db.db,"ROLLBACK"); return fail(db.db,"Prepare topics"); }
        for (std::size_t i=0;i<g.topic_breakdown.size();++i) {
            const auto& x=g.topic_breakdown[i];
            sqlite3_reset(tp.s); sqlite3_clear_bindings(tp.s);
            tp.text(1,a.attempt_id); tp.num(2,static_cast<int>(i));
            tp.text(3,x.subject_tag); tp.num(4,x.correct);
            tp.num(5,x.total); tp.real(6,x.marks);
            if (sqlite3_step(tp.s)!=SQLITE_DONE) {
                exec(db.db,"ROLLBACK"); return fail(db.db,"Save topic result");
            }
        }

        if (!exec(db.db,"COMMIT")) {
            exec(db.db,"ROLLBACK"); return fail(db.db,"Commit grade");
        }
        return {};
    }

    Result findReport(const std::string& quiz,const std::string& student,
                      GradeReport* out) const override {
        if (!db.ok()) return fail(db.db,"Find report");
        if (!out) return {ErrorCode::VALIDATION_FAILED,"Null report output"};
        Stmt s(db.db,"SELECT r.attempt,r.score,r.max_score,r.auto FROM reports r "
                     "JOIN attempts a ON a.id=r.attempt WHERE a.quiz=?1 "
                     "AND a.student=?2 ORDER BY a.submitted_at DESC LIMIT 1");
        if (!s.ok()) return fail(db.db,"Prepare report lookup");
        s.text(1,quiz); s.text(2,student);
        if (sqlite3_step(s.s)!=SQLITE_ROW)
            return missing(ErrorCode::ATTEMPT_NOT_FOUND,"Report");
        std::string id=s.str(0);
        GradeReport g;
        g.score=sqlite3_column_double(s.s,1);
        g.max_score=sqlite3_column_double(s.s,2);
        g.auto_submitted=sqlite3_column_int(s.s,3);

        Stmt it(db.db,"SELECT qid,selected,correct_option,correct,blank,marks "
                      "FROM items WHERE attempt=?1 ORDER BY pos");
        if (!it.ok()) return fail(db.db,"Prepare items lookup");
        it.text(1,id);
        while (sqlite3_step(it.s)==SQLITE_ROW) {
            ItemResult x;
            x.question_id=it.str(0); x.selected_option=sqlite3_column_int(it.s,1);
            x.correct_option=static_cast<std::size_t>(sqlite3_column_int(it.s,2));
            x.correct=sqlite3_column_int(it.s,3); x.blank=sqlite3_column_int(it.s,4);
            x.marks_awarded=sqlite3_column_double(it.s,5);
            g.itemised_breakdown.push_back(x);
        }

        Stmt tp(db.db,"SELECT tag,correct,total,marks FROM topics "
                      "WHERE attempt=?1 ORDER BY pos");
        if (!tp.ok()) return fail(db.db,"Prepare topics lookup");
        tp.text(1,id);
        while (sqlite3_step(tp.s)==SQLITE_ROW) {
            TopicScore x;
            x.subject_tag=tp.str(0); x.correct=sqlite3_column_int(tp.s,1);
            x.total=sqlite3_column_int(tp.s,2); x.marks=sqlite3_column_double(tp.s,3);
            g.topic_breakdown.push_back(x);
        }
        *out=std::move(g);
        return {};
    }

    Result listGradedAttempts(const std::string& quiz,
                              std::vector<Attempt>* out) const override {
        return list("SELECT id FROM attempts WHERE quiz=?1 AND submitted=1 "
                    "AND mode=1 ORDER BY submitted_at",quiz,out);
    }

    Result listAttemptsForStudent(const std::string& student,
                                 std::vector<Attempt>* out) const override {
        return list("SELECT id FROM attempts WHERE student=?1 ORDER BY started",
                    student,out);
    }

private:
    Result list(const char* sql,const std::string& key,
                std::vector<Attempt>* out) const {
        if (!db.ok()) return fail(db.db,"List attempts");
        if (!out) return {ErrorCode::VALIDATION_FAILED,"Null attempt list"};
        Stmt s(db.db,sql);
        if (!s.ok()) return fail(db.db,"Prepare attempt list");
        s.text(1,key);
        std::vector<Attempt> result;
        while (sqlite3_step(s.s)==SQLITE_ROW) {
            Attempt a;
            auto r=loadAttempt(db.db,s.str(0),&a);
            if (!r.ok()) return r;
            result.push_back(std::move(a));
        }
        *out=std::move(result);
        return {};
    }
};

} // namespace

std::unique_ptr<DataAccessLayer> createSqliteDataAccessLayer(
    const std::string& path,std::string* error) {
    auto users=std::make_unique<SQLiteUserRepository>(path);
    if (!users->isOpen()) {
        if (error) *error=users->error();
        return nullptr;
    }
    auto quizzes=std::make_unique<QuizRepo>(path);
    if (!quizzes->ok()) {
        if (error) *error=quizzes->error();
        return nullptr;
    }
    auto attempts=std::make_unique<AttemptRepo>(path);
    if (!attempts->ok()) {
        if (error) *error=attempts->error();
        return nullptr;
    }

    auto layer=std::make_unique<DataAccessLayer>();
    layer->users=std::move(users);
    layer->quizzes=std::move(quizzes);
    layer->attempts=std::move(attempts);
    if (error) error->clear();
    return layer;
}

} // namespace quizsphere