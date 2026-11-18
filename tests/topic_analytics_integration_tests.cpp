// Integration test (BL-41/#41): topic analytics on the Student result screen against the real
// SQLite DAL, Quiz Engine and Grading Module - after submitting, from "View my result", and after
// the database is closed and reopened. A practice attempt must not move the class average (BR-4).
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "analytics_module.hpp"
#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"
#include "question_types.hpp"
#include "quiz_console.hpp"
#include "quiz_engine.hpp"
#include "quiz_types.hpp"

namespace quizsphere {
namespace {

class TopicAnalyticsIntegrationTest : public ::testing::Test {
protected:
    std::string path;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grading;
    std::unique_ptr<QuizEngine> engine;
    std::unique_ptr<AnalyticsModule> analytics;
    const Session instructor{"1", Role::Instructor, "2026-11-16T00:00:00Z"};
    const Session alice{"alice", Role::Student, "2026-11-16T00:00:00Z"};
    const Session bob{"bob", Role::Student, "2026-11-16T00:00:00Z"};
    Quiz quiz;

    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer(path, &error);
        ASSERT_NE(dal, nullptr) << error;
        auto repo = createSqliteQuestionRepository(path, &error);
        ASSERT_NE(repo, nullptr) << error;
        bank = createQuestionBank(std::move(repo));
        ASSERT_TRUE(bank->load().ok());
        grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        engine = createQuizEngine(*bank, *dal->quizzes, *dal->attempts, *grading, 7);
        analytics = createAnalyticsModule(*bank, *dal->quizzes, *dal->attempts);
    }
    void close() {
        analytics.reset();
        engine.reset();
        grading.reset();
        bank.reset();
        dal.reset();
    }
    void SetUp() override {
        path = (std::filesystem::temp_directory_path() /
                ("quizsphere-bl41-" + std::to_string(std::random_device{}()) + ".sqlite"))
                   .string();
        open();
        // Two Math questions and one Science question; option 0 is always correct.
        ASSERT_TRUE(bank->addQuestion(instructor, Question{"m1", "Math", Difficulty::Easy, QuestionType::MCQ,
                                                          "2+2?", {"4", "5"}, 0, true}).ok());
        ASSERT_TRUE(bank->addQuestion(instructor, Question{"m2", "Math", Difficulty::Easy, QuestionType::MCQ,
                                                          "3+3?", {"6", "7"}, 0, true}).ok());
        ASSERT_TRUE(bank->addQuestion(instructor, Question{"s1", "Science", Difficulty::Easy, QuestionType::MCQ,
                                                          "H2O is?", {"water", "salt"}, 0, true}).ok());
        QuizConfig config{"Mixed Quiz", 15, 3, std::nullopt, Difficulty::Easy, {1, 0}};
        ASSERT_TRUE(engine->createQuiz(instructor, config, &quiz).ok());
    }
    void TearDown() override {
        close();
        std::error_code ignored;
        for (const std::string suffix : {"", "-journal", "-wal", "-shm"}) {
            std::filesystem::remove(path + suffix, ignored);
        }
    }

    // Grades a GRADED attempt for `student` answering with ORIGINAL option indexes per question.
    void gradeAttempt(const Session& student, int math1, int math2, int science) {
        Attempt attempt;
        ASSERT_TRUE(engine->startAttempt(student, quiz.quiz_id, QuizMode::Graded, &attempt).ok());
        std::vector<Answer> answers = {Answer{"m1", math1, false}, Answer{"m2", math2, false},
                                       Answer{"s1", science, false}};
        GradeReport report;
        ASSERT_TRUE(grading->gradeAttempt(quiz.quiz_id, student.user_id, answers, &report).ok());
    }
};

TEST_F(TopicAnalyticsIntegrationTest, ResultScreenShowsTopicsVsClassAverageAndWeakestAfterRestart) {
    gradeAttempt(alice, 0, 0, 1);  // Math 2/2, Science 0/1
    gradeAttempt(bob, 0, 1, 0);    // Math 1/2, Science 1/1

    // A practice attempt for alice's classmate carol: nothing stored, nothing counted (BR-4).
    const Session carol{"carol", Role::Student, "2026-11-16T00:00:00Z"};
    Attempt practice;
    ASSERT_TRUE(engine->startAttempt(carol, quiz.quiz_id, QuizMode::Practice, &practice).ok());

    // "View my result" for alice through the real console entry point.
    std::istringstream input(quiz.quiz_id + "\n");
    std::ostringstream output;
    runQuizResult(alice, *engine, *grading, *bank, input, output, analytics.get());
    std::string text = output.str();
    EXPECT_NE(text.find("Score: 2.00 / 3.00"), std::string::npos);
    EXPECT_NE(text.find("Science: 0/1 correct (0.0%), class average 50.0%  <- weakest"), std::string::npos);
    EXPECT_NE(text.find("Math: 2/2 correct (100.0%), class average 75.0%"), std::string::npos);
    EXPECT_NE(text.find("Weakest topic: Science"), std::string::npos);

    // Restart and view again: identical.
    close();
    open();
    ASSERT_NE(analytics, nullptr);
    std::istringstream again(quiz.quiz_id + "\n");
    std::ostringstream reopened;
    runQuizResult(alice, *engine, *grading, *bank, again, reopened, analytics.get());
    EXPECT_EQ(reopened.str(), text);

    // Without analytics (e.g. a caller that only has grading) the itemised result still prints.
    std::istringstream plain(quiz.quiz_id + "\n");
    std::ostringstream plain_output;
    runQuizResult(alice, *engine, *grading, *bank, plain, plain_output);
    EXPECT_NE(plain_output.str().find("Score: 2.00 / 3.00"), std::string::npos);
    EXPECT_EQ(plain_output.str().find("Weakest topic"), std::string::npos);
}

TEST_F(TopicAnalyticsIntegrationTest, StudentCannotReadAnotherStudentsTopicsButInstructorCan) {
    gradeAttempt(alice, 0, 0, 1);
    std::vector<TopicAnalytics> topics;
    EXPECT_EQ(analytics->topicAnalytics(bob, quiz.quiz_id, "alice", &topics).code, ErrorCode::NOT_AUTHORIZED);
    ASSERT_TRUE(analytics->topicAnalytics(instructor, quiz.quiz_id, "alice", &topics).ok());
    ASSERT_EQ(topics.size(), 2u);
    EXPECT_EQ(topics[0].subject_tag, "Science");
    EXPECT_TRUE(topics[0].weakest);
}

}  // namespace
}  // namespace quizsphere
