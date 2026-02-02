cmake_minimum_required(VERSION 3.21)
project(Concentrate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SPDLOG_BUILD_SHARED OFF)
# ╭──────────────────────────────────────╮
# │                 CPM                  │
# ╰──────────────────────────────────────╯
set(CPM_FILE ${CMAKE_BINARY_DIR}/CPM.cmake)
set(CPM_VERSION "0.42.0")
if(NOT EXISTS "${CPM_FILE}")
    file(DOWNLOAD "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake" ${CPM_FILE})
endif()
include(${CPM_FILE})

# ╭──────────────────────────────────────╮
# │             Dependencies             │
# ╰──────────────────────────────────────╯
cpmaddpackage("gh:yhirose/cpp-httplib@0.30.1")
cpmaddpackage("gh:nlohmann/json@3.11.3")
cpmaddpackage("gh:gabime/spdlog@1.17.0")

find_package(SQLite3 REQUIRED)
pkg_check_modules(PC_DBUS REQUIRED dbus-1)

# ╭──────────────────────────────────────╮
# │            get libsecret             │
# ╰──────────────────────────────────────╯
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_LIBSECRET REQUIRED libsecret-1)
include_directories(${PC_LIBSECRET_INCLUDE_DIRS})
link_directories(${PC_LIBSECRET_LIBRARY_DIRS})
add_definitions(${PC_LIBSECRET_CFLAGS_OTHER})
add_definitions("-DCPPHTTPLIB_THREAD_POOL_COUNT=2")

# ╭──────────────────────────────────────╮
# │       Concentrate executable        │
# ╰──────────────────────────────────────╯
add_executable(
    Concentrate
    src/main.cpp
    src/Concentrate.cpp
    src/secrets.cpp
    src/notification.cpp
    src/window.cpp
    src/anytype.cpp
    src/hydration.cpp
    src/json.cpp
    src/sqlite.cpp)

add_custom_target(
    webfiles
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/index.html $<TARGET_FILE_DIR:Concentrate>/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/app.js $<TARGET_FILE_DIR:Concentrate>/app.js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/favicon.svg $<TARGET_FILE_DIR:Concentrate>/favicon.svg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/style.css $<TARGET_FILE_DIR:Concentrate>/style.css
    COMMENT "Copying webfiles")

add_dependencies(Concentrate webfiles)

target_include_directories(Concentrate PRIVATE ${PC_DBUS_INCLUDE_DIRS})
target_link_directories(Concentrate PRIVATE ${PC_DBUS_LIBRARY_DIRS})
target_compile_definitions(Concentrate PRIVATE ${PC_DBUS_CFLAGS_OTHER})

target_link_libraries(Concentrate PRIVATE httplib::httplib nlohmann_json::nlohmann_json SQLite::SQLite3 ${PC_LIBSECRET_LIBRARIES} ${PC_DBUS_LIBRARIES} spdlog)
target_compile_options(Concentrate PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
set_target_properties(Concentrate PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

install(TARGETS Concentrate RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/index.html ${CMAKE_SOURCE_DIR}/app.js ${CMAKE_SOURCE_DIR}/favicon.svg ${CMAKE_SOURCE_DIR}/style.css DESTINATION share/Concentrate)
