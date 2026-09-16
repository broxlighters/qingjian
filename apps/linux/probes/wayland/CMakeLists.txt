# //! 阶段 0 独立探针，不链接青简插件或 Fcitx 私有 Wayland 包装库。
cmake_minimum_required(VERSION 3.16)
project(qingjian-wayland-probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(FCITX REQUIRED IMPORTED_TARGET Fcitx5Utils>=5.1.9)
pkg_check_modules(PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(PROTOCOL_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(INPUT_METHOD_XML "${PROTOCOL_DIR}/unstable/input-method/input-method-unstable-v1.xml")
add_custom_command(OUTPUT input-method-client.h input-method.c
  COMMAND ${WAYLAND_SCANNER} client-header ${INPUT_METHOD_XML} input-method-client.h
  COMMAND ${WAYLAND_SCANNER} private-code ${INPUT_METHOD_XML} input-method.c
  DEPENDS ${INPUT_METHOD_XML})
add_executable(qingjian-wayland-probe main.cpp probe.cpp input-method.c)
target_include_directories(qingjian-wayland-probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(qingjian-wayland-probe PRIVATE PkgConfig::WAYLAND PkgConfig::FCITX)
target_compile_options(qingjian-wayland-probe PRIVATE -Wall -Wextra -Werror)
