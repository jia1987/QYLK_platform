# 每个测试为独立可执行文件，注册进 ctest（注册验证记录的可执行载体）
function(add_core_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE massage_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_frame_codec)
add_core_test(test_profile)
add_core_test(test_head_state)
add_core_test(test_bus_scheduler)
