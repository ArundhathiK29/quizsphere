#include "console_input.hpp"

#include <sstream>

#include <gtest/gtest.h>

TEST(ConsoleInput, PasswordUsesOneLineWithoutEchoingOrTrimmingTheSecret) {
    std::istringstream input("  pass phrase  \nnext line\n");
    std::ostringstream output;
    std::string password;
    ASSERT_TRUE(quizsphere::readConsolePassword(input, output, "Password: ", password));
    EXPECT_EQ(password, "  pass phrase  ");
    EXPECT_EQ(output.str(), "Password: ");
    std::string next;
    ASSERT_TRUE(std::getline(input, next));
    EXPECT_EQ(next, "next line");
}

TEST(ConsoleInput, EndOfInputDoesNotReuseAnEarlierPassword) {
    std::istringstream input;
    std::ostringstream output;
    std::string password = "old secret";
    EXPECT_FALSE(quizsphere::readConsolePassword(input, output, "Password: ", password));
    EXPECT_TRUE(password.empty());
    EXPECT_EQ(output.str(), "Password: ");
}
