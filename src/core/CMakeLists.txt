add_library(massage_core STATIC
  crc16.cpp
  frame_codec.cpp
)

# 头文件以 core/xxx.h 形式引用
target_include_directories(massage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
