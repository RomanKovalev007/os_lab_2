cmake_minimum_required(VERSION 3.14)
project(laba2)


set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(run main.c)

add_executable(data generate_data.c)

