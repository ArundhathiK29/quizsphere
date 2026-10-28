#include "result_view.hpp"

#include <iomanip>
#include <sstream>

namespace quizsphere {
namespace {

std::string formatOption(int selected_option) {
    if (selected_option == kBlankAnswer) {
        return "blank";
    }
    return std::to_string(selected_option);
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

}  // namespace quizsphere