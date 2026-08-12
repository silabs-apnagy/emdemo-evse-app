# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(matter_thread_soc_evse_app_series_2_freertos)

include(ExternalProject)

ExternalProject_Add(matter_thread_soc_evse_app_freertos
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../matter_thread_soc_evse_app_freertos/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../matter_thread_soc_evse_app_freertos/cmake_gcc/build
  CMAKE_ARGS --preset project
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../matter_thread_soc_evse_app_freertos/cmake_gcc
)
ExternalProject_Add(matter_bootloader_external_series_2
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../matter_bootloader_external_series_2/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../matter_bootloader_external_series_2/cmake_gcc/build
  CMAKE_ARGS --preset project
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../matter_bootloader_external_series_2/cmake_gcc
)

ExternalProject_Add_StepDependencies(matter_bootloader_external_series_2 build matter_thread_soc_evse_app_freertos)


set(post_build_command ${POST_BUILD_EXE} postbuild "./matter_thread_soc_evse_app_series_2_freertos.slpb")


add_custom_target(postbuild ALL
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)

add_dependencies(postbuild matter_thread_soc_evse_app_freertos matter_bootloader_external_series_2)

# BEGIN_SIMPLICITY_STUDIO_METADATA=eJy9U99v0zAQ/leiaELbA3E3JB5KYEIrD5GSFZFWAqmS5TrX1uD4jM9pOk387xzrmg4BRbzweJfvvh/ny31aF9X7srgpZp9kPZtPiqmsp+V8VkxvZTWdvCvTcZpf71qbbCGQQfd6kV5mo0WagNPYGLfmxtv6pigW6fWbhcurGm0XGUgJTzka78gwZBOjHwvR933Wv8gwrMXVaHQpPlZlrTfQqufGUVROAzPzxNjhrWqBvNKwB5SoVdzrP+EiY9WSMo2tIBIH6Qw0BmZiP0mS02P3oeL6gKqw6Sx8gBUEzgKUmIbJyeqsx/DlQTprVFSLVAyjk2oCK+PMj/k/4x/hPOCR4rIztqkj+KHNH47Ns3N23yrXQLhIBnzy7GuH8dXZ+cH9hWhVjBBk3ARQjSTUErYEUnkvCYIBkldyFQBCRMrI+uWeIhe/GsjF743lPuBn0DFxvHwOd0LyoMTvZY9PI+ZshYTyTq3vRG1ab4028a6OXWNQbF/KYVf/lOcU+OhE/C3IEjFaVLxqCTtuOGUHrf8b5LST4eDETxe3v2fx5KBzcfzfuEy/fQfzfGCt=END_SIMPLICITY_STUDIO_METADATA