# //! 本地验证程序；不加入青简生产构建。
# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.20)
project(fcitx_popup_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
set(FCITX_SOURCE "" CACHE PATH "Patched Fcitx source")
set(FCITX_BUILD "" CACHE PATH "Fcitx build")
set(FCITX_PREFIX "" CACHE PATH "Installed Fcitx headers")
foreach(v FCITX_SOURCE FCITX_BUILD FCITX_PREFIX)
  if(NOT ${v})
    message(FATAL_ERROR "${v} is required")
  endif()
endforeach()
find_package(Fcitx5ModuleWaylandIM CONFIG REQUIRED
  PATHS "${FCITX_PREFIX}/lib/cmake/Fcitx5ModuleWaylandIM" NO_DEFAULT_PATH)
if(EXISTS "${FCITX_PREFIX}/include/Fcitx5/Module/fcitx-module/waylandim/waylandim_public.h")
  message(FATAL_ERROR "SDK must not install the private wrapper interface")
endif()
set(PROTOCOL_DIR "${FCITX_SOURCE}/src/lib/fcitx-wayland/input-method-v2")
foreach(pair "im;input-method-unstable-v2" "vk;virtual-keyboard-unstable-v1")
  list(GET pair 0 stem)
  list(GET pair 1 protocol)
  add_custom_command(OUTPUT ${stem}-server.h ${stem}-protocol.c
    COMMAND wayland-scanner server-header ${PROTOCOL_DIR}/${protocol}.xml ${stem}-server.h
    COMMAND wayland-scanner private-code ${PROTOCOL_DIR}/${protocol}.xml ${stem}-protocol.c
    DEPENDS ${PROTOCOL_DIR}/${protocol}.xml)
endforeach()
add_executable(popup-lifecycle lifecycle.cpp compositor.cpp virtual.cpp
  ${FCITX_SOURCE}/src/frontend/waylandim/virtualinputcontext.cpp
  im-protocol.c vk-protocol.c)
target_include_directories(popup-lifecycle PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${FCITX_PREFIX}/include/Fcitx5/Core ${FCITX_PREFIX}/include/Fcitx5/Config
  ${FCITX_PREFIX}/include/Fcitx5/Utils
  ${FCITX_PREFIX}/include/Fcitx5/Module/fcitx-module/waylandim
  ${FCITX_PREFIX}/include/Fcitx5/Module/fcitx-module/wayland
  ${FCITX_SOURCE}/src/frontend/waylandim ${FCITX_SOURCE}/src/lib ${FCITX_BUILD})
target_link_libraries(popup-lifecycle PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_SERVER
  ${FCITX_BUILD}/bin/libFcitx5Core.so ${FCITX_BUILD}/bin/libFcitx5Config.so
  ${FCITX_BUILD}/bin/libFcitx5Utils.so)
target_compile_options(popup-lifecycle PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_compile_definitions(popup-lifecycle PRIVATE FCITX_BUILD="${FCITX_BUILD}" TEST_DATA="${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data/addon)

configure_file(${FCITX_BUILD}/src/modules/wayland/wayland.conf.in ${CMAKE_CURRENT_BINARY_DIR}/data/addon/wayland.conf COPYONLY)
configure_file(${FCITX_BUILD}/src/frontend/waylandim/waylandim.conf.in ${CMAKE_CURRENT_BINARY_DIR}/data/addon/waylandim.conf COPYONLY)
add_executable(popup-consumer consumer.cpp)
target_include_directories(popup-consumer PRIVATE
  ${FCITX_PREFIX}/include/Fcitx5/Core ${FCITX_PREFIX}/include/Fcitx5/Config
  ${FCITX_PREFIX}/include/Fcitx5/Utils)
target_link_libraries(popup-consumer PRIVATE PkgConfig::WAYLAND_CLIENT Fcitx5::Module::WaylandIM
  ${FCITX_BUILD}/bin/libFcitx5Core.so ${FCITX_BUILD}/bin/libFcitx5Config.so ${FCITX_BUILD}/bin/libFcitx5Utils.so)
target_compile_options(popup-consumer PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME popup-consumer COMMAND popup-consumer)
foreach(mode lifecycle disconnect unload global missing)
  add_test(NAME popup-${mode} COMMAND popup-lifecycle ${mode})
  set_tests_properties(popup-${mode} PROPERTIES TIMEOUT 20)
endforeach()
