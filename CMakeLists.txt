cmake_minimum_required(VERSION 3.25)
project(myutils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NIRIUSE ON)

add_executable(niriuse niriuse.cpp)
