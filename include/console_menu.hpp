#pragma once

#include <istream>
#include <memory>
#include <ostream>
#include <string>

#include "common_types.hpp"
#include "question_bank.hpp"

namespace quizsphere {

class ConsoleMenu {
public:
    virtual ~ConsoleMenu() = default;
    virtual void run() = 0;
};

std::unique_ptr<ConsoleMenu> createConsoleMenu(const Session& session,
                                               std::string display_name,
                                               QuestionBank& question_bank,
                                               std::istream& input,
                                               std::ostream& output);

}  // namespace quizsphere
