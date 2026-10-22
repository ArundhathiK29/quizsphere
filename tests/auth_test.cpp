
#include "auth.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace QuizSphere;

    // Username validation
    assert(isValidUsername("aruu"));
    assert(isValidUsername("aruu123"));
    assert(!isValidUsername(""));
    assert(!isValidUsername("   "));

    // Role validation
    assert(isValidRole(UserRole::Student));
    assert(isValidRole(UserRole::Instructor));

    std::cout << "All authentication validation tests passed.\n";
    return 0;
}
