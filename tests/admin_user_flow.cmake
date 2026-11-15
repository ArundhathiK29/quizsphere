# ST-ADM-01: drive the actual CLI across process restarts using synthetic accounts.
if(NOT DEFINED APP OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "APP and TEST_DIR are required")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}")
set(database "${TEST_DIR}/users.sqlite")
file(REMOVE "${database}" "${database}-journal" "${database}-wal" "${database}-shm" "${TEST_DIR}/auth.log")

function(run_step label commands)
    set(input_file "${TEST_DIR}/${label}.input")
    file(WRITE "${input_file}" "${commands}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "QUIZSPHERE_DB=${database}" "QUIZSPHERE_AUTH_LOG=${TEST_DIR}/auth.log" "${APP}"
        INPUT_FILE "${input_file}"
        OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE result
        WORKING_DIRECTORY "${TEST_DIR}" TIMEOUT 25
    )
    file(REMOVE "${input_file}")
    file(WRITE "${TEST_DIR}/${label}.txt" "${text}\n${errors}")
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${label}: CLI failed (${result}): ${errors}")
    endif()
    set(step_output "${text}" PARENT_SCOPE)
endfunction()

function(expect_text text expected)
    string(FIND "${text}" "${expected}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "Expected console text not found: ${expected}")
    endif()
endfunction()

function(reject_text text forbidden)
    string(FIND "${text}" "${forbidden}" index)
    if(NOT index EQUAL -1)
        message(FATAL_ERROR "Unexpected console text: ${forbidden}")
    endif()
endfunction()

# These are disposable fixture credentials, never production accounts.
set(secret "FlowPass123!")
set(admin_login "1\nroot_admin\n${secret}\n")
run_step(create_role_disable
    "root_admin\n${secret}\n${secret}\n${admin_login}2\n2\nflow_user\n1\n${secret}\n${secret}\nYES\n3\nflow_user\n2\nYES\n4\nflow_user\n2\nYES\n0\n0\n0\n")
expect_text("${step_output}" "Account created: flow_user (Student)")
expect_text("${step_output}" "Role updated: flow_user -> Instructor")
expect_text("${step_output}" "Account disabled: flow_user")
reject_text("${step_output}" "Manage users (coming soon)")
reject_text("${step_output}" "${secret}")

run_step(disabled_login_then_enable
    "1\nflow_user\n${secret}\n${admin_login}2\n4\nflow_user\n1\nYES\n0\n0\n0\n")
expect_text("${step_output}" "This account has been disabled.")
expect_text("${step_output}" "Account enabled: flow_user")

run_step(instructor_denied "1\nflow_user\n${secret}\n5\n0\n0\n")
expect_text("${step_output}" "=== QuizSphere: Instructor Menu ===")
expect_text("${step_output}" "Invalid option.")
reject_text("${step_output}" "Manage users")

run_step(change_back_to_student "${admin_login}2\n3\nflow_user\n1\nYES\n0\n0\n0\n")
expect_text("${step_output}" "Role updated: flow_user -> Student")

run_step(student_denied "1\nflow_user\n${secret}\n5\n0\n0\n")
expect_text("${step_output}" "=== QuizSphere: Student Menu ===")
expect_text("${step_output}" "Invalid option.")
reject_text("${step_output}" "Manage users")

run_step(cancel_then_remove
    "${admin_login}2\n5\nflow_user\nNO\n1\nflow_user\n5\nflow_user\nYES\n5\nroot_admin\nYES\n0\n0\n0\n")
expect_text("${step_output}" "Cancelled.")
expect_text("${step_output}" "User: flow_user | Role: Student | Status: Enabled")
expect_text("${step_output}" "Account removed: flow_user")
expect_text("${step_output}" "Permission denied")

run_step(removal_survives_restart "1\nflow_user\n${secret}\n${admin_login}2\n1\nflow_user\n0\n0\n0\n")
expect_text("${step_output}" "Invalid username or password.")
expect_text("${step_output}" "=== QuizSphere: Admin Menu ===")
expect_text("${step_output}" "No such user.")
reject_text("${step_output}" "${secret}")
message(STATUS "ST-ADM-01 passed: persisted Admin lifecycle, confirmation, self-protection and non-admin menu boundaries")
