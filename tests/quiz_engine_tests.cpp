
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "data_access_layer.hpp"
#include "grading_module.hpp"
#include "question_bank.hpp"
#include "quiz_engine.hpp"

namespace quizsphere {
namespace {

class FakeQuestionBank final : public QuestionBank {
public:
    std::vector<PublicQuestion> available_questions;

    Result load() override {
        return {ErrorCode::OK, "Loaded."};
    }

    Result addQuestion(const Session&, const Question&) override {
        return notImplemented("FakeQuestionBank::addQuestion");
    }

    Result updateQuestion(const Session&, const Question&) override {
        return notImplemented("FakeQuestionBank::updateQuestion");
    }

    Result deleteQuestion(const Session&, const std::string&, bool) override {
        return notImplemented("FakeQuestionBank::deleteQuestion");
    }

    Result setAvailability(const Session&, const std::string&, bool) override {
        return notImplemented("FakeQuestionBank::setAvailability");
    }

    Result findQuestionById(const Session&, const std::string&,
                            Question*) const override {
        return notImplemented("FakeQuestionBank::findQuestionById");
    }

    Result listQuestions(
        const Session&, std::vector<Question>*, bool,
        const std::optional<std::string>&,
        const std::optional<Difficulty>&) const override {
        return notImplemented("FakeQuestionBank::listQuestions");
    }

    Result getQuestionForQuiz(const std::string&, PublicQuestion*) const override {
        return notImplemented("FakeQuestionBank::getQuestionForQuiz");
    }

    Result listAvailable(
        std::vector<PublicQuestion>* questions,
        const std::optional<std::string>& subject_filter,
        const std::optional<Difficulty>& difficulty_filter) const override {
        if (questions == nullptr) {
            return {ErrorCode::VALIDATION_FAILED,
                    "Questions output pointer must not be null."};
        }

        questions->clear();

        for (const auto& question : available_questions) {
            if (!question.is_available) {
                continue;
            }

            if (subject_filter && question.subject_tag != *subject_filter) {
                continue;
            }

            if (difficulty_filter &&
                question.difficulty != *difficulty_filter) {
                continue;
            }

            questions->push_back(question);
        }

        return {ErrorCode::OK, "Available questions retrieved."};
    }

    ImportReport importFromCsv(const Session&, const std::string&) override {
        return {notImplemented("FakeQuestionBank::importFromCsv"), 0, 0, {}};
    }

    Result exportToCsv(
        const Session&, const std::string&,
        const std::optional<std::string>&,
        const std::optional<Difficulty>&, bool) const override {
        return notImplemented("FakeQuestionBank::exportToCsv");
    }
};

class FakeQuizRepository final : public QuizRepository {
public:
    std::vector<Quiz> saved_quizzes;

    Result saveQuiz(const Quiz& quiz) override {
        const auto duplicate = std::find_if(
            saved_quizzes.begin(), saved_quizzes.end(),
            [&quiz](const Quiz& saved) {
                return saved.quiz_id == quiz.quiz_id;
            });

        if (duplicate != saved_quizzes.end()) {
            return {ErrorCode::DUPLICATE_ID, "Duplicate quiz ID."};
        }

        saved_quizzes.push_back(quiz);
        return {ErrorCode::OK, "Quiz saved."};
    }

    Result findQuiz(const std::string& quiz_id, Quiz* quiz) const override {
        if (quiz == nullptr) {
            return {ErrorCode::VALIDATION_FAILED, "Quiz pointer is null."};
        }

        const auto found = std::find_if(
            saved_quizzes.begin(), saved_quizzes.end(),
            [&quiz_id](const Quiz& saved) {
                return saved.quiz_id == quiz_id;
            });

        if (found == saved_quizzes.end()) {
            return {ErrorCode::QUIZ_NOT_FOUND, "Quiz not found."};
        }

        *quiz = *found;
        return {ErrorCode::OK, "Quiz found."};
    }

    Result listQuizzes(std::vector<Quiz>* quizzes) const override {
        if (quizzes == nullptr) {
            return {ErrorCode::VALIDATION_FAILED,
                    "Quiz list pointer is null."};
        }

        *quizzes = saved_quizzes;
        return {ErrorCode::OK, "Quizzes listed."};
    }
};

class FakeAttemptRepository final : public AttemptRepository {
public:
    Result saveInProgressAttempt(const Attempt&) override {
        return notImplemented("FakeAttemptRepository");
    }

