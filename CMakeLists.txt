cmake_minimum_required(VERSION 3.15)

project(ExampleThreadPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/example/main.cpp
)

# ThreadPool.h 位于 include/ 目录，需要把头文件目录加入搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 让 MSVC 按 UTF-8 解析源码，解决 C4819 警告（中文注释/字符串）
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()
