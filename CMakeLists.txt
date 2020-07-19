cmake_minimum_required(VERSION 3.10)
project(_format_factory)

set(CMAKE_CXX_STANDARD 14)

add_executable(_format_factory main.cpp)


target_link_libraries(_format_factory avformat avcodec avutil)