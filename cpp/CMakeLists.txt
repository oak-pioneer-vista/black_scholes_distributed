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

# Generate C++ header from FlatBuffers schema.
# Re-runs flatc when the .fbs file changes; all targets that link
# range_pricer_lib see the generated header as a build dependency.
find_program(FLATC flatc REQUIRED)
set(FBS_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/../schemas/pricing_request.fbs)
set(FBS_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/include/generated/pricing_request_generated.h)
add_custom_command(
    OUTPUT  ${FBS_GENERATED}
    COMMAND ${FLATC} --cpp -o ${CMAKE_CURRENT_SOURCE_DIR}/include/generated ${FBS_SCHEMA}
    DEPENDS ${FBS_SCHEMA}
    COMMENT "Generating FlatBuffers C++ header"
)
add_custom_target(flatbuffers_gen DEPENDS ${FBS_GENERATED})

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
target_include_directories(range_pricer_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(range_pricer_lib PUBLIC flatbuffers_headers hwy::hwy hwy::hwy_contrib)
add_dependencies(range_pricer_lib flatbuffers_gen)

find_package(Threads REQUIRED)

# Google Highway — portable SIMD
find_package(hwy REQUIRED)

# gflags — command-line flag parsing
find_package(gflags REQUIRED)

# Server — multi-process / multi-thread ZMQ ROUTER-DEALER broker
add_executable(range_pricer_server
    src/main.cpp
    src/worker.cpp
    src/config.cpp
    src/logger.cpp
)
target_link_libraries(range_pricer_server PRIVATE range_pricer_lib cppzmq Threads::Threads gflags)

# Client — CLI utility to send a PricingRequest and print the PV
add_executable(range_pricer_client
    src/client.cpp
    src/logger.cpp
)
target_link_libraries(range_pricer_client PRIVATE range_pricer_lib cppzmq gflags)

# Tests
enable_testing()
find_package(GTest REQUIRED HINTS /opt/homebrew)

add_executable(range_pricer_test test/range_pricer_test.cpp)
target_link_libraries(range_pricer_test PRIVATE range_pricer_lib GTest::gtest_main)
add_test(NAME range_pricer_test COMMAND range_pricer_test)
