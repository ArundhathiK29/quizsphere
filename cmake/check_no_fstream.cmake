file(GLOB_RECURSE ALL_SOURCES
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
)

set(ALLOWED
    "${PROJECT_SOURCE_DIR}/src/question_repository_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/import_logger.cpp"
    "${PROJECT_SOURCE_DIR}/src/auth_event_log.cpp"
)

foreach(file_path IN LISTS ALL_SOURCES)
    list(FIND ALLOWED "${file_path}" allowed_idx)
    if(NOT allowed_idx EQUAL -1)
        continue()
    endif()

    file(READ "${file_path}" content)
    string(FIND "${content}" "#include <fstream>" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Disallowed <fstream> include in ${file_path}")
    endif()
endforeach()
