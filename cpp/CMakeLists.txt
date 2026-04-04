cmake_minimum_required(VERSION 3.20)
project(range_pricer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static linking
set(BUILD_SHARED_LIBS OFF)
if(NOT APPLE)
    # GCC/Linux: statically link libgcc and libstdc++
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# Release build flags
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# FlatBuffers (header-only — vendored at include/flatbuffers, v25.12.19)
add_library(flatbuffers_headers INTERFACE)
target_include_directories(flatbuffers_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ZeroMQ — system library + cppzmq header vendored at include/zmq.hpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

add_library(cppzmq INTERFACE)
target_include_directories(cppzmq INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
)
target_link_directories(cppzmq INTERFACE ${ZMQ_LIBRARY_DIRS})
target_link_libraries(cppzmq INTERFACE ${ZMQ_LIBRARIES})

# Pricing library
add_library(range_pricer_lib STATIC
    src/range_pricer.cpp
)

target_include_directories(range_pricer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/generated
)

target_link_libraries(range_pricer_lib PUBLIC flatbuffers_headers)

find_package(Threads REQUIRED)

# Server — multi-process / multi-thread ZMQ ROUTER-DEALER broker
add_executable(range_pricer_server
    src/main.cpp
    src/worker.cpp
    src/config.cpp
    src/logger.cpp
)
target_link_libraries(range_pricer_server PRIVATE range_pricer_lib cppzmq Threads::Threads)

# Client — CLI utility to send a PricingRequest and print the PV
add_executable(range_pricer_client
    src/client.cpp
    src/logger.cpp
)
target_link_libraries(range_pricer_client PRIVATE range_pricer_lib cppzmq)
