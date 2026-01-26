cmake_minimum_required(VERSION 3.21)
project(focusservice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE
        Release
        CACHE STRING "" FORCE)
endif()

option(focusservice "Build focusservice executable" ON)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers in Debug" OFF)
option(ENABLE_PROFILING "Enable profiling with gprof" OFF)

# CPM
set(CPM_FILE ${CMAKE_BINARY_DIR}/CPM.cmake)
set(CPM_VERSION "0.42.0")
if(NOT EXISTS "${CPM_FILE}")
    file(DOWNLOAD "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake" ${CPM_FILE})
endif()

include(${CPM_FILE})

# Dependencies
cpmaddpackage("gh:yhirose/cpp-httplib@0.30.1")
cpmaddpackage("gh:nlohmann/json@3.11.3")
find_package(SQLite3 REQUIRED)
pkg_check_modules(PC_DBUS REQUIRED dbus-1)

# Global warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)

if(WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
endif()

# IPO / LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Target
if(focusservice)
    # get libsecret
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PC_LIBSECRET REQUIRED libsecret-1)
    include_directories(${PC_LIBSECRET_INCLUDE_DIRS})
    link_directories(${PC_LIBSECRET_LIBRARY_DIRS})
    add_definitions(${PC_LIBSECRET_CFLAGS_OTHER})
    add_definitions("-DCPPHTTPLIB_THREAD_POOL_COUNT=1")

    # focusservice executable
    add_executable(
        focusservice
        src/main.cpp
        src/focusservice.cpp
        src/secrets.cpp
        src/notification.cpp
        src/window.cpp
        src/anytype.cpp
        src/sqlite.cpp
        # src/window_focus.cpp
        # src/focus_rules.cpp
        # src/sqlite_store.cpp
        # src/http_server.cpp
        # src/notification.cpp
    )

    add_custom_command(
        TARGET focusservice
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/index.html $<TARGET_FILE_DIR:focusservice>/index.html
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/app.js $<TARGET_FILE_DIR:focusservice>/app.js)

    target_include_directories(focusservice PRIVATE ${PC_DBUS_INCLUDE_DIRS})
    target_link_directories(focusservice PRIVATE ${PC_DBUS_LIBRARY_DIRS})
    target_compile_definitions(focusservice PRIVATE ${PC_DBUS_CFLAGS_OTHER})

    target_link_libraries(focusservice PRIVATE httplib::httplib nlohmann_json::nlohmann_json SQLite::SQLite3 ${PC_LIBSECRET_LIBRARIES} ${PC_DBUS_LIBRARIES})
    target_compile_options(focusservice PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
    set_target_properties(focusservice PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

    if(ENABLE_PROFILING)
        message(STATUS "Profiling enabled")
        target_compile_options(focusservice PRIVATE -pg)
        target_link_options(focusservice PRIVATE -pg)
        target_compile_options(focusservice PRIVATE -g -O2 -fno-omit-frame-pointer)

        # copy index.html and app.js to binary dir for profiling report generation
        add_custom_command(
            TARGET focusservice
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/index.html $<TARGET_FILE_DIR:focusservice>
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/app.js $<TARGET_FILE_DIR:focusservice>)
    elseif(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(focusservice PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(focusservice PRIVATE -fsanitize=address,undefined)
    endif()

    install(TARGETS focusservice RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_SOURCE_DIR}/index.html ${CMAKE_SOURCE_DIR}/app.js DESTINATION share/focusservice)
endif()
