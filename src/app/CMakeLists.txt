# Qt 应用层（M3）：QML UI + 传输适配。核心逻辑在 massage_core（零 Qt 依赖）。
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS SerialPort QUIET)

add_executable(massage_app
  main.cpp
  app_config.cpp
  head_item.cpp
  app_core.cpp
  mock_bus.cpp
  serial_transport.cpp
  ${CMAKE_SOURCE_DIR}/qml/qml.qrc
)

target_include_directories(massage_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(massage_app PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(massage_app PRIVATE
  massage_core
  massage_audit
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Gui
  Qt${QT_VERSION_MAJOR}::Quick
  Qt${QT_VERSION_MAJOR}::QuickControls2
)

if(TARGET Qt${QT_VERSION_MAJOR}::SerialPort)
  target_link_libraries(massage_app PRIVATE Qt${QT_VERSION_MAJOR}::SerialPort)
  target_compile_definitions(massage_app PRIVATE APP_HAS_SERIALPORT)
  message(STATUS "Qt SerialPort: 已安装 —— 真实总线可用（--serial <port>）")
else()
  message(STATUS "Qt SerialPort: 未安装 —— 仅模拟总线（维护工具勾选 Qt Serial Port 后重新配置）")
endif()
