cmake_minimum_required(VERSION 3.13)
project(2GIG-345-Decoder)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PkgConfig
find_package(PkgConfig REQUIRED)
# Mosquittopp
pkg_check_modules(Mosquitto IMPORTED_TARGET libmosquittopp REQUIRED)
# RTL-SDR
pkg_check_modules(RTL-SDR IMPORTED_TARGET librtlsdr REQUIRED)
# YAML-CPP
find_package(yaml-cpp REQUIRED)
# spdlog
find_package(spdlog REQUIRED)

include_directories(${YAML_CPP_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}
	src/main.cpp
	src/analog_decoder.cpp
	src/analog_decoder.h
	src/digital_decoder.cpp
	src/digital_decoder.h
	src/mqtt.cpp
	src/mqtt.h
)

target_link_libraries(${PROJECT_NAME} PkgConfig::Mosquitto PkgConfig::RTL-SDR ${YAML_CPP_LIBRARIES} spdlog::spdlog)

# Tests
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(tests
        tests/test_digital_decoder.cpp
        tests/test_analog_decoder.cpp
        src/digital_decoder.cpp
        src/analog_decoder.cpp
    )
    target_include_directories(tests PRIVATE src tests)
    target_link_libraries(tests GTest::gtest_main spdlog::spdlog)
    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
