cmake_minimum_required(VERSION 3.20)
project(QuizSphere LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUIZSPHERE_BUILD_TESTS "Build QuizSphere tests" ON)
option(QUIZSPHERE_COVERAGE "Enable coverage flags" OFF)
option(QUIZSPHERE_BUILD_WEB "Build the optional local browser interface" ON)

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
    src/input_validation.cpp
    src/common_types.cpp
    src/console_input.cpp
    src/admin_user_menu.cpp
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
    src/quiz_attempt.cpp
    src/quiz_console.cpp
    src/timed_console_input.cpp
    src/analytics_module.cpp
    src/analytics_console.cpp
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

if(QUIZSPHERE_BUILD_WEB)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" WEB_HTML)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/app.css" WEB_CSS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js" WEB_JS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS web/index.html web/app.css web/app.js)
    configure_file(web/assets.hpp.in generated/web_assets.hpp @ONLY)
    add_library(quizsphere_web_adapter src/web_application.cpp)
    target_link_libraries(quizsphere_web_adapter PUBLIC quizsphere_core)
    target_include_directories(quizsphere_web_adapter PUBLIC include)
    quizsphere_enable_warnings(quizsphere_web_adapter)
    quizsphere_enable_coverage(quizsphere_web_adapter)
    add_executable(quizsphere_web src/web_server.cpp)
    target_include_directories(quizsphere_web PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(quizsphere_web PRIVATE quizsphere_web_adapter)
    if(WIN32)
        target_link_libraries(quizsphere_web PRIVATE ws2_32)
    endif()
    quizsphere_enable_warnings(quizsphere_web)
    quizsphere_enable_coverage(quizsphere_web)
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

    if(QUIZSPHERE_BUILD_WEB)
        add_executable(web_application_tests tests/web_application_tests.cpp)
        target_link_libraries(web_application_tests PRIVATE quizsphere_web_adapter GTest::gtest GTest::gtest_main)
        quizsphere_enable_warnings(web_application_tests)
        quizsphere_enable_coverage(web_application_tests)
        gtest_discover_tests(web_application_tests DISCOVERY_TIMEOUT 120)
    endif()

    add_executable(question_bank_tests tests/question_bank_tests.cpp)
    target_link_libraries(question_bank_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(question_bank_tests)
    quizsphere_enable_coverage(question_bank_tests)

    add_test(NAME question_bank_tests COMMAND question_bank_tests)

    add_executable(auth_tests
        tests/password_hasher_tests.cpp
        tests/console_input_tests.cpp
        tests/auth_tests.cpp
        tests/auth_role_boundary_tests.cpp
        tests/auth_event_log_tests.cpp
        tests/sqlite_user_repository_tests.cpp
        tests/module_interfaces_tests.cpp
    )
    target_link_libraries(auth_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(auth_tests)
    quizsphere_enable_coverage(auth_tests)
    gtest_discover_tests(auth_tests DISCOVERY_TIMEOUT 120)

    add_executable(admin_user_menu_tests tests/admin_user_menu_tests.cpp)
    target_link_libraries(admin_user_menu_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(admin_user_menu_tests)
    quizsphere_enable_coverage(admin_user_menu_tests)
    gtest_discover_tests(admin_user_menu_tests DISCOVERY_TIMEOUT 120)
    add_test(
        NAME ST_ADM_01_AdminUserFlow
        COMMAND ${CMAKE_COMMAND}
            "-DAPP=$<TARGET_FILE:quizsphere>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/admin-system-flow"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/admin_user_flow.cmake
    )
    set_tests_properties(ST_ADM_01_AdminUserFlow PROPERTIES TIMEOUT 180)

    add_executable(data_access_layer_tests
        tests/data_access_layer_tests.cpp
    )
    find_package(Threads REQUIRED)
    target_link_libraries(data_access_layer_tests
        PRIVATE quizsphere_core GTest::gtest GTest::gtest_main Threads::Threads
    )
    quizsphere_enable_warnings(data_access_layer_tests)
    quizsphere_enable_coverage(data_access_layer_tests)
    gtest_discover_tests(data_access_layer_tests DISCOVERY_TIMEOUT 120)

    add_executable(input_validation_tests tests/input_validation_tests.cpp)
    target_link_libraries(input_validation_tests PRIVATE quizsphere_core GTest::gtest_main)
    quizsphere_enable_warnings(input_validation_tests)
    quizsphere_enable_coverage(input_validation_tests)
    gtest_discover_tests(input_validation_tests DISCOVERY_TIMEOUT 120)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ST_SEC_08_ActualConsoleValidation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/security_console_flow.py $<TARGET_FILE:quizsphere>)
    set_tests_properties(ST_SEC_08_ActualConsoleValidation PROPERTIES TIMEOUT 90)
    add_test(NAME ST_SEC_09_TimedConfirmationControls
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/timed_confirmation_flow.py $<TARGET_FILE:quizsphere>)
    set_tests_properties(ST_SEC_09_TimedConfirmationControls PROPERTIES TIMEOUT 60)
    if(QUIZSPHERE_BUILD_WEB)
        foreach(timer_case slow-header slow-body practice-switch)
            add_test(NAME web_timer_${timer_case}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/web_timer_regression.py
                    $<TARGET_FILE:quizsphere_web> ${timer_case})
            set_tests_properties(web_timer_${timer_case} PROPERTIES TIMEOUT 45)
        endforeach()
    endif()

    add_executable(quiz_engine_tests
        tests/quiz_engine_tests.cpp
        tests/quiz_engine_sqlite_tests.cpp
        tests/quiz_attempt_tests.cpp
        tests/quiz_timer_tests.cpp
    )
    target_link_libraries(quiz_engine_tests
        PRIVATE quizsphere_core GTest::gtest GTest::gtest_main
    )
    quizsphere_enable_warnings(quiz_engine_tests)
    quizsphere_enable_coverage(quiz_engine_tests)
    gtest_discover_tests(quiz_engine_tests DISCOVERY_TIMEOUT 120)

    add_executable(grading_module_tests tests/grading_module_tests.cpp)
    target_link_libraries(grading_module_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(grading_module_tests)
    quizsphere_enable_coverage(grading_module_tests)
    gtest_discover_tests(grading_module_tests DISCOVERY_TIMEOUT 120)

    add_executable(grading_module_sqlite_integration_tests tests/grading_module_sqlite_integration_tests.cpp)
    target_link_libraries(grading_module_sqlite_integration_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(grading_module_sqlite_integration_tests)
    quizsphere_enable_coverage(grading_module_sqlite_integration_tests)
    add_test(NAME grading_module_sqlite_integration_tests COMMAND grading_module_sqlite_integration_tests)

    add_executable(result_view_tests tests/result_view_tests.cpp)
    target_link_libraries(result_view_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(result_view_tests)
    quizsphere_enable_coverage(result_view_tests)

        add_test(NAME result_view_tests COMMAND result_view_tests)

    add_executable(result_view_integration_tests tests/result_view_integration_tests.cpp)
    target_link_libraries(result_view_integration_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(result_view_integration_tests)
    quizsphere_enable_coverage(result_view_integration_tests)
    add_test(NAME result_view_integration_tests COMMAND result_view_integration_tests)

        add_executable(analytics_module_tests
        tests/analytics_module_tests.cpp
        tests/leaderboard_sqlite_tests.cpp
    )
    target_link_libraries(analytics_module_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(analytics_module_tests)
    quizsphere_enable_coverage(analytics_module_tests)

    add_test(NAME analytics_module_tests COMMAND analytics_module_tests)

    add_executable(analytics_module_sqlite_integration_tests tests/analytics_module_sqlite_integration_tests.cpp)
    target_link_libraries(analytics_module_sqlite_integration_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(analytics_module_sqlite_integration_tests)
    quizsphere_enable_coverage(analytics_module_sqlite_integration_tests)
    add_test(NAME analytics_module_sqlite_integration_tests COMMAND analytics_module_sqlite_integration_tests)

    add_executable(topic_analytics_integration_tests tests/topic_analytics_integration_tests.cpp)
    target_link_libraries(topic_analytics_integration_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(topic_analytics_integration_tests)
    quizsphere_enable_coverage(topic_analytics_integration_tests)
    add_test(NAME topic_analytics_integration_tests COMMAND topic_analytics_integration_tests)

    add_executable(transcript_export_integration_tests tests/transcript_export_integration_tests.cpp)
    target_link_libraries(transcript_export_integration_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(transcript_export_integration_tests)
    quizsphere_enable_coverage(transcript_export_integration_tests)
    add_test(NAME transcript_export_integration_tests COMMAND transcript_export_integration_tests)

    add_executable(submitted_attempt_protection_tests tests/submitted_attempt_protection_tests.cpp)
    target_link_libraries(submitted_attempt_protection_tests PRIVATE quizsphere_core GTest::gtest GTest::gtest_main)
    quizsphere_enable_warnings(submitted_attempt_protection_tests)
    quizsphere_enable_coverage(submitted_attempt_protection_tests)
    add_test(NAME submitted_attempt_protection_tests COMMAND submitted_attempt_protection_tests)

    add_test(
        NAME no_fstream_outside_repository_or_logging
        COMMAND ${CMAKE_COMMAND} -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_no_fstream.cmake
    )
endif()
