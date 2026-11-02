cmake_minimum_required(VERSION 3.16)
project(Snake VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Часть I: базовая игра ----
add_executable(snake base/snake.cpp)
target_compile_options(snake PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(snake PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- Тесты: партии по заранее записанным нажатиям клавиш ----
# Первый символ закрывает экран правил, дальше идут ходы.
# --seed фиксирует случайность, поэтому еда каждый раз появляется в тех же клетках.
include(CTest)
if (BUILD_TESTING)
    add_test(NAME snake_rules COMMAND sh -c "printf q | $<TARGET_FILE:snake>")
    set_tests_properties(snake_rules PROPERTIES PASS_REGULAR_EXPRESSION "Цель игры")

    add_test(NAME snake_wall COMMAND sh -c "printf ' ddddd' | $<TARGET_FILE:snake> --seed 1")
    set_tests_properties(snake_wall PROPERTIES PASS_REGULAR_EXPRESSION "врезалась в стену")

    add_test(NAME snake_help COMMAND sh -c "printf ' dhxq' | $<TARGET_FILE:snake> --seed 1")
    set_tests_properties(snake_help PROPERTIES PASS_REGULAR_EXPRESSION "вернуться к игре")

    add_test(NAME snake_tail COMMAND sh -c
        "printf ' sddwwasaaaassddddwwwaaaaaasssddddddwwwwwwwdsawdsawq' | $<TARGET_FILE:snake> --seed 3")
    set_tests_properties(snake_tail PROPERTIES
        PASS_REGULAR_EXPRESSION "врезалась в свой хвост.*Итоговый счёт: 7")
endif()
