#include "result_view.hpp"

#include <iomanip>
#include <sstream>

namespace quizsphere {
namespace {

// Internal session used only to read full Question records (including option text) from the
// Question Bank, which requires an Instructor/Admin session (QuestionBank::findQuestionById).
// Same rationale as grading_module.cpp/analytics_module.cpp: this is a trusted system read, not
// acting on behalf of any particular user.
const Session& systemSession() {
    static const Session session{"system:result_view", Role::Admin, "1970-01-01T00:00:00Z"};
    return session;
}

std::string formatOption(int selected_option) {
    if (selected_option == kBlankAnswer) {
        return "blank";
    }
    return std::to_string(selected_option);
}

// Resolves an option index to its text via the question's ORIGINAL (unshuffled) option order -
// the same order ItemResult::selected_option/correct_option are always expressed in. Returns the
// bare index as a fallback if the index is out of range for some reason.
std::string formatOption(int option_index, const Question& question) {
    if (option_index == kBlankAnswer) {
        return "blank";
    }
    if (option_index < 0 || static_cast<std::size_t>(option_index) >= question.options.size()) {
        return std::to_string(option_index);
    }
    return question.options[static_cast<std::size_t>(option_index)];
}

const char* statusLabel(const ItemResult& item) {
    if (item.blank) {
        return "BLANK";
    }
    return item.correct ? "CORRECT" : "WRONG";
}

}  // namespace

std::string formatItemisedResult(const GradeReport& report) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);

    out << "Itemised Result\n";
    out << "----------------\n";
    for (const ItemResult& item : report.itemised_breakdown) {
        out << item.question_id << ": your answer = " << formatOption(item.selected_option)
            << ", correct answer = " << item.correct_option << " -> " << statusLabel(item) << " ("
            << (item.marks_awarded >= 0.0 ? "+" : "") << item.marks_awarded << ")\n";
    }
    out << "\nScore: " << report.score << " / " << report.max_score;
    if (report.auto_submitted) {
        out << "  (auto-submitted: time expired)";
    }
    out << '\n';

    return out.str();
}

std::string formatItemisedResult(const GradeReport& report, QuestionBank& question_bank) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);

    out << "Itemised Result\n";
    out << "----------------\n";
    for (const ItemResult& item : report.itemised_breakdown) {
        Question question;
        const Result question_result = question_bank.findQuestionById(systemSession(), item.question_id, &question);
        if (!question_result.ok()) {
            // Fall back to the index-only line rather than failing the whole result screen over
            // one question that can no longer be found (e.g. deleted since the attempt).
            out << item.question_id << ": your answer = " << formatOption(item.selected_option)
                << ", correct answer = " << item.correct_option << " -> " << statusLabel(item) << " ("
                << (item.marks_awarded >= 0.0 ? "+" : "") << item.marks_awarded << ")\n";
            continue;
        }
        out << item.question_id << ": your answer = " << formatOption(item.selected_option, question)
            << ", correct answer = " << formatOption(item.correct_option, question) << " -> " << statusLabel(item)
            << " (" << (item.marks_awarded >= 0.0 ? "+" : "") << item.marks_awarded << ")\n";
    }
    out << "\nScore: " << report.score << " / " << report.max_score;
    if (report.auto_submitted) {
        out << "  (auto-submitted: time expired)";
    }
    out << '\n';

    return out.str();
}

}  // namespace quizsphere