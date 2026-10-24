#include "common_types.hpp"

namespace quizsphere {

std::string roleToString(Role role) {
    switch (role) {
        case Role::Student:
            return "Student";
        case Role::Instructor:
            return "Instructor";
        case Role::Admin:
            return "Admin";
    }
    return "Unknown";
}

}  // namespace quizsphere
