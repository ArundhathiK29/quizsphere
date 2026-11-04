#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "quiz_engine.hpp"

namespace quizsphere {

struct AttemptQuestionView {
    PublicQuestion question;  // Options are in display order; no answer key.
    std::vector<std::size_t> original_option_indices;
    int selected_display_option{kBlankAnswer};
    bool flagged{false};
    bool visited{false};
};

struct AttemptReview {
    std::size_t total{0};
    std::size_t answered{0};
    std::size_t unanswered{0};
    std::vector<std::size_t> flagged_positions;  // zero-based attempt positions
};

struct PracticeFeedback {
    bool blank{true};
    bool correct{false};
    std::size_t correct_display_option{0};
    std::string correct_answer;
};

// Presentation/navigation state for one student. Shared Attempt and grading contracts are unchanged.
class QuizAttemptSession {
public:
    QuizAttemptSession(const Session& student, QuizEngine& engine, QuestionBank& bank);
    Result start(const std::string& quiz_id, QuizMode mode);
    Result question(AttemptQuestionView* view) const;
    Result next();
    Result previous();
    Result jump(std::size_t zero_based_position);
    Result select(int displayed_option);  // kBlankAnswer clears a response
    Result flag(bool flagged);
    Result feedback(PracticeFeedback* feedback) const;  // Practice only
    Result submit(bool confirmed, GradeReport* report);
    [[nodiscard]] AttemptReview review() const;
    [[nodiscard]] const Attempt& attempt() const { return attempt_; }
    [[nodiscard]] std::size_t position() const { return position_; }

private:
    Session student_;
    QuizEngine& engine_;
    QuestionBank& bank_;
    Attempt attempt_;
    std::vector<AttemptQuestionView> views_;
    std::size_t position_{0};
    Result requireOpen() const;
    Answer currentAnswer() const;
};

}  // namespace quizsphere
