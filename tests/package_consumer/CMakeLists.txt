cmake_minimum_required(VERSION 3.16)
project(panicdump_consumer C)

find_package(panicdump CONFIG REQUIRED)

add_executable(panicdump_consumer main.c)
target_link_libraries(panicdump_consumer PRIVATE panicdump::panicdump)
