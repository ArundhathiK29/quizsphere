#include "quizsphere.hpp"

#include <iostream>

int main() {
    auto question_bank = quizsphere::createQuestionBank();
    const quizsphere::OperationResult load_result = question_bank->load();
    if (!load_result.success) {
        std::cerr << "Startup failure: " << load_result.message << '\n';
        return 1;
    }

    quizsphere::Session demo_instructor_session{
        "demo-instructor",
        "Demo Instructor",
        quizsphere::Role::Instructor,
        true,
    };

    auto menu = quizsphere::createConsoleMenu(demo_instructor_session, *question_bank, std::cin, std::cout);
    menu->run();

    return 0;
}
