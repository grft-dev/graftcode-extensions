set(CMAKE_MIN 3.22)
cmake_minimum_required (VERSION ${CMAKE_MIN})
set(CMAKE_POLICY_VERSION_MINIMUM ${CMAKE_MIN})
cmake_policy(VERSION ${CMAKE_MIN})

set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/StaticPluginRuntime.cmake")
graftcode_prepare_static_runtime()

project ("RabbitmqPlugin" VERSION 1.0.0)

graftcode_apply_static_crt()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DUNICODE)
enable_testing()

set(BUILDTYPE ${CMAKE_BUILD_TYPE})

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory ("RabbitmqPlugin")
add_subdirectory ("RabbitmqPluginTest")

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/InstallPluginRuntime.cmake")
graftcode_install_plugin(RabbitmqPlugin)

