cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BRICK_VERSION LIMIT_COUNT 1)
project(brick_framework VERSION ${BRICK_VERSION} LANGUAGES CXX)

option(BRICK_BUILD_TESTS "Build BRICK PC tests" ON)

add_library(brick_interfaces INTERFACE)
add_library(BRICK::Interfaces ALIAS brick_interfaces)
target_include_directories(brick_interfaces INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/interfaces/include")

add_library(brick_core STATIC
    libs/core/src/AudioBuffer.cpp
    libs/core/src/AssetBundle.cpp
    libs/core/src/AssetStreamer.cpp
    libs/core/src/BmpDecoder.cpp
    libs/core/src/PeriodicAudioPlayer.cpp
    libs/core/src/PeriodicTimer.cpp
    libs/core/src/TouchMapper.cpp
    libs/core/src/WavDecoder.cpp)
add_library(BRICK::Core ALIAS brick_core)
target_include_directories(brick_core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/core/include")
target_include_directories(brick_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/interfaces/include")
target_link_libraries(brick_core PUBLIC BRICK::Interfaces)

add_library(brick_platform_esp32 INTERFACE)
add_library(BRICK::PlatformESP32 ALIAS brick_platform_esp32)
target_include_directories(brick_platform_esp32 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/platforms/esp32/include")
target_link_libraries(brick_platform_esp32 INTERFACE BRICK::Core)

add_library(brick_platform_stm32 INTERFACE)
add_library(BRICK::PlatformSTM32 ALIAS brick_platform_stm32)
target_include_directories(brick_platform_stm32 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/platforms/stm32/include")
target_link_libraries(brick_platform_stm32 INTERFACE BRICK::Core)

if(BRICK_BUILD_TESTS)
  enable_testing()
  add_executable(brick_core_tests tests/brick/brick_core_tests.cpp)
  target_compile_features(brick_core_tests PRIVATE cxx_std_17)
  target_compile_options(brick_core_tests PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(brick_core_tests PRIVATE BRICK::Core)
  add_test(NAME brick_core_tests COMMAND brick_core_tests)
endif()
