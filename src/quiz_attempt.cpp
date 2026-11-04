#include "quiz_attempt.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

namespace quizsphere {
namespace {

// Stable across reloads and standard-library implementations: mt19937_64 output is specified,
// and bounded sampling is explicit rather than depending on std::shuffle's implementation.
std::vector<std::size_t> optionOrder(const Attempt& attempt, const PublicQuestion& question) {
    const std::string key = attempt.attempt_id + ":" + question.question_id;
    std::vector<std::uint32_t> key_bytes(key.size());
    std::transform(key.begin(), key.end(), key_bytes.begin(), [](unsigned char byte) {
        return static_cast<std::uint32_t>(byte);
    });
    std::seed_seq seed(key_bytes.begin(), key_bytes.end());
    std::mt19937_64 random(seed);
    std::vector<std::size_t> order(question.options.size());
    std::iota(order.begin(), order.end(), 0);
    for (std::size_t n=order.size(); n>1; --n) {
        const auto bound = static_cast<std::uint64_t>(n);
        const auto limit = std::numeric_limits<std::uint64_t>::max() -
                           (std::numeric_limits<std::uint64_t>::max() % bound);
        std::uint64_t value;
        do { value = random(); } while (value >= limit);
        std::swap(order[n-1], order[static_cast<std::size_t>(value % bound)]);
    }
    return order;
}

}  // namespace

QuizAttemptSession::QuizAttemptSession(const Session& student, QuizEngine& engine, QuestionBank& bank)
    : student_(student), engine_(engine), bank_(bank) {}

Result QuizAttemptSession::start(const std::string& quiz_id, QuizMode mode) {
    Attempt candidate;
    auto result = engine_.startAttempt(student_, quiz_id, mode, &candidate);
    if (!result.ok()) return result;
    if (candidate.question_order.empty()) return {ErrorCode::VALIDATION_FAILED, "Attempt has no questions."};
    std::vector<AttemptQuestionView> views;
    for (const auto& id : candidate.question_order) {
        AttemptQuestionView view;
        PublicQuestion original;
        result = bank_.getQuestionForQuiz(id, &original);
        if (!result.ok()) return result;
        view.original_option_indices = optionOrder(candidate, original);
        view.question = original;
        for (std::size_t i=0; i<original.options.size(); ++i)
            view.question.options[i] = original.options[view.original_option_indices[i]];
        views.push_back(std::move(view));
    }
    attempt_ = std::move(candidate);
    views_ = std::move(views);
    position_ = 0;
    views_[0].visited = true;
    return result;
}

Result QuizAttemptSession::requireOpen() const {
    if (views_.empty()) return {ErrorCode::VALIDATION_FAILED, "Start an attempt first."};
    if (attempt_.submitted) return {ErrorCode::ATTEMPT_ALREADY_SUBMITTED, "Attempt already submitted."};
    return {};
}

Answer QuizAttemptSession::currentAnswer() const {
    const auto& id = attempt_.question_order[position_];
    const auto found = std::find_if(attempt_.answers.begin(), attempt_.answers.end(), [&](const Answer& a) {
        return a.question_id == id;
    });
    return found == attempt_.answers.end() ? Answer{id, kBlankAnswer, false} : *found;
}

Result QuizAttemptSession::question(AttemptQuestionView* out) const {
    if (!out) return {ErrorCode::VALIDATION_FAILED, "Question output must not be null."};
    if (views_.empty()) return {ErrorCode::VALIDATION_FAILED, "Start an attempt first."};
    *out = views_[position_];
    const auto answer = currentAnswer();
    out->flagged = answer.flagged_for_review;
    out->selected_display_option = kBlankAnswer;
    if (answer.selected_option != kBlankAnswer) {
        const auto it = std::find(out->original_option_indices.begin(), out->original_option_indices.end(),
                                  static_cast<std::size_t>(answer.selected_option));
        if (it == out->original_option_indices.end()) return {ErrorCode::VALIDATION_FAILED, "Stored answer mapping is invalid."};
        out->selected_display_option = static_cast<int>(it - out->original_option_indices.begin());
    }
    return {};
}

Result QuizAttemptSession::jump(std::size_t target) {
    const auto result = requireOpen();
    if (!result.ok()) return result;
    if (target >= views_.size()) return {ErrorCode::VALIDATION_FAILED, "Question number is out of range."};
    position_ = target;
    views_[position_].visited = true;
    return {};
}

Result QuizAttemptSession::next() { return jump(position_ + 1); }
Result QuizAttemptSession::previous() {
    if (position_ == 0) return {ErrorCode::VALIDATION_FAILED, "Already at the first question."};
    return jump(position_ - 1);
}

Result QuizAttemptSession::select(int displayed_option) {
    const auto result = requireOpen();
    if (!result.ok()) return result;
    const auto& order = views_[position_].original_option_indices;
    if (displayed_option < kBlankAnswer || (displayed_option != kBlankAnswer &&
        static_cast<std::size_t>(displayed_option) >= order.size()))
        return {ErrorCode::VALIDATION_FAILED, "Displayed option is out of range."};
    auto answer = currentAnswer();
    answer.selected_option = displayed_option == kBlankAnswer ? kBlankAnswer :
        static_cast<int>(order[static_cast<std::size_t>(displayed_option)]);
    return engine_.saveAnswer(&attempt_, answer);
}

Result QuizAttemptSession::flag(bool flagged) {
    const auto result = requireOpen();
    if (!result.ok()) return result;
    auto answer = currentAnswer();
    answer.flagged_for_review = flagged;
    return engine_.saveAnswer(&attempt_, answer);
}

Result QuizAttemptSession::feedback(PracticeFeedback* out) const {
    const auto result = requireOpen();
    if (!result.ok()) return result;
    if (attempt_.mode != QuizMode::Practice) return {ErrorCode::NOT_AUTHORIZED, "Feedback is available only in practice mode."};
    if (!out) return {ErrorCode::VALIDATION_FAILED, "Feedback output must not be null."};
    // Trusted internal access, matching GradingModule's answer-key boundary; never exposed in graded views.
    const Session reader{"system:practice", Role::Admin, "1970-01-01T00:00:00Z"};
    Question original;
    const auto read = bank_.findQuestionById(reader, attempt_.question_order[position_], &original);
    if (!read.ok()) return read;
    if (original.correct_option >= original.options.size()) return {ErrorCode::VALIDATION_FAILED, "Invalid answer key."};
    const auto& order = views_[position_].original_option_indices;
    const auto correct = std::find(order.begin(), order.end(), original.correct_option);
    if (correct == order.end()) return {ErrorCode::VALIDATION_FAILED, "Invalid answer-key mapping."};
    const auto answer = currentAnswer();
    *out = {answer.selected_option == kBlankAnswer,
            answer.selected_option != kBlankAnswer && static_cast<std::size_t>(answer.selected_option) == original.correct_option,
            static_cast<std::size_t>(correct-order.begin()), original.options[original.correct_option]};
    return {};
}

AttemptReview QuizAttemptSession::review() const {
    AttemptReview review;
    review.total = views_.size();
    for (std::size_t i=0; i<attempt_.question_order.size(); ++i) {
        const auto found = std::find_if(attempt_.answers.begin(), attempt_.answers.end(), [&](const Answer& a) {
            return a.question_id == attempt_.question_order[i];
        });
        if (found == attempt_.answers.end()) continue;
        if (found->selected_option != kBlankAnswer) ++review.answered;
        if (found->flagged_for_review) review.flagged_positions.push_back(i);
    }
    review.unanswered = review.total-review.answered;
    return review;
}

Result QuizAttemptSession::submit(bool confirmed, GradeReport* report) {
    const auto result = requireOpen();
    if (!result.ok()) return result;
    if (!confirmed) return {ErrorCode::CONFIRMATION_REQUIRED, "Confirm submission after reviewing answers."};
    return engine_.submitAttempt(student_, &attempt_, false, report);
}

}  // namespace quizsphere
