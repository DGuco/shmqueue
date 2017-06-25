cmake_minimum_required(VERSION 3.6)
project(shmqueue)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.cpp
        messagequeue.cpp
        messagequeue.h)
add_executable(shmqueue ${SOURCE_FILES})