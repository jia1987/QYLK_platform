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

add_subdirectory(tests)

# ---- Qt 应用层（可选：找到 Qt 才构建，MSVC 纯测试构建自动跳过）----
option(BUILD_APP "Build Qt application layer" ON)
if(BUILD_APP)
  find_package(QT NAMES Qt6 Qt5 COMPONENTS Core QUIET)
  if(QT_VERSION_MAJOR)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Quick QuickControls2 QUIET)
    if(Qt${QT_VERSION_MAJOR}Core_FOUND AND Qt${QT_VERSION_MAJOR}Quick_FOUND)
      set(CMAKE_AUTOMOC ON)
      set(CMAKE_AUTORCC ON)
      message(STATUS "Qt${QT_VERSION_MAJOR} found — building app layer")
      add_subdirectory(src/app)
    else()
      message(STATUS "Qt Quick 模块不全 — 仅构建核心与测试")
    endif()
  else()
    message(STATUS "未找到 Qt — 仅构建核心与测试（用 -DCMAKE_PREFIX_PATH=<Qt kit> 指定）")
  endif()
endif()
