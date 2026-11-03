# Static MSVC CRT and, on Windows, a matching static vcpkg triplet.
#
# Include this file after cmake_minimum_required() and call
# graftcode_prepare_static_runtime() before project(), then
# graftcode_apply_static_crt() after project().
#
# /MT links vcruntime and the C++ standard library into the plugin, so
# loading it does not require the VC++ Redistributable. The Universal CRT
# (ucrtbase.dll) stays a Windows system DLL.
#
# x64-windows-static (and the arm64/x86 equivalents) build vcpkg ports as
# static libraries with that same CRT. Linux and macOS vcpkg triplets are
# already static; glibc, libstdc++, and libSystem stay on the host.

macro(graftcode_prepare_static_runtime)
  # FetchContent projects often pin an older cmake_minimum_required, which
  # would otherwise leave CMP0091 unset and ignore CMAKE_MSVC_RUNTIME_LIBRARY.
  set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Static MSVC CRT (/MT, /MTd for Debug)" FORCE)

  if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(_graftcode_arch "x64")
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64|arm64"
        OR "$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "ARM64")
      set(_graftcode_arch "arm64")
    elseif(CMAKE_GENERATOR_PLATFORM MATCHES "Win32"
        OR "$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "X86")
      set(_graftcode_arch "x86")
    endif()
    set(VCPKG_TARGET_TRIPLET "${_graftcode_arch}-windows-static"
      CACHE STRING "Static vcpkg triplet matching /MT" FORCE)
    unset(_graftcode_arch)
  endif()
endmacro()

# project() fills CMAKE_<LANG>_FLAGS* with /MD. Rewrite those so dependencies
# that do not honor CMP0091 still compile against the static CRT.
macro(graftcode_apply_static_crt)
  if(MSVC)
    foreach(_graftcode_flag_var
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if(DEFINED ${_graftcode_flag_var})
        string(REPLACE "/MDd" "/MTd" ${_graftcode_flag_var} "${${_graftcode_flag_var}}")
        string(REPLACE "/MD" "/MT" ${_graftcode_flag_var} "${${_graftcode_flag_var}}")
      endif()
    endforeach()
    unset(_graftcode_flag_var)
  endif()
endmacro()
