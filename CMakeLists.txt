cmake_minimum_required(VERSION 3.6)
project(shmqueue)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11")

set(SOURCE_FILES main.cpp
        shmmqueue.cpp
        shmmqueue.h
        shm_rwlock.h
        shm_rwlock.cpp)
add_executable(shmqueue ${SOURCE_FILES})
target_link_libraries(shmqueue pthread)
