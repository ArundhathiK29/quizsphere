#include "quiz_console.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace quizsphere {
namespace {

bool read(std::istream& input, std::ostream& output, const std::string& prompt, std::string* value) {
    output << prompt;
    if (!std::getline(input,*value)) return false;
    const auto not_space=[](unsigned char c) { return std::isspace(c)==0; };
    value->erase(value->begin(),std::find_if(value->begin(),value->end(),not_space));
    value->erase(std::find_if(value->rbegin(),value->rend(),not_space).base(),value->end());
    return true;
}

template<typename T>
bool number(const std::string& text, T* out) {
    if (text.empty()) return false;
    std::istringstream parser(text);
    parser >> std::noskipws >> *out;
    return !parser.fail() && parser.eof();
}

void outcome(const Result& result, std::ostream& output) {
    if (!result.ok()) output << "Error: " << result.message << '\n';
}

bool chooseQuiz(const QuizEngine& engine, std::istream& input, std::ostream& output, std::string* id) {
    std::vector<Quiz> quizzes;
    const auto result=engine.listQuizzes(&quizzes);
    if (!result.ok()) { outcome(result,output); return false; }
    if (quizzes.empty()) { output << "No quizzes available.\n"; return false; }
    for (const auto& quiz : quizzes)
        output << quiz.quiz_id << " | " << quiz.title << " | " << quiz.question_ids.size() << " questions\n";
    return read(input,output,"Quiz ID (blank to go back): ",id) && !id->empty();
}

void showReview(const QuizAttemptSession& session, std::ostream& output) {
    const auto review=session.review();
    output << "Review: total " << review.total << ", answered " << review.answered
           << ", unanswered " << review.unanswered << ", flagged " << review.flagged_positions.size() << '\n';
    output << "Flagged questions:";
    for (const auto position : review.flagged_positions) output << ' ' << position+1;
    if (review.flagged_positions.empty()) output << " none";
    output << "\nUnanswered questions:";
    for (std::size_t i=0; i<session.attempt().question_order.size(); ++i) {
        const auto& id=session.attempt().question_order[i];
        const auto found=std::find_if(session.attempt().answers.begin(),session.attempt().answers.end(),[&](const Answer& answer) {
            return answer.question_id==id;
        });
        if (found==session.attempt().answers.end() || found->selected_option==kBlankAnswer) output << ' ' << i+1;
    }
    output << "\nUse j N to return to a flagged or unanswered question.\n";
}

void practiceFeedback(const QuizAttemptSession& session, std::ostream& output) {
    if (session.attempt().mode!=QuizMode::Practice) return;
    PracticeFeedback feedback;
    const auto result=session.feedback(&feedback);
    if (!result.ok()) { outcome(result,output); return; }
    output << (feedback.blank ? "Unanswered. " : (feedback.correct ? "Correct. " : "Incorrect. "))
           << "Correct answer: " << feedback.correct_display_option+1 << ". " << feedback.correct_answer << '\n';
}

}  // namespace

void runQuizCreation(const Session& session, QuizEngine& engine, std::istream& input, std::ostream& output) {
    if (session.role!=Role::Instructor && session.role!=Role::Admin) {
        output << "Not authorized: quiz creation requires an Instructor or Admin.\n"; return;
    }
    QuizConfig config;
    std::string text;
    if (!read(input,output,"Quiz title (blank to cancel): ",&config.title) || config.title.empty()) return;
    if (!read(input,output,"Time limit in minutes: ",&text) || !number(text,&config.time_limit_minutes)) {
        output << "Invalid time limit.\n"; return;
    }
    if (!read(input,output,"Question count: ",&text) || !number(text,&config.question_count)) {
        output << "Invalid question count.\n"; return;
    }
    if (!read(input,output,"Subject (blank for all): ",&text)) return;
    if (!text.empty()) config.subject_filter=text;
    if (!read(input,output,"Difficulty (Easy/Medium/Hard, blank for all): ",&text)) return;
    if (!text.empty()) {
        config.difficulty_filter=parseDifficulty(text);
        if (!config.difficulty_filter) { output << "Invalid difficulty.\n"; return; }
    }
    if (!read(input,output,"Marks per question: ",&text) || !number(text,&config.scoring.marks_per_question)) {
        output << "Invalid marks.\n"; return;
    }
    if (!read(input,output,"Wrong-answer penalty (0 disables): ",&text) || !number(text,&config.scoring.negative_mark)) {
        output << "Invalid penalty.\n"; return;
    }
    Quiz quiz;
    const auto result=engine.createQuiz(session,config,&quiz);
    if (!result.ok()) outcome(result,output);
    else output << "Quiz created: " << quiz.quiz_id << '\n';
}

