cmake_minimum_required(VERSION 3.16)
project(Snake VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Часть I: базовая игра ----
add_executable(snake base/snake.cpp)
target_compile_options(snake PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(snake PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- Часть II: вариант 6 «Сад» ----
add_executable(variant_06 variant_06/garden.cpp)
target_compile_options(variant_06 PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(variant_06 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

    add_test(NAME garden_rules COMMAND sh -c "printf q | $<TARGET_FILE:variant_06>")
    set_tests_properties(garden_rules PROPERTIES PASS_REGULAR_EXPRESSION "банан:  \\+2")

    add_test(NAME garden_wall COMMAND sh -c "printf ' ddddd' | $<TARGET_FILE:variant_06> --seed 1")
    set_tests_properties(garden_wall PROPERTIES PASS_REGULAR_EXPRESSION "врезалась в стену")

    # Полная партия до победы: съедено 6 яблок, 2 банана и 7 вишен, одна вишня упущена
    add_test(NAME garden_win COMMAND sh -c
        "printf ' sddwasssawwaaawwwwwaassssddssddddwdsddsaaaaaawaaawwwwwwddsdww' | $<TARGET_FILE:variant_06> --seed 3")
    set_tests_properties(garden_win PROPERTIES
        PASS_REGULAR_EXPRESSION "ПОБЕДА.*счёт: 31.*яблок — 6, бананов — 2, вишен — 7.*Упущено вишен: 1")
endif()
