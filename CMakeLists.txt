cmake_minimum_required(VERSION 3.16)
project(SandboxBoundaryTester LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(sbt sbt.cpp)
add_executable(probe_confidentiality probe_confidentiality.cpp)
add_executable(probe_integrity probe_integrity.cpp)
