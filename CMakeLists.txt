cmake_minimum_required(VERSION 3.25)
project(myutils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NIRIUSE "Build niriuse executable" ON)

set(CPM_FILE ${CMAKE_BINARY_DIR}/CPM.cmake)
set(CPM_VERSION "0.42.0")
if(NOT EXISTS "${CPM_FILE}")
    file(DOWNLOAD
         "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake"
         ${CPM_FILE})
endif()
include(${CPM_FILE})

CPMAddPackage("gh:yhirose/cpp-httplib@0.30.1")
CPMAddPackage("gh:nlohmann/json@3.11.3")

find_package(SQLite3 REQUIRED)

if(NIRIUSE)
    add_executable(niriuse niriuse.cpp)
    target_compile_definitions(niriuse PRIVATE NIRI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(niriuse PRIVATE httplib::httplib nlohmann_json::nlohmann_json SQLite::SQLite3)
    install(TARGETS niriuse RUNTIME DESTINATION bin)
    install(TARGETS niriuse RUNTIME DESTINATION bin/niriuse/index.html)
endif()

