cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OM_VERSION LIMIT_COUNT 1)
project(ObjectManager VERSION ${OM_VERSION} LANGUAGES CXX RC)
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ObjectManager WIN32
    src/manager.cpp
    src/app.manifest
    src/manager.rc
)

target_compile_definitions(ObjectManager PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(ObjectManager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(MSVC)
    target_compile_options(ObjectManager PRIVATE /W4 /utf-8 /permissive-)
    set_property(TARGET ObjectManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(ObjectManager PRIVATE user32 gdi32 comctl32 shell32 dwmapi)
