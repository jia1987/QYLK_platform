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

# Qt 集成测试：换头烧录安全联锁（AppCore + MockBus，仅 Qt::Core）
if(MASSAGE_QT_FOUND)
  add_executable(test_maint_burn
    test_maint_burn.cpp
    ${CMAKE_SOURCE_DIR}/src/app/app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/app/head_item.cpp
    ${CMAKE_SOURCE_DIR}/src/app/app_config.cpp
    ${CMAKE_SOURCE_DIR}/src/app/mock_bus.cpp)
  target_include_directories(test_maint_burn PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(test_maint_burn PRIVATE
    massage_core Qt${QT_VERSION_MAJOR}::Core)
  add_test(NAME maint_burn COMMAND test_maint_burn)
endif()
