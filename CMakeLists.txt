cmake_minimum_required(VERSION 3.10)
project(oj_problem_015 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output the executable at the project root as required by OJ
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(code
  main.cpp
)

# Optimize for speed
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(code PRIVATE -O3 -pipe -DNDEBUG -march=native -flto=auto -fno-exceptions -fno-rtti)
  target_link_options(code PRIVATE -flto=auto)
endif()
