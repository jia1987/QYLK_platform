# @srs SRS-003
# 审计追踪引擎（M4a，决策 D13–D22/D29）：纯 C++17，零 Qt 依赖。
# sqlite3 amalgamation 静态嵌入（third_party/sqlite3，版本溯源见其 SOUP-NOTE.md）。
find_package(Threads REQUIRED)

add_library(sqlite3 STATIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3)
set_target_properties(sqlite3 PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0                 # 禁双引号字符串（SQL 卫生）
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_LOAD_EXTENSION   # 禁扩展加载（缩小攻击面）
)
# 第三方 vendored 代码：抑制全部警告（非本项目代码，不参与 -Wall 治理）
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /w)
else()
  target_compile_options(sqlite3 PRIVATE -w)
endif()
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_library(massage_audit STATIC
  audit_log.cpp
  audit_export.cpp
  sha256.cpp
  json_mini.cpp
)
# 头文件以 audit/xxx.h 形式引用（与 core/ 同款约定）
target_include_directories(massage_audit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(massage_audit PUBLIC sqlite3)
