#pragma once

#include <istream>
#include <ostream>

#include "auth.hpp"

namespace quizsphere {

// UC-09 / SEC-003. Revalidates the actor's stored Admin role and enabled status.
// Account storage and changes go through the repository/auth boundaries.
void runAdminUserMenu(const Session& session, IUserRepository& users,
                      std::istream& input, std::ostream& output);

}  // namespace quizsphere
