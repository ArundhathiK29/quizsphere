cmake_minimum_required(VERSION 3.15)
project(QuizSphere LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(quizsphere_modules
    src/auth_module.cpp
    src/question_bank.cpp
    src/quiz_engine.cpp
    src/grading_module.cpp
    src/analytics_module.cpp
    src/data_access_layer.cpp
    src/console_ui.cpp
)

target_include_directories(quizsphere_modules PUBLIC include)

add_executable(quizsphere src/main.cpp)
target_link_libraries(quizsphere PRIVATE quizsphere_modules)

enable_testing()
