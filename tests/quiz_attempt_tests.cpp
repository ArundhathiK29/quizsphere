#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <random>
#include <set>
#include <sstream>

#include "quiz_attempt.hpp"
#include "quiz_console.hpp"
#include "question_repository.hpp"

namespace quizsphere {
namespace {

class RecordingAttempts final : public AttemptRepository {
public:
    explicit RecordingAttempts(AttemptRepository& actual) : actual_(actual) {}
    int writes{0};
    bool fail_save{false};
    Result saveInProgressAttempt(const Attempt& a) override {
        ++writes;
        return fail_save ? Result{ErrorCode::IO_ERROR, "Injected storage failure"} : actual_.saveInProgressAttempt(a);
    }
    Result saveGradedAttempt(const Attempt& a, const GradeReport& g) override { ++writes; return actual_.saveGradedAttempt(a,g); }
    Result findAttempt(const std::string& id, Attempt* a) const override { return actual_.findAttempt(id,a); }
    Result findOpenAttempt(const std::string& q, const std::string& s, Attempt* a) const override { return actual_.findOpenAttempt(q,s,a); }
    Result findReport(const std::string& q, const std::string& s, GradeReport* g) const override { return actual_.findReport(q,s,g); }
    Result listGradedAttempts(const std::string& q, std::vector<Attempt>* a) const override { return actual_.listGradedAttempts(q,a); }
    Result listAttemptsForStudent(const std::string& s, std::vector<Attempt>* a) const override { return actual_.listAttemptsForStudent(s,a); }
private:
    AttemptRepository& actual_;
};

// Test double for the unmerged BL-10 implementation. Uses a fixed result, not a scoring algorithm.
class RecordingGrading final : public GradingModule {
public:
    explicit RecordingGrading(AttemptRepository& attempts) : attempts_(attempts) {}
    int calls{0};
    bool available{true};
    std::vector<Answer> received;
    Result gradeAttempt(const std::string& quiz, const std::string& student, const std::vector<Answer>& answers, GradeReport* report) override {
        ++calls;
        if (!available) return notImplemented("BL-10 grading");
        Attempt attempt;
        auto result = attempts_.findOpenAttempt(quiz,student,&attempt);
        if (!result.ok()) return result;
        received = answers;
        attempt.answers = answers;
        attempt.submitted = true;
        attempt.submitted_at = "2026-11-04T10:00:00Z";
        attempt.score = 7;
        GradeReport fixed;
        fixed.score = 7; fixed.max_score = 12;
        result = attempts_.saveGradedAttempt(attempt,fixed);
        if (result.ok()) *report = fixed;
        return result;
    }
    Result getReport(const std::string& q, const std::string& s, GradeReport* out) const override { return attempts_.findReport(q,s,out); }
private:
    AttemptRepository& attempts_;
};

class QuizAttemptTest : public ::testing::Test {
protected:
    std::string path;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<RecordingAttempts> attempts;
    std::unique_ptr<RecordingGrading> grading;
    std::unique_ptr<QuizEngine> engine;
    const Session student{"student",Role::Student,"2026-11-04T09:00:00Z"};
    const Session instructor{"instructor",Role::Instructor,"2026-11-04T09:00:00Z"};