void runQuizAttempts(const Session& student, QuizEngine& engine, QuestionBank& bank,
                     std::istream& input, std::ostream& output) {
    if (student.role!=Role::Student) { output << "Not authorized: attempts require a Student.\n"; return; }
    std::string id, mode;
    if (!chooseQuiz(engine,input,output,&id)) return;
    if (!read(input,output,"Mode: p = practice, g = graded: ",&mode)) return;
    if (mode!="p" && mode!="g") { output << "Invalid mode. Choose p or g.\n"; return; }
    QuizAttemptSession session(student,engine,bank);
    auto result=session.start(id,mode=="p" ? QuizMode::Practice : QuizMode::Graded);
    if (!result.ok()) { outcome(result,output); return; }
    output << (mode=="p" ? "Practice: feedback is immediate; nothing is stored.\n" :
        "Graded: answers and flags are saved. The time limit is not enforced yet.\n");
    for (;;) {
        AttemptQuestionView view;
        result=session.question(&view);
        if (!result.ok()) { outcome(result,output); return; }
        output << "\nQuestion " << session.position()+1 << '/' << session.attempt().question_order.size()
               << " [" << (view.selected_display_option==kBlankAnswer ? "unanswered" : "answered")
               << (view.flagged ? ", flagged" : "") << "]: " << view.question.question_text << '\n';
        for (std::size_t i=0;i<view.question.options.size();++i)
            output << i+1 << ". " << view.question.options[i] << (view.selected_display_option==static_cast<int>(i) ? " [selected]" : "") << '\n';
        std::string line;
        if (!read(input,output,"n next | p previous | j N jump | a N answer | c clear | f flag/unflag | r review | s submit | 0 back: ",&line) || line=="0") return;
        if (line=="n") result=session.next();
        else if (line=="p") result=session.previous();
        else if (line=="f") result=session.flag(!view.flagged);
        else if (line=="c") { result=session.select(kBlankAnswer); if (result.ok()) practiceFeedback(session,output); }
        else if (line=="r") { showReview(session,output); continue; }
        else if (line=="s") {
            showReview(session,output);
            std::string answer;
            if (!read(input,output,"Submit this attempt? [yes/no]: ",&answer)) return;
            if (answer!="yes") { output << "Submission cancelled.\n"; continue; }
            GradeReport report;
            result=session.submit(true,&report);
            if (result.ok()) {
                if (mode=="p") output << "Practice completed. No result stored.\n";
                else output << "Submitted. Score: " << report.score << '/' << report.max_score << '\n';
                return;
            }
            if (result.code==ErrorCode::NOT_IMPLEMENTED) {
                output << "Grading is not available yet. Your graded answers remain saved and the attempt stays open.\n";
                continue;
            }
        } else if (line.size()>2 && (line[0]=='j' || line[0]=='a') && line[1]==' ') {
            std::size_t value=0;
            if (!number(line.substr(2),&value) || value==0 || value>static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                output << "Invalid question/option number.\n"; continue;
            }
            if (line[0]=='j') result=session.jump(value-1);
            else { result=session.select(static_cast<int>(value-1)); if (result.ok()) practiceFeedback(session,output); }
        } else { output << "Invalid command.\n"; continue; }
        outcome(result,output);
    }
}

void runQuizResult(const Session& student, const QuizEngine& engine, const GradingModule& grading,
                   std::istream& input, std::ostream& output) {
    if (student.role!=Role::Student) { output << "Not authorized: view your own results as a Student.\n"; return; }
    std::string id;
    if (!chooseQuiz(engine,input,output,&id)) return;
    GradeReport report;
    const auto result=grading.getReport(id,student.user_id,&report);
    if (result.code==ErrorCode::NOT_IMPLEMENTED)
        output << "Results are not available yet. Your saved attempt can be resumed from Attempt a quiz.\n";
    else if (!result.ok()) outcome(result,output);
    else output << "Score: " << report.score << '/' << report.max_score << '\n';
}

}  // namespace quizsphere
