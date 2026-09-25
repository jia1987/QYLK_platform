cmake_minimum_required(VERSION 3.16)
project(massage_controller VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

# 协议核心层：纯 C++17，零 Qt 依赖（设计文档 §4）
add_subdirectory(src/core)

# Qt 应用层（Milestone 2 引入）：
# find_package(Qt...) 后 add_subdirectory(src/app)

add_subdirectory(tests)
