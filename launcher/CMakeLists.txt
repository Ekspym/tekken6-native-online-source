cmake_minimum_required(VERSION 3.21)
project(t6_native_launcher LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# The official build's settings and assets (not in the public repository): launcher/official.cmake sets T6_SERVER
# ("tls://host:port") and T6_RELEASE_API (GitHub releases API url); official.qrc holds the pinned server certificate,
# the rank pictures and the art. Without them the launcher builds with placeholders.
include(${CMAKE_CURRENT_SOURCE_DIR}/official.cmake OPTIONAL)
set(T6_SOURCES
    src/characters.cpp
    src/common.cpp
    src/controls.cpp
    src/files.cpp
    src/http.cpp
    src/launcher.cpp
    src/launcher_build.cpp
    src/launcher_controls.cpp
    src/launcher_custom.cpp
    src/launcher_game.cpp
    src/launcher_match.cpp
    src/launcher_play.cpp
    src/launcher_practice.cpp
    src/launcher_profile.cpp
    src/launcher_server.cpp
    src/launcher_update.cpp
    src/main.cpp
    src/processes.cpp
    src/server.cpp
    src/style.cpp
    src/system.cpp
    src/upnp.cpp
    src/versions.cpp
    src/widgets.cpp
    launcher.qrc app.rc)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/official.qrc AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ranks AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/art)
  list(APPEND T6_SOURCES official.qrc)
endif()
add_executable(t6_native_launcher WIN32 ${T6_SOURCES})
if(T6_SERVER)
  target_compile_definitions(t6_native_launcher PRIVATE "T6_SERVER=\"${T6_SERVER}\"")
endif()
if(T6_RELEASE_API)
  target_compile_definitions(t6_native_launcher PRIVATE "T6_RELEASE_API=L\"${T6_RELEASE_API}\"")
endif()
# -DT6_DEV=ON: the developer's own launcher (statistics tab; build it in launcher\build_dev, it is never packaged)
option(T6_DEV "developer launcher with the statistics tab" OFF)
if(T6_DEV)
  target_compile_definitions(t6_native_launcher PRIVATE T6_DEV)
  set_target_properties(t6_native_launcher PROPERTIES OUTPUT_NAME "Tekken6 Native Online Dev")
else()
  set_target_properties(t6_native_launcher PROPERTIES OUTPUT_NAME "Tekken6 Native Online")
endif()
target_compile_options(t6_native_launcher PRIVATE /utf-8 /W3)
target_link_libraries(t6_native_launcher PRIVATE Qt6::Widgets Qt6::Network dwmapi winhttp dxgi winmm dbghelp ws2_32)
target_compile_options(t6_native_launcher PRIVATE /Zi)
target_link_options(t6_native_launcher PRIVATE /DEBUG /OPT:REF /OPT:ICF)
