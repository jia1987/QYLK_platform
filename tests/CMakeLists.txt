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

# 审计引擎测试（M4a，纯 C++ 零 Qt —— Windows/WSL 双平台同待遇）
function(add_audit_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE massage_audit)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_audit_test(test_sha256)
add_audit_test(test_pin_guard)
add_audit_test(test_audit_log)

# 崩溃/断电完整性：测试需重新执行自身（子进程 _Exit 硬杀），argv[1] 注入完整路径
add_executable(test_audit_crash test_audit_crash.cpp)
target_link_libraries(test_audit_crash PRIVATE massage_audit)
target_include_directories(test_audit_crash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_audit_crash COMMAND test_audit_crash "$<TARGET_FILE:test_audit_crash>")

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
    massage_core massage_audit Qt${QT_VERSION_MAJOR}::Core)
  add_test(NAME maint_burn COMMAND test_maint_burn)

  # M4a 集成测试：完整治疗场次 ⇄ 审计 DB 逐条对账（DoD#2）
  add_executable(test_audit_flow
    test_audit_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/app/app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/app/head_item.cpp
    ${CMAKE_SOURCE_DIR}/src/app/app_config.cpp
    ${CMAKE_SOURCE_DIR}/src/app/mock_bus.cpp)
  target_include_directories(test_audit_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(test_audit_flow PRIVATE APP_VERSION="${PROJECT_VERSION}-test")
  target_link_libraries(test_audit_flow PRIVATE
    massage_core massage_audit Qt${QT_VERSION_MAJOR}::Core)
  add_test(NAME audit_flow COMMAND test_audit_flow)
endif()
