# brick_add_simulator(<target> <sources>...)
#
# Builds a PC simulator executable linked against BRICK::PlatformPC. On
# Windows the result runs without any particular PATH: the MinGW runtime is
# linked statically and SDL2.dll is copied next to the executable.
function(brick_add_simulator target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE BRICK::PlatformPC)
  if(MINGW)
    target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
  endif()
  if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${target}>
      VERBATIM)
  endif()
endfunction()