    void open() {
        std::string error;
        dal = createSqliteDataAccessLayer(path,&error);
        ASSERT_NE(dal,nullptr) << error;
        bank = createQuestionBank(createSqliteQuestionRepository(path,&error));
        ASSERT_TRUE(bank->load().ok());
        attempts = std::make_unique<RecordingAttempts>(*dal->attempts);
        grading = std::make_unique<RecordingGrading>(*attempts);
        engine = createQuizEngine(*bank,*dal->quizzes,*attempts,*grading,42);
    }
    void close() { engine.reset(); grading.reset(); attempts.reset(); bank.reset(); dal.reset(); }
    void SetUp() override {
        path = (std::filesystem::temp_directory_path()/ ("quizsphere-attempt-"+std::to_string(std::random_device{}())+".sqlite")).string();
        open();
        ASSERT_NE(engine,nullptr);
        Quiz quiz;
        quiz.quiz_id="quiz"; quiz.title="Quiz"; quiz.created_by="instructor";
        quiz.created_at="2026-11-04T09:00:00Z"; quiz.time_limit_minutes=10; quiz.scoring={4,1};
        for (int i=0;i<4;++i) {
            Question q{"q"+std::to_string(i),"Math",Difficulty::Easy,QuestionType::MCQ,
                       "Choose the second option",{"first","second","third","fourth"},1,true};
            ASSERT_TRUE(bank->addQuestion(instructor,q).ok());
            quiz.question_ids.push_back(q.question_id);
        }
        ASSERT_TRUE(dal->quizzes->saveQuiz(quiz).ok());
    }
    void TearDown() override {
        close();
        std::error_code ignored;
        for (const std::string suffix : {"","-journal","-wal","-shm"}) std::filesystem::remove(path+suffix,ignored);
    }
    bool sql(const char* statement) {
        sqlite3* db=nullptr;
        if (sqlite3_open(path.c_str(),&db)!=SQLITE_OK) { sqlite3_close(db); return false; }
        const int rc=sqlite3_exec(db,statement,nullptr,nullptr,nullptr);
        sqlite3_close(db); return rc==SQLITE_OK;
    }
};

TEST_F(QuizAttemptTest, UT_QE_01_ShuffledQuestionsKeepEveryIdentityExactlyOnce) {
    std::set<std::vector<std::string>> orders;
    for (int i=0;i<8;++i) {
        Attempt attempt;
        ASSERT_TRUE(engine->startAttempt(student,"quiz",QuizMode::Practice,&attempt).ok());
        EXPECT_EQ((std::set<std::string>(attempt.question_order.begin(),attempt.question_order.end())),
                  (std::set<std::string>{"q0","q1","q2","q3"}));
        EXPECT_EQ(attempt.question_order.size(),4U);
        orders.insert(attempt.question_order);
    }
    EXPECT_GT(orders.size(),1U);  // Fixed seed; no dependence on wall-clock randomness.
    EXPECT_EQ(attempts->writes,0);
}

TEST_F(QuizAttemptTest, ST_QE_01_DisplayedAnswersMapToOriginalOptions) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    for (std::size_t i=0;i<4;++i) {
        ASSERT_TRUE(session.jump(i).ok());
        AttemptQuestionView view;
        ASSERT_TRUE(session.question(&view).ok());
        EXPECT_EQ((std::set<std::size_t>(view.original_option_indices.begin(),view.original_option_indices.end())),
                  (std::set<std::size_t>{0,1,2,3}));
        PublicQuestion original;
        ASSERT_TRUE(bank->getQuestionForQuiz(view.question.question_id,&original).ok());
        for (int display=0;display<4;++display) {
            EXPECT_EQ(view.question.options[static_cast<std::size_t>(display)],
                      original.options[view.original_option_indices[static_cast<std::size_t>(display)]]);
            ASSERT_TRUE(session.select(display).ok());
            EXPECT_EQ(session.attempt().answers.back().selected_option,
                      static_cast<int>(view.original_option_indices[static_cast<std::size_t>(display)]));
            AttemptQuestionView selected;
            ASSERT_TRUE(session.question(&selected).ok());
            EXPECT_EQ(selected.selected_display_option,display);
        }
    }
}

TEST_F(QuizAttemptTest, SelectingChangingClearingAndFlaggingPreserveOneAnswer) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    ASSERT_TRUE(session.select(1).ok());
    ASSERT_TRUE(session.flag(true).ok());
    ASSERT_TRUE(session.select(2).ok());
    ASSERT_EQ(session.attempt().answers.size(),1U);
    EXPECT_TRUE(session.attempt().answers[0].flagged_for_review);
    ASSERT_TRUE(session.select(kBlankAnswer).ok());
    EXPECT_EQ(session.attempt().answers[0].selected_option,kBlankAnswer);
    EXPECT_TRUE(session.attempt().answers[0].flagged_for_review);
    ASSERT_TRUE(session.flag(false).ok());
    EXPECT_FALSE(session.attempt().answers[0].flagged_for_review);
}

