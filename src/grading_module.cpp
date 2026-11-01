#include "grading_module.hpp"

namespace quizsphere {
namespace {

// BL-01 stub. Chaturya/Ashwin replace the bodies (BL-10, BL-25, BL-43); keep the interface in grading_module.hpp.
class StubGradingModule final : public GradingModule {
public:
    Result gradeAttempt(const std::string&, const std::string&, const std::vector<Answer>&, GradeReport*) override {
        return notImplemented("GradingModule::gradeAttempt");
    }
    Result getReport(const std::string&, const std::string&, GradeReport*) const override {
        return notImplemented("GradingModule::getReport");
    }
};

}  // namespace

std::unique_ptr<GradingModule> createGradingModule(QuestionBank&, QuizRepository&, AttemptRepository&) {
    return std::make_unique<StubGradingModule>();
}

}  // namespace quizsphere
