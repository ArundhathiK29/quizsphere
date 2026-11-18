#include <gtest/gtest.h>

#include "question_bank.hpp"
#include "question_repository.hpp"

namespace quizsphere {
namespace {

// UT-AUTH-07's Test Plan scenario is an Instructor-only call, independent of menus.
TEST(AuthRoleBoundary, UT_AUTH_07_StudentCannotCallInstructorQuestionBankFunctions) {
    std::string error;
    auto repository = createSqliteQuestionRepository(":memory:", &error);
    ASSERT_NE(repository, nullptr) << error;
    auto bank = createQuestionBank(std::move(repository));
    ASSERT_TRUE(bank->load().ok());

    const Session student{"student-1", Role::Student, "2026-11-10T00:00:00Z"};
    const Session instructor{"instructor-1", Role::Instructor, "2026-11-10T00:00:00Z"};
    const Question question{"AUTH-Q1", "Math", Difficulty::Easy, QuestionType::MCQ,
                            "What is 2 + 2?", {"3", "4"}, 1, true};

    EXPECT_EQ(bank->addQuestion(student, question).code, ErrorCode::NOT_AUTHORIZED);
    std::vector<Question> stored;
    ASSERT_TRUE(bank->listQuestions(instructor, &stored, false, std::nullopt, std::nullopt).ok());
    EXPECT_TRUE(stored.empty());

    ASSERT_TRUE(bank->addQuestion(instructor, question).ok());
    auto changed = question;
    changed.question_text = "Unauthorised edit";
    EXPECT_EQ(bank->updateQuestion(student, changed).code, ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank->deleteQuestion(student, question.question_id, true).code, ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(bank->setAvailability(student, question.question_id, false).code, ErrorCode::NOT_AUTHORIZED);

    Question unchanged;
    ASSERT_TRUE(bank->findQuestionById(instructor, question.question_id, &unchanged).ok());
    EXPECT_EQ(unchanged.question_text, question.question_text);
    EXPECT_TRUE(unchanged.is_available);
}

}  // namespace
}  // namespace quizsphere