TEST_F(QuizAttemptTest, ST_QE_03_NavigationAndReviewKeepOptionsAndAnswersStable) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Practice).ok());
    AttemptQuestionView first;
    ASSERT_TRUE(session.question(&first).ok());
    EXPECT_TRUE(first.visited);
    ASSERT_TRUE(session.select(2).ok());
    ASSERT_TRUE(session.flag(true).ok());
    ASSERT_TRUE(session.next().ok());
    ASSERT_TRUE(session.flag(true).ok());
    ASSERT_TRUE(session.previous().ok());
    AttemptQuestionView back;
    ASSERT_TRUE(session.question(&back).ok());
    EXPECT_EQ(back.question.options,first.question.options);
    EXPECT_EQ(back.selected_display_option,2);
    EXPECT_TRUE(back.flagged);
    const auto review=session.review();
    EXPECT_EQ(review.total,4U); EXPECT_EQ(review.answered,1U); EXPECT_EQ(review.unanswered,3U);
    EXPECT_EQ(review.flagged_positions,(std::vector<std::size_t>{0,1}));
    EXPECT_EQ(session.previous().code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(session.jump(4).code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(session.jump(std::numeric_limits<std::size_t>::max()).code,ErrorCode::VALIDATION_FAILED);
    ASSERT_TRUE(session.jump(3).ok());
    EXPECT_EQ(session.next().code,ErrorCode::VALIDATION_FAILED);
    EXPECT_FALSE(session.attempt().submitted);
}

TEST_F(QuizAttemptTest, ST_QE_04_PracticeFeedbackAndCompletionNeverPersist) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Practice).ok());
    AttemptQuestionView view;
    ASSERT_TRUE(session.question(&view).ok());
    const auto correct=std::find(view.original_option_indices.begin(),view.original_option_indices.end(),1U);
    ASSERT_NE(correct,view.original_option_indices.end());
    ASSERT_TRUE(session.select(static_cast<int>(correct-view.original_option_indices.begin())).ok());
    PracticeFeedback feedback;
    ASSERT_TRUE(session.feedback(&feedback).ok());
    EXPECT_TRUE(feedback.correct); EXPECT_FALSE(feedback.blank); EXPECT_EQ(feedback.correct_answer,"second");
    ASSERT_TRUE(session.select(kBlankAnswer).ok());
    ASSERT_TRUE(session.feedback(&feedback).ok()); EXPECT_TRUE(feedback.blank);
    ASSERT_TRUE(session.select(static_cast<int>((feedback.correct_display_option+1)%4)).ok());
    ASSERT_TRUE(session.feedback(&feedback).ok()); EXPECT_FALSE(feedback.correct);
    GradeReport report;
    ASSERT_TRUE(session.submit(true,&report).ok());
    EXPECT_TRUE(session.attempt().submitted);
    EXPECT_EQ(attempts->writes,0); EXPECT_EQ(grading->calls,0);
    std::vector<Attempt> stored;
    ASSERT_TRUE(dal->attempts->listAttemptsForStudent(student.user_id,&stored).ok()); EXPECT_TRUE(stored.empty());
    ASSERT_TRUE(dal->attempts->listGradedAttempts("quiz",&stored).ok()); EXPECT_TRUE(stored.empty());
    EXPECT_EQ(dal->attempts->findReport("quiz",student.user_id,&report).code,ErrorCode::ATTEMPT_NOT_FOUND);
}

TEST_F(QuizAttemptTest, ST_QE_05_GradedResumeKeepsQuestionOrderOptionsAnswersAndFlags) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    ASSERT_TRUE(session.select(3).ok()); ASSERT_TRUE(session.flag(true).ok());
    const auto original=session.attempt();
    AttemptQuestionView first;
    ASSERT_TRUE(session.question(&first).ok());
    close(); open();
    QuizAttemptSession resumed(student,*engine,*bank);
    ASSERT_TRUE(resumed.start("quiz",QuizMode::Graded).ok());
    EXPECT_EQ(resumed.attempt().attempt_id,original.attempt_id);
    EXPECT_EQ(resumed.attempt().question_order,original.question_order);
    AttemptQuestionView restored;
    ASSERT_TRUE(resumed.question(&restored).ok());
    EXPECT_EQ(restored.question.options,first.question.options); EXPECT_EQ(restored.selected_display_option,3); EXPECT_TRUE(restored.flagged);
    ASSERT_TRUE(resumed.select(kBlankAnswer).ok());
    close(); open();
    QuizAttemptSession cleared(student,*engine,*bank);
    ASSERT_TRUE(cleared.start("quiz",QuizMode::Graded).ok());
    ASSERT_TRUE(cleared.question(&restored).ok()); EXPECT_EQ(restored.selected_display_option,kBlankAnswer); EXPECT_TRUE(restored.flagged);
}

