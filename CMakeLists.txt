cmake_minimum_required(VERSION 3.16)
project(massage_controller VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

# ---- Qt 探测（提前：tests 里的 Qt 集成测试与 src/app 都需要）----
option(BUILD_APP "Build Qt application layer" ON)
set(MASSAGE_QT_FOUND FALSE)
if(BUILD_APP)
  find_package(QT NAMES Qt6 Qt5 COMPONENTS Core QUIET)
  if(QT_VERSION_MAJOR)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Quick QuickControls2 QUIET)
    if(Qt${QT_VERSION_MAJOR}Core_FOUND AND Qt${QT_VERSION_MAJOR}Quick_FOUND)
      set(MASSAGE_QT_FOUND TRUE)
      set(CMAKE_AUTOMOC ON)
      set(CMAKE_AUTORCC ON)
      message(STATUS "Qt${QT_VERSION_MAJOR} found — building app layer + Qt integration tests")
    else()
      message(STATUS "Qt Quick 模块不全 — 仅构建核心与纯 C++ 测试")
    endif()
  else()
    message(STATUS "未找到 Qt — 仅构建核心与纯 C++ 测试（用 -DCMAKE_PREFIX_PATH=<Qt kit> 指定）")
  endif()
endif()

# 协议核心层：纯 C++17，零 Qt 依赖（设计文档 §4）
add_subdirectory(src/core)

# 审计追踪引擎：纯 C++17，零 Qt 依赖（M4a，决策 D29，sqlite3 嵌入）
add_subdirectory(src/audit)

add_subdirectory(tests)

if(MASSAGE_QT_FOUND)
  add_subdirectory(src/app)
endif()
