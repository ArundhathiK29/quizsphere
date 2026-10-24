#pragma once

#include <string>
#include <vector>

namespace QuizSphere {

// Shared answer representation. Confirm field names with the team.
struct Answer {
    std::string questionId;
    std::string selectedOption;
};

// Shared attempt representation. Fields are provisional pending team agreement.
struct Attempt {
    std::string quizId;
    std::string studentId;
    std::vector<Answer> answers;
    bool autoSubmitted = false;
};

} // namespace QuizSphere