    Result findAttempt(const std::string&, Attempt*) const override {
        return notImplemented("FakeAttemptRepository");
    }

    Result findOpenAttempt(const std::string&, const std::string&,
                           Attempt*) const override {
        return notImplemented("FakeAttemptRepository");
    }

    Result saveGradedAttempt(const Attempt&, const GradeReport&) override {
        return notImplemented("FakeAttemptRepository");
    }

    Result findReport(const std::string&, const std::string&,
                      GradeReport*) const override {
        return notImplemented("FakeAttemptRepository");
    }

    Result listGradedAttempts(const std::string&,
                              std::vector<Attempt>*) const override {
        return notImplemented("FakeAttemptRepository");
    }

    Result listAttemptsForStudent(const std::string&,
                                  std::vector<Attempt>*) const override {
        return notImplemented("FakeAttemptRepository");
    }
};

class FakeGradingModule final : public GradingModule {
public:
    Result gradeAttempt(const std::string&, const std::string&,
                        const std::vector<Answer>&,
                        GradeReport*) override {
        return notImplemented("FakeGradingModule");
    }

    Result getReport(const std::string&, const std::string&,
                     GradeReport*) const override {
        return notImplemented("FakeGradingModule");
    }
};

class QuizEngineTests : public ::testing::Test {
protected:
    FakeQuestionBank question_bank;
    FakeQuizRepository quiz_repository;
    FakeAttemptRepository attempt_repository;
    FakeGradingModule grading_module;
    std::unique_ptr<QuizEngine> engine;

    const Session instructor{
        "instructor-1", Role::Instructor, "2026-10-26T10:00:00Z"};
    const Session admin{
        "admin-1", Role::Admin, "2026-10-26T10:00:00Z"};
    const Session student{
        "student-1", Role::Student, "2026-10-26T10:00:00Z"};

    void SetUp() override {
        question_bank.available_questions = {
            {"q1", "Math", Difficulty::Easy, QuestionType::MCQ,
             "What is 2 + 2?", {"3", "4"}, true},
            {"q2", "Math", Difficulty::Medium, QuestionType::MCQ,
             "What is 5 + 5?", {"10", "11"}, true},
            {"q3", "Science", Difficulty::Easy, QuestionType::MCQ,
             "Which planet do we live on?", {"Mars", "Earth"}, true},
            {"q4", "Math", Difficulty::Easy, QuestionType::MCQ,
             "What is 3 + 3?", {"6", "7"}, false}
        };

        engine = createQuizEngine(
            question_bank, quiz_repository, attempt_repository, grading_module);
    }

