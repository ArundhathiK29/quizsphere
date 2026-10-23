cmake_minimum_required(VERSION 3.10)
project(QuizSphere LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUIZSPHERE_BUILD_TESTS "Build QuizSphere tests" ON)

add_library(quizsphere_core
    src/quizsphere.cpp
)

target_include_directories(quizsphere_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(quizsphere_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(quizsphere_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(quizsphere src/main.cpp)
target_link_libraries(quizsphere PRIVATE quizsphere_core)

if(QUIZSPHERE_BUILD_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        add_executable(question_bank_tests tests/question_bank_tests.cpp)
        target_link_libraries(question_bank_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
        if(MSVC)
            target_compile_options(question_bank_tests PRIVATE /W4 /permissive-)
        else()
            target_compile_options(question_bank_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME question_bank_tests COMMAND question_bank_tests)
    else()
        message(WARNING "GoogleTest not found; skipping test target build.")
    endif()
endif()
