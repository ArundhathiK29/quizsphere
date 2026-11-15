#include "analytics_console.hpp"

#include <iomanip>
#include <string>
#include <vector>

namespace quizsphere {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool prompt(std::istream& input, std::ostream& output, const std::string& label, std::string* value) {
    output << label << std::flush;
    if (!std::getline(input, *value)) {
        return false;
    }
    *value = trim(*value);
    return true;
}

void printEntries(const std::string& title, const std::vector<LeaderboardEntry>& entries, std::ostream& output) {
    output << "\n--- " << title << " ---\n";
    if (entries.empty()) {
        output << "No graded attempts yet.\n";
        return;
    }
    output << std::fixed << std::setprecision(2);
    for (const LeaderboardEntry& entry : entries) {
        output << std::setw(3) << entry.rank << ". " << entry.student_id << "  " << entry.score << '\n';
    }
}

void showQuizLeaderboard(const AnalyticsModule& analytics, const QuizRepository& quizzes, std::istream& input,
                         std::ostream& output) {
    std::vector<Quiz> all;
    const Result list_result = quizzes.listQuizzes(&all);
    if (!list_result.ok()) {
        output << "Could not list quizzes: " << list_result.message << '\n';
        return;
    }
    if (all.empty()) {
        output << "No quizzes exist yet.\n";
        return;
    }
    output << "\nQuizzes:\n";
    for (const Quiz& quiz : all) {
        output << "  " << quiz.quiz_id << "  " << quiz.title << '\n';
    }
    std::string quiz_id;
    if (!prompt(input, output, "Quiz id (blank to cancel): ", &quiz_id) || quiz_id.empty()) {
        return;
    }
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics.leaderboard(quiz_id, &entries);
    if (!result.ok()) {
        output << "Could not compute leaderboard: " << result.message << '\n';
        return;
    }
    printEntries("Leaderboard: " + quiz_id, entries, output);
}

void showTopicLeaderboard(const AnalyticsModule& analytics, std::istream& input, std::ostream& output) {
    std::string subject_tag;
    if (!prompt(input, output, "Subject tag (blank to cancel): ", &subject_tag) || subject_tag.empty()) {
        return;
    }
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics.topicLeaderboard(subject_tag, &entries);
    if (!result.ok()) {
        output << "Could not compute topic leaderboard: " << result.message << '\n';
        return;
    }
    printEntries("Topic leaderboard: " + subject_tag, entries, output);
}

void showAllTimeLeaderboard(const AnalyticsModule& analytics, std::ostream& output) {
    std::vector<LeaderboardEntry> entries;
    const Result result = analytics.allTimeLeaderboard(&entries);
    if (!result.ok()) {
        output << "Could not compute all-time leaderboard: " << result.message << '\n';
        return;
    }
    printEntries("All-time leaderboard", entries, output);
}

}  // namespace

void runAnalyticsMenu(const Session& session, const AnalyticsModule& analytics, const QuizRepository& quizzes,
                      std::istream& input, std::ostream& output) {
    if (session.role != Role::Instructor && session.role != Role::Admin) {
        output << "Only instructors and administrators can view analytics.\n";
        return;
    }
    for (;;) {
        output << "\n=== Analytics ===\n"
               << "1. Leaderboard for a quiz\n"
               << "2. Topic leaderboard\n"
               << "3. All-time leaderboard\n"
               << "0. Back\n";
        std::string choice;
        if (!prompt(input, output, "Select: ", &choice) || choice == "0") {
            return;
        }
        if (choice == "1") {
            showQuizLeaderboard(analytics, quizzes, input, output);
        } else if (choice == "2") {
            showTopicLeaderboard(analytics, input, output);
        } else if (choice == "3") {
            showAllTimeLeaderboard(analytics, output);
        } else {
            output << "Invalid option.\n";
        }
    }
}

}  // namespace quizsphere