    QuizConfig validConfig() const {
        QuizConfig config;
        config.title = "  Algebra Quiz  ";
        config.time_limit_minutes = 30;
        config.question_count = 2;
        config.scoring = {2.0, 0.5};
        return config;
    }
};

TEST_F(QuizEngineTests, CreatesQuizAndPersistsItsDetails) {
    Quiz quiz;
    const auto result = engine->createQuiz(instructor, validConfig(), &quiz);

    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_FALSE(quiz.quiz_id.empty());
    EXPECT_EQ(quiz.title, "Algebra Quiz");
    EXPECT_EQ(quiz.created_by, instructor.user_id);
    EXPECT_FALSE(quiz.created_at.empty());
    EXPECT_EQ(quiz.time_limit_minutes, 30);
    EXPECT_DOUBLE_EQ(quiz.scoring.marks_per_question, 2.0);
    EXPECT_DOUBLE_EQ(quiz.scoring.negative_mark, 0.5);
    ASSERT_EQ(quiz.question_ids.size(), 2U);

    Quiz stored;
    ASSERT_TRUE(engine->getQuiz(quiz.quiz_id, &stored).ok());
    EXPECT_EQ(stored.quiz_id, quiz.quiz_id);
    EXPECT_EQ(stored.title, quiz.title);
    EXPECT_EQ(stored.question_ids, quiz.question_ids);
}

TEST_F(QuizEngineTests, AdminCanCreateQuiz) {
    Quiz quiz;
    const auto result = engine->createQuiz(admin, validConfig(), &quiz);

    EXPECT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(quiz.created_by, admin.user_id);
}

TEST_F(QuizEngineTests, StudentCannotCreateQuiz) {
    Quiz quiz;
    const auto result = engine->createQuiz(student, validConfig(), &quiz);

    EXPECT_EQ(result.code, ErrorCode::NOT_AUTHORIZED);
    EXPECT_TRUE(quiz_repository.saved_quizzes.empty());
}

TEST_F(QuizEngineTests, RejectsUnauthenticatedSession) {
    const Session invalid_session{"", Role::Instructor, ""};
    Quiz quiz;

    const auto result =
        engine->createQuiz(invalid_session, validConfig(), &quiz);

    EXPECT_EQ(result.code, ErrorCode::NOT_AUTHORIZED);
    EXPECT_TRUE(quiz_repository.saved_quizzes.empty());
}

TEST_F(QuizEngineTests, RejectsBlankTitle) {
    auto config = validConfig();
    config.title = "   ";
    Quiz quiz;

    const auto result = engine->createQuiz(instructor, config, &quiz);

    EXPECT_EQ(result.code, ErrorCode::VALIDATION_FAILED);
    EXPECT_TRUE(quiz_repository.saved_quizzes.empty());
}

TEST_F(QuizEngineTests, RejectsNonPositiveQuestionCount) {
    auto config = validConfig();
    config.question_count = 0;
    Quiz quiz;

    EXPECT_EQ(engine->createQuiz(instructor, config, &quiz).code,
              ErrorCode::VALIDATION_FAILED);

    config.question_count = -1;
    EXPECT_EQ(engine->createQuiz(instructor, config, &quiz).code,
              ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizEngineTests, RejectsNonPositiveTimeLimit) {
    auto config = validConfig();
    config.time_limit_minutes = 0;
    Quiz quiz;

    EXPECT_EQ(engine->createQuiz(instructor, config, &quiz).code,
              ErrorCode::VALIDATION_FAILED);

    config.time_limit_minutes = -5;
    EXPECT_EQ(engine->createQuiz(instructor, config, &quiz).code,
              ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizEngineTests, RejectsNullOutputPointer) {
    EXPECT_EQ(engine->createQuiz(instructor, validConfig(), nullptr).code,
              ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizEngineTests, RejectsInsufficientAvailableQuestions) {
    auto config = validConfig();
    config.question_count = 5;
    Quiz quiz;

    const auto result = engine->createQuiz(instructor, config, &quiz);

    EXPECT_EQ(result.code, ErrorCode::VALIDATION_FAILED);
    EXPECT_TRUE(quiz_repository.saved_quizzes.empty());
}

TEST_F(QuizEngineTests, AppliesSubjectAndDifficultyFilters) {
    auto config = validConfig();
    config.subject_filter = "Math";
    config.difficulty_filter = Difficulty::Easy;
    config.question_count = 1;

    Quiz quiz;
    const auto result = engine->createQuiz(instructor, config, &quiz);

    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_EQ(quiz.question_ids.size(), 1U);
    EXPECT_EQ(quiz.question_ids.front(), "q1");
}

TEST_F(QuizEngineTests, DoesNotIncludeUnavailableQuestions) {
    auto config = validConfig();
    config.subject_filter = "Math";
    config.difficulty_filter = Difficulty::Easy;
    config.question_count = 2;

    Quiz quiz;
    const auto result = engine->createQuiz(instructor, config, &quiz);

    EXPECT_EQ(result.code, ErrorCode::VALIDATION_FAILED);
    EXPECT_TRUE(quiz_repository.saved_quizzes.empty());
}

TEST_F(QuizEngineTests, ListsCreatedQuizzes) {
    Quiz first;
    Quiz second;
    ASSERT_TRUE(engine->createQuiz(instructor, validConfig(), &first).ok());

    auto config = validConfig();
    config.title = "Science Quiz";
    config.subject_filter = "Science";
    config.question_count = 1;
    ASSERT_TRUE(engine->createQuiz(instructor, config, &second).ok());

    std::vector<Quiz> quizzes;
    const auto result = engine->listQuizzes(&quizzes);

    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_EQ(quizzes.size(), 2U);
}

TEST_F(QuizEngineTests, RejectsNullQuizListPointer) {
    EXPECT_EQ(engine->listQuizzes(nullptr).code,
              ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizEngineTests, RejectsNullQuizPointerWhenRetrieving) {
    EXPECT_EQ(engine->getQuiz("some-id", nullptr).code,
              ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizEngineTests, RejectsEmptyQuizIdWhenRetrieving) {
    Quiz quiz;
    EXPECT_EQ(engine->getQuiz("   ", &quiz).code,
              ErrorCode::VALIDATION_FAILED);
}

}  // namespace
}  // namespace quizsphere
