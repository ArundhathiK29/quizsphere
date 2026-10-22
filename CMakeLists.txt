
cmake_minimum_required(VERSION 3.10)
project(QuizSphere LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(quizsphere
    src/main.cpp
    src/auth.cpp
)

add_executable(auth_test
    tests/auth_test.cpp
    src/auth.cpp
)
