cmake_minimum_required(VERSION 3.5)
project(KcpStudy)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.cpp kcp/ikcp.c)
add_executable(KcpStudy ${SOURCE_FILES})