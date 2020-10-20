project(torsken)
cmake_minimum_required(VERSION 2.8)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fsanitize=address -fsanitize=undefined")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

add_library(torsken32 SHARED torsken.cpp)
set_target_properties(torsken32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
target_link_libraries(torsken32 dl)

add_library(torsken64 SHARED torsken.cpp)
set_target_properties(torsken64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
target_link_libraries(torsken64 dl)
