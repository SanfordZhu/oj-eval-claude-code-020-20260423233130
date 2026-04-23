cmake_minimum_required(VERSION 3.10)
project(buddy C)

add_executable(code main.c buddy.c)

target_compile_options(code PRIVATE -Wall -Wno-error)
