#include <iostream>

#include "console_menu.hpp"
#include "question_bank.hpp"
#include "question_repository.hpp"

int main() {
    auto repository = quizsphere::createFileQuestionRepository();
    auto question_bank = quizsphere::createQuestionBank(std::move(repository));

    const auto load_result = question_bank->load();
    if (!load_result.ok()) {
        std::cerr << "Startup failure: " << load_result.message << '\n';
        return 1;
    }

#if defined(QUIZSPHERE_DEMO_SESSION_ENABLED)
    std::cerr << "WARNING: QUIZSPHERE_DEMO_SESSION is ON. Demo Instructor session enabled for local development only.\n";
    const auto session = quizsphere::createDemoSession();
    if (!session.has_value()) {
        std::cerr << "Startup failure: unable to create demo session.\n";
        return 1;
    }
    auto menu = quizsphere::createConsoleMenu(*session, "Demo Instructor", *question_bank, std::cin, std::cout);
    menu->run();
    return 0;
#else
    std::cerr << "No authenticated session available. Management menu is disabled.\n";
    return 1;
#endif
}
