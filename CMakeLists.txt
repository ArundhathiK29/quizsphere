cmake_minimum_required(VERSION 3.15)
project(QuizSphere LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

add_library(quizsphere_auth
    src/auth.cpp
    src/sqlite_user_repository.cpp
)
target_include_directories(quizsphere_auth PUBLIC include)
target_link_libraries(quizsphere_auth PUBLIC SQLite3::SQLite3 bcrypt)

add_executable(auth_test tests/auth_test.cpp)
target_link_libraries(auth_test PRIVATE quizsphere_auth)

add_executable(sqlite_repository_test tests/sqlite_repository_test.cpp)
target_link_libraries(sqlite_repository_test PRIVATE quizsphere_auth)

enable_testing()
add_test(NAME auth_test COMMAND auth_test)
add_test(NAME sqlite_repository_test COMMAND sqlite_repository_test)