TEST_F(QuizAttemptTest, SubmissionRequiresConfirmationAndUnavailableGradingKeepsAttemptOpen) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    GradeReport report;
    EXPECT_EQ(session.submit(false,&report).code,ErrorCode::CONFIRMATION_REQUIRED);
    EXPECT_EQ(grading->calls,0);
    grading->available=false;
    EXPECT_EQ(session.submit(true,&report).code,ErrorCode::NOT_IMPLEMENTED);
    EXPECT_FALSE(session.attempt().submitted);
    Attempt stored;
    ASSERT_TRUE(dal->attempts->findAttempt(session.attempt().attempt_id,&stored).ok()); EXPECT_FALSE(stored.submitted);
    ASSERT_TRUE(session.select(0).ok());
}

TEST_F(QuizAttemptTest, GradingHandoffUsesOriginalIndicesAndStoredResultSurvivesRestartWithTestDouble) {
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    AttemptQuestionView view;
    ASSERT_TRUE(session.question(&view).ok());
    ASSERT_TRUE(session.select(2).ok());
    GradeReport report;
    ASSERT_TRUE(session.submit(true,&report).ok());
    ASSERT_EQ(grading->received.size(),1U);
    EXPECT_EQ(grading->received[0].selected_option,static_cast<int>(view.original_option_indices[2]));
    EXPECT_EQ(session.select(0).code,ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    EXPECT_EQ(session.submit(true,&report).code,ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    close(); open();
    ASSERT_TRUE(grading->getReport("quiz",student.user_id,&report).ok()); EXPECT_DOUBLE_EQ(report.score,7);
    Attempt repeated;
    EXPECT_EQ(engine->startAttempt(student,"quiz",QuizMode::Graded,&repeated).code,ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
}

TEST_F(QuizAttemptTest, GradedFeedbackIsDeniedAndModesDoNotShareAnswers) {
    QuizAttemptSession graded(student,*engine,*bank), practice(student,*engine,*bank);
    ASSERT_TRUE(graded.start("quiz",QuizMode::Graded).ok()); ASSERT_TRUE(graded.select(0).ok());
    PracticeFeedback feedback;
    EXPECT_EQ(graded.feedback(&feedback).code,ErrorCode::NOT_AUTHORIZED);
    ASSERT_TRUE(practice.start("quiz",QuizMode::Practice).ok()); EXPECT_TRUE(practice.attempt().answers.empty());
    ASSERT_TRUE(practice.flag(true).ok());
    EXPECT_FALSE(graded.attempt().answers[0].flagged_for_review);
}

TEST_F(QuizAttemptTest, StorageFailuresDoNotChangeInMemoryAnswersOrCreatePartialAttempts) {
    attempts->fail_save=true;
    Attempt failed;
    EXPECT_EQ(engine->startAttempt(student,"quiz",QuizMode::Graded,&failed).code,ErrorCode::IO_ERROR);
    EXPECT_TRUE(failed.attempt_id.empty());
    attempts->fail_save=false;
    QuizAttemptSession session(student,*engine,*bank);
    ASSERT_TRUE(session.start("quiz",QuizMode::Graded).ok());
    ASSERT_TRUE(session.select(0).ok()); const auto before=session.attempt().answers[0];
    attempts->fail_save=true;
    EXPECT_EQ(session.select(1).code,ErrorCode::IO_ERROR);
    EXPECT_EQ(session.attempt().answers[0].selected_option,before.selected_option);
}

TEST_F(QuizAttemptTest, InvalidRolesModesOptionsAndTamperedAttemptsAreRejected) {
    Attempt attempt;
    EXPECT_EQ(engine->startAttempt(instructor,"quiz",QuizMode::Graded,&attempt).code,ErrorCode::NOT_AUTHORIZED);
    EXPECT_EQ(engine->startAttempt(student,"quiz",static_cast<QuizMode>(99),&attempt).code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(engine->startAttempt(student,"missing",QuizMode::Graded,&attempt).code,ErrorCode::QUIZ_NOT_FOUND);
    EXPECT_EQ(engine->startAttempt(student,"quiz",QuizMode::Graded,nullptr).code,ErrorCode::VALIDATION_FAILED);
    ASSERT_TRUE(engine->startAttempt(student,"quiz",QuizMode::Graded,&attempt).ok());
    EXPECT_EQ(engine->saveAnswer(&attempt,{"outside",0,false}).code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(engine->saveAnswer(&attempt,{attempt.question_order[0],99,false}).code,ErrorCode::VALIDATION_FAILED);
    attempt.mode=QuizMode::Practice;
    EXPECT_EQ(engine->saveAnswer(&attempt,{attempt.question_order[0],0,false}).code,ErrorCode::VALIDATION_FAILED);
}

TEST_F(QuizAttemptTest, MissingOrMalformedQuizQuestionsFailBeforeAttemptPersistence) {
    ASSERT_TRUE(sql("PRAGMA foreign_keys=OFF; DROP TRIGGER protect_question_delete_v2; DROP TRIGGER IF EXISTS protect_question_delete; DELETE FROM questions WHERE id='q0';"));
    ASSERT_TRUE(bank->load().ok());
    Attempt attempt;
    EXPECT_EQ(engine->startAttempt(student,"quiz",QuizMode::Graded,&attempt).code,ErrorCode::QUESTION_NOT_FOUND);
    EXPECT_EQ(attempts->writes,0);
}

TEST_F(QuizAttemptTest, InstructorConsoleCreatesQuizThroughExistingValidation) {
    std::istringstream input("Console quiz\n15\n2\nMath\nEasy\n4\n1\n");
    std::ostringstream output;
    runQuizCreation(instructor,*engine,input,output);
    EXPECT_NE(output.str().find("Quiz created:"),std::string::npos);
    std::vector<Quiz> quizzes;
    ASSERT_TRUE(dal->quizzes->listQuizzes(&quizzes).ok()); ASSERT_EQ(quizzes.size(),2U);
    const auto created=std::find_if(quizzes.begin(),quizzes.end(),[](const Quiz& q) { return q.title=="Console quiz"; });
    ASSERT_NE(created,quizzes.end()); EXPECT_EQ(created->question_ids.size(),2U); EXPECT_DOUBLE_EQ(created->scoring.negative_mark,1);
}

TEST_F(QuizAttemptTest, InstructorConsoleRejectsMalformedAndNonpositiveConfiguration) {
    for (const std::string script : {"Bad\n10oops\n", "Bad\n0\n2\n\n\n4\n1\n",
                                      "Bad\n10\n2\n\nUnknown\n"}) {
        std::istringstream input(script); std::ostringstream output;
        runQuizCreation(instructor,*engine,input,output);
        EXPECT_EQ(output.str().find("Quiz created:"),std::string::npos);
    }
    std::vector<Quiz> quizzes;
    ASSERT_TRUE(dal->quizzes->listQuizzes(&quizzes).ok()); EXPECT_EQ(quizzes.size(),1U);
}

TEST_F(QuizAttemptTest, StudentConsolePracticesReviewsCancelsAndCompletesWithoutStorage) {
    std::istringstream input("quiz\np\nf\nr\nj 2\nj 1\na 1\nc\ns\nno\nr\ns\nyes\n");
    std::ostringstream output;
    runQuizAttempts(student,*engine,*bank,input,output);
    const auto text=output.str();
    EXPECT_NE(text.find("Flagged questions: 1"),std::string::npos);
    EXPECT_NE(text.find("Correct answer:"),std::string::npos);
    EXPECT_NE(text.find("Submission cancelled."),std::string::npos);
    EXPECT_NE(text.find("Practice completed. No result stored."),std::string::npos);
    EXPECT_EQ(attempts->writes,0); EXPECT_EQ(grading->calls,0);
}

TEST_F(QuizAttemptTest, StudentConsoleKeepsGradedAnswersWhenGradingIsUnavailable) {
    grading->available=false;
    std::istringstream input("quiz\ng\na 2\nf\ns\nyes\n0\n"); std::ostringstream output;
    runQuizAttempts(student,*engine,*bank,input,output);
    EXPECT_NE(output.str().find("attempt stays open"),std::string::npos);
    Attempt stored;
    ASSERT_TRUE(dal->attempts->findOpenAttempt("quiz",student.user_id,&stored).ok());
    ASSERT_EQ(stored.answers.size(),1U); EXPECT_TRUE(stored.answers[0].flagged_for_review);
    EXPECT_FALSE(stored.submitted);
}

TEST_F(QuizAttemptTest, StudentConsoleSubmitsAndRetrievesOnlyItsOwnResultWithTestDouble) {
    std::istringstream input("quiz\ng\na 2\ns\nyes\n"); std::ostringstream output;
    runQuizAttempts(student,*engine,*bank,input,output);
    EXPECT_NE(output.str().find("Submitted. Score: 7/12"),std::string::npos);
    std::istringstream result_input("quiz\n");
    runQuizResult(student,*engine,*grading,result_input,output);
    EXPECT_NE(output.str().find("Score: 7/12"),std::string::npos);
    const Session other{"other",Role::Student,"2026-11-04T09:00:00Z"};
    std::istringstream other_input("quiz\n"); std::ostringstream other_output;
    runQuizResult(other,*engine,*grading,other_input,other_output);
    EXPECT_EQ(other_output.str().find("Score:"),std::string::npos);
}

TEST_F(QuizAttemptTest, MenuCancellationInvalidCommandsAndEofNeverSubmit) {
    std::istringstream input("quiz\ng\nj -1\na 99999999999999999999999999999999\nj 0\na 2oops\nf\ns\nno\n");
    std::ostringstream output;
    runQuizAttempts(student,*engine,*bank,input,output);
    EXPECT_EQ(grading->calls,0);
    Attempt stored;
    ASSERT_TRUE(dal->attempts->findOpenAttempt("quiz",student.user_id,&stored).ok());
    EXPECT_FALSE(stored.submitted);
    ASSERT_EQ(stored.answers.size(),1U); EXPECT_EQ(stored.answers[0].selected_option,kBlankAnswer);
}

TEST_F(QuizAttemptTest, RoleSpecificConsoleFunctionsRejectOtherRoles) {
    std::istringstream input; std::ostringstream output;
    runQuizCreation(student,*engine,input,output);
    runQuizAttempts(instructor,*engine,*bank,input,output);
    runQuizResult(instructor,*engine,*grading,input,output);
    EXPECT_NE(output.str().find("Not authorized"),std::string::npos);
    EXPECT_EQ(attempts->writes,0);
}

TEST_F(QuizAttemptTest, StaleAttemptCopiesCannotModifySubmittedDatabaseRecords) {
    Attempt attempt;
    ASSERT_TRUE(engine->startAttempt(student,"quiz",QuizMode::Graded,&attempt).ok());
    const auto stale=attempt;
    GradeReport report;
    ASSERT_TRUE(engine->submitAttempt(student,&attempt,false,&report).ok());
    auto changed=stale;
    EXPECT_EQ(engine->saveAnswer(&changed,{changed.question_order[0],0,true}).code,ErrorCode::ATTEMPT_ALREADY_SUBMITTED);
    const Session other{"other",Role::Student,student.logged_in_at};
    EXPECT_EQ(engine->submitAttempt(other,&changed,false,&report).code,ErrorCode::NOT_AUTHORIZED);
}

TEST_F(QuizAttemptTest, CorruptDuplicateQuizQuestionRowsAreRejectedBeforeSave) {
    ASSERT_TRUE(sql("INSERT INTO quiz_q VALUES('quiz',4,'q0')"));
    Attempt attempt;
    EXPECT_EQ(engine->startAttempt(student,"quiz",QuizMode::Graded,&attempt).code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(attempts->writes,0);
}

TEST_F(QuizAttemptTest, TamperedOrDuplicateAnswerVectorsCannotReachGrading) {
    Attempt attempt;
    ASSERT_TRUE(engine->startAttempt(student,"quiz",QuizMode::Graded,&attempt).ok());
    attempt.answers={{"outside",0,false},{"outside",0,false}};
    GradeReport report;
    EXPECT_EQ(engine->submitAttempt(student,&attempt,false,&report).code,ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(grading->calls,0);
}

}  // namespace
}  // namespace quizsphere
