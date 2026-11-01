cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BRICK_VERSION LIMIT_COUNT 1)
project(brick_framework VERSION ${BRICK_VERSION} LANGUAGES CXX)

# Tests default to ON only when BRICK is the top-level project, not when an
# application pulls it in with add_subdirectory().
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(BRICK_TOP_LEVEL ON)
else()
  set(BRICK_TOP_LEVEL OFF)
endif()
option(BRICK_BUILD_TESTS "Build BRICK PC tests" ${BRICK_TOP_LEVEL})
option(BRICK_BUILD_PC "Build the PC platform and simulator" ON)
option(BRICK_PC_SDL2 "Use SDL2 for the simulator window when it is available" ON)

add_library(brick_interfaces INTERFACE)
add_library(BRICK::Interfaces ALIAS brick_interfaces)
target_include_directories(brick_interfaces INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/interfaces/include")

add_library(brick_core STATIC
    libs/core/src/AudioBuffer.cpp
    libs/core/src/AssetBundle.cpp
    libs/core/src/AssetStreamer.cpp
    libs/core/src/BmpDecoder.cpp
    libs/core/src/DragSlider.cpp
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

add_library(brick_boards INTERFACE)
add_library(BRICK::Boards ALIAS brick_boards)
target_include_directories(brick_boards INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/include")
target_link_libraries(brick_boards INTERFACE BRICK::Interfaces)

# LVGL adapters. Their sources are compiled into the consumer, which must also
# link an LVGL v9 target.
add_library(brick_lvgl INTERFACE)
add_library(BRICK::Lvgl ALIAS brick_lvgl)
target_include_directories(brick_lvgl INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl/include")
target_sources(brick_lvgl INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl/src/LvglDisplayAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl/src/LvglImagePresenter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/lvgl/src/LvglTouchAdapter.cpp")
target_link_libraries(brick_lvgl INTERFACE BRICK::Interfaces)

add_library(brick_mocks INTERFACE)
add_library(BRICK::Mocks ALIAS brick_mocks)
target_include_directories(brick_mocks INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/mocks/include")
target_link_libraries(brick_mocks INTERFACE BRICK::Interfaces)

if(BRICK_BUILD_PC)
  add_library(brick_platform_pc STATIC
      platforms/pc/src/PcFileSystem.cpp
      platforms/pc/src/PcFrameBufferDisplay.cpp
      platforms/pc/src/Simulator.cpp)
  add_library(BRICK::PlatformPC ALIAS brick_platform_pc)
  target_compile_features(brick_platform_pc PUBLIC cxx_std_17)
  target_compile_options(brick_platform_pc PRIVATE -Wall -Wextra -Werror)
  # Boards are included so that brick/boards/pc/SimulatorBoard.h resolves.
  target_include_directories(brick_platform_pc PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/platforms/pc/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/boards/include")
  target_link_libraries(brick_platform_pc PUBLIC BRICK::Core)
  set(BRICK_PC_HAS_SDL2 0)
  if(BRICK_PC_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
      target_link_libraries(brick_platform_pc PRIVATE SDL2::SDL2)
      # MinGW SDL2 exports the toolchain include directory; as -isystem it
      # breaks #include_next in libstdc++.
      set_target_properties(brick_platform_pc PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
      set(BRICK_PC_HAS_SDL2 1)
    endif()
  endif()
  target_compile_definitions(brick_platform_pc PRIVATE BRICK_PC_HAS_SDL2=${BRICK_PC_HAS_SDL2})
  if(BRICK_PC_HAS_SDL2)
    message(STATUS "BRICK PC simulator: SDL2 window enabled")
  else()
    message(STATUS "BRICK PC simulator: SDL2 not found, headless only")
  endif()
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BrickSimulator.cmake")
endif()

if(BRICK_BUILD_TESTS)
  enable_testing()
  add_executable(brick_core_tests tests/brick/brick_core_tests.cpp)
  target_compile_features(brick_core_tests PRIVATE cxx_std_17)
  target_compile_options(brick_core_tests PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(brick_core_tests PRIVATE BRICK::Core BRICK::Mocks)
  add_test(NAME brick_core_tests COMMAND brick_core_tests)
  if(BRICK_BUILD_PC)
    brick_add_simulator(brick_pc_tests tests/pc/pc_platform_tests.cpp)
    target_compile_options(brick_pc_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME brick_pc_tests COMMAND brick_pc_tests WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()
