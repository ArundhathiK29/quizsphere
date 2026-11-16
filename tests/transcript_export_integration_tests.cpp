// Integration test (BL-42/#42): exportTranscriptCsv against the real SQLite DAL, Grading Module
// and the real file repository - writes an actual CSV file, re-reads it, refuses to overwrite
// without confirmation, overwrites with it, and reports an unwritable path as IO_ERROR.
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "analytics_module.hpp"
#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_engine.hpp"
#include "quiz_types.hpp"

namespace quizsphere {
namespace {

class TranscriptExportIntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path dir;
    std::string db_path;
    std::unique_ptr<DataAccessLayer> dal;
    const QuestionRepository* files{nullptr};
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grading;
    std::unique_ptr<QuizEngine> engine;
    std::unique_ptr<AnalyticsModule> analytics;
    const Session instructor{"1", Role::Instructor, "2026-11-16T00:00:00Z"};
    const Session alice{"alice", Role::Student, "2026-11-16T00:00:00Z"};

    void SetUp() override {
        dir = std::filesystem::temp_directory_path() /
              ("quizsphere-bl42-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(dir);
        db_path = (dir / "quiz.sqlite").string();

        std::string error;
        dal = createSqliteDataAccessLayer(db_path, &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repo = createSqliteQuestionRepository(db_path, &error);
        ASSERT_NE(repo, nullptr) << error;
        files = repo.get();
        bank = createQuestionBank(std::move(repo));
        ASSERT_TRUE(bank->load().ok());
        grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grading, 3);
        analytics = createAnalyticsModule(*bank, *dal->quizzes, *dal->attempts, files);

        ASSERT_TRUE(bank->addQuestion(instructor, Question{"q1", "Math", Difficulty::Easy, QuestionType::MCQ,
                                                          "1+1?", {"2", "3"}, 0, true}).ok());
        ASSERT_TRUE(bank->addQuestion(instructor, Question{"q2", "Math", Difficulty::Easy, QuestionType::MCQ,
                                                          "2+2?", {"4", "5"}, 0, true}).ok());
    }
    void TearDown() override {
        analytics.reset();
        engine.reset();
        grading.reset();
        bank.reset();
        dal.reset();
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    Quiz makeQuiz(const std::string& title, int count) {
        Quiz quiz;
        QuizConfig config{title, 15, count, std::nullopt, Difficulty::Easy, {1, 0}};
        EXPECT_TRUE(engine->createQuiz(instructor, config, &quiz).ok());
        return quiz;
    }
    void grade(const Quiz& quiz, int first_option) {
        Attempt attempt;
        ASSERT_TRUE(engine->startAttempt(alice, quiz.quiz_id, QuizMode::Graded, &attempt).ok());
        std::vector<Answer> answers;
        for (const std::string& qid : attempt.question_order) {
            answers.push_back(Answer{qid, first_option, false});
        }
        GradeReport report;
        ASSERT_TRUE(grading->gradeAttempt(quiz.quiz_id, alice.user_id, answers, &report).ok());
    }
    std::string readFile(const std::string& path) {
        std::string content;
        EXPECT_TRUE(files->readTextFile(path, &content).ok());
        return content;
    }
};

TEST_F(TranscriptExportIntegrationTest, WritesRereadsRefusesSilentOverwriteAndReportsBadPath) {
    const Quiz perfect = makeQuiz("Perfect, \"two\"", 2);
    const Quiz zero = makeQuiz("Zero", 1);
    grade(perfect, 0);  // 2/2
    grade(zero, 1);     // 0/1
    // A practice attempt is never in the transcript (BR-4).
    Attempt practice;
    ASSERT_TRUE(engine->startAttempt(alice, perfect.quiz_id, QuizMode::Practice, &practice).ok());

    const std::string out = (dir / "alice.csv").string();
    const Result first = analytics->exportTranscriptCsv(instructor, "alice", out, false);
    ASSERT_TRUE(first.ok()) << first.message;
    ASSERT_TRUE(std::filesystem::exists(out));

    const std::string content = readFile(out);
    EXPECT_EQ(content.rfind("quiz_id,quiz_title,score,max_score,percent,submitted_at\n", 0), 0u);
    EXPECT_NE(content.find(perfect.quiz_id + ",\"Perfect, \"\"two\"\"\",2.00,2.00,100.00,"), std::string::npos);
    EXPECT_NE(content.find(zero.quiz_id + ",Zero,0.00,1.00,0.00,"), std::string::npos);
    EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), 3);  // header + 2 graded rows

    // Same text the module reports without writing.
    std::string text;
    ASSERT_TRUE(analytics->transcriptCsv(instructor, "alice", &text).ok());
    EXPECT_EQ(text, content);

    // Exists -> confirmation required, file untouched; overwrite=true rewrites it.
    EXPECT_EQ(analytics->exportTranscriptCsv(instructor, "alice", out, false).code, ErrorCode::CONFIRMATION_REQUIRED);
    EXPECT_EQ(readFile(out), content);
    grade(makeQuiz("Third", 1), 0);
    ASSERT_TRUE(analytics->exportTranscriptCsv(instructor, "alice", out, true).ok());
    const std::string rewritten = readFile(out);
    EXPECT_EQ(std::count(rewritten.begin(), rewritten.end(), '\n'), 4);

    // Unwritable location: the "directory" component is actually the existing CSV file, so no
    // file can be created beneath it on any platform -> IO_ERROR, nothing created.
    const std::string bad = (std::filesystem::path(out) / "x.csv").string();
    EXPECT_EQ(analytics->exportTranscriptCsv(instructor, "alice", bad, true).code, ErrorCode::IO_ERROR);
    EXPECT_FALSE(std::filesystem::exists(bad));

    // Student may export their own, not someone else's.
    const std::string own = (dir / "own.csv").string();
    EXPECT_TRUE(analytics->exportTranscriptCsv(alice, "alice", own, false).ok());
    const Session bob{"bob", Role::Student, "2026-11-16T00:00:00Z"};
    EXPECT_EQ(analytics->exportTranscriptCsv(bob, "alice", (dir / "bob.csv").string(), false).code,
              ErrorCode::NOT_AUTHORIZED);
    EXPECT_FALSE(std::filesystem::exists(dir / "bob.csv"));
}

TEST_F(TranscriptExportIntegrationTest, StudentWithNoGradedAttemptsGetsAHeaderOnlyFile) {
    const std::string out = (dir / "empty.csv").string();
    ASSERT_TRUE(analytics->exportTranscriptCsv(instructor, "alice", out, false).ok());
    EXPECT_EQ(readFile(out), "quiz_id,quiz_title,score,max_score,percent,submitted_at\n");
}

}  // namespace
}  // namespace quizsphere
