cmake_minimum_required(VERSION 3.13.4)
project(CustomPasses)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Can also be an option
# add_library(CustomPasses SHARED src/Passes.cpp)

add_library(CustomPasses MODULE src/Passes.cpp src/LoopFuse.cpp)

target_link_libraries(CustomPasses LLVM)

