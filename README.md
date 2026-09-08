# QuizSphere

Online Quiz Assessment and Analytics System, built for UE24CS341A Software Engineering, Team 12.

## What it does
Lets instructors build a tagged question bank, create timed quizzes with configurable scoring (including negative marking), and lets students attempt quizzes in practice or graded mode with instant auto-grading, a leaderboard, and per-topic weak-area feedback.

## Tech stack
- C / C++ (CMake build)
- SQLite3 for local persistence
- GoogleTest for unit testing
- cppcheck for static analysis

## Team 12
- Arundhathi K - Auth and credential security
- Aryan Revankar - Question bank and CSV import/export
- Ashwin Venkatesh - Quiz configuration and timed attempt engine
- Chaturya Prabhakar Reddy - Auto-grading, leaderboard, analytics

## Build
    mkdir build
    cd build
    cmake ..
    cmake --build .
