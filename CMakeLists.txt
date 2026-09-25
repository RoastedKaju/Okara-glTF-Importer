cmake_minimum_required(VERSION 3.24)

project(gltf-importer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE "src/main.cpp" "src/cJSON.c")
target_include_directories(${PROJECT_NAME} PRIVATE "src")