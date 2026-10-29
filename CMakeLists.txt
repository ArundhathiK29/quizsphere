cmake_minimum_required(VERSION 3.20)
project(QuizSphere LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUIZSPHERE_BUILD_TESTS "Build QuizSphere tests" ON)
option(QUIZSPHERE_COVERAGE "Enable coverage flags" OFF)
option(QUIZSPHERE_DEMO_SESSION "Enable demo session in main" OFF)

function(quizsphere_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

function(quizsphere_enable_coverage target_name)
    if(QUIZSPHERE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE --coverage -O0 -g)
            target_link_options(${target_name} PRIVATE --coverage)
        else()
            message(WARNING "Coverage option is enabled but compiler does not support gcov-style flags.")
        endif()
    endif()
endfunction()

find_package(SQLite3 REQUIRED)
# CMake 3.14-3.x calls the target SQLite::SQLite3; newer CMake prefers SQLite3::SQLite3.
if(TARGET SQLite3::SQLite3)
    set(QUIZSPHERE_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(QUIZSPHERE_SQLITE_TARGET SQLite::SQLite3)
endif()
find_package(OpenSSL REQUIRED)

add_library(quizsphere_core
    src/common_types.cpp
    src/question_bank.cpp
    src/question_repository_file.cpp
    src/console_menu.cpp
    src/import_logger.cpp
    src/password_hasher.cpp
    src/auth.cpp
    src/auth_event_log.cpp
    src/sqlite_user_repository.cpp
    src/data_access_layer.cpp
    src/grading_module.cpp
    src/quiz_engine.cpp
    src/analytics_module.cpp
    src/result_view.cpp
)

target_include_directories(quizsphere_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quizsphere_core PUBLIC ${QUIZSPHERE_SQLITE_TARGET} OpenSSL::Crypto)
quizsphere_enable_warnings(quizsphere_core)
quizsphere_enable_coverage(quizsphere_core)

add_executable(quizsphere src/main.cpp)
target_link_libraries(quizsphere PRIVATE quizsphere_core)
quizsphere_enable_warnings(quizsphere)
quizsphere_enable_coverage(quizsphere)

if(QUIZSPHERE_DEMO_SESSION)
    target_compile_definitions(quizsphere PRIVATE QUIZSPHERE_DEMO_SESSION_ENABLED=1)
endif()

if(QUIZSPHERE_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "System GoogleTest not found; fetching v1.14.0")
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
    include(GoogleTest)

    add_executable(question_bank_tests tests/question_bank_tests.cpp)
    target_link_libraries(question_bank_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(question_bank_tests)
    quizsphere_enable_coverage(question_bank_tests)

    add_test(NAME question_bank_tests COMMAND question_bank_tests)

    add_executable(auth_tests
        tests/password_hasher_tests.cpp
        tests/auth_tests.cpp
        tests/auth_event_log_tests.cpp
        tests/sqlite_user_repository_tests.cpp
        tests/module_interfaces_tests.cpp
    )
    target_link_libraries(auth_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(auth_tests)
    quizsphere_enable_coverage(auth_tests)
    gtest_discover_tests(auth_tests DISCOVERY_TIMEOUT 120)

    add_executable(grading_module_tests tests/grading_module_tests.cpp)
    target_link_libraries(grading_module_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(grading_module_tests)
    quizsphere_enable_coverage(grading_module_tests)
    gtest_discover_tests(grading_module_tests DISCOVERY_TIMEOUT 120)

        add_executable(result_view_tests tests/result_view_tests.cpp)
    target_link_libraries(result_view_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(result_view_tests)
    quizsphere_enable_coverage(result_view_tests)

    add_test(NAME result_view_tests COMMAND result_view_tests)

        add_executable(analytics_module_tests tests/analytics_module_tests.cpp)
    target_link_libraries(analytics_module_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(analytics_module_tests)
    quizsphere_enable_coverage(analytics_module_tests)

    add_test(NAME analytics_module_tests COMMAND analytics_module_tests)

    add_test(
        NAME no_fstream_outside_repository_or_logging
        COMMAND ${CMAKE_COMMAND} -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_no_fstream.cmake
    )
endif()
