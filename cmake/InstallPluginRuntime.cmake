# Install one plugin as a flat set of runtime shared libraries.
#
# Archive/import libraries (.lib, .exp, static .a) are not installed. System
# libraries are left on the host. Imported shared libraries (vcpkg DLLs on
# Windows) are installed next to the plugin because install(RUNTIME_DEPENDENCY_SET)
# scans the built binary. A DLL is included only when that scan finds it, so
# Windows Pub/Sub does not pick up libssl while curl is built with Schannel.
#
# Multi-config generators (Visual Studio) honor `cmake --install --config Release`.
# Single-config generators (Ninja, Makefiles) use CMAKE_BUILD_TYPE, which CI sets
# to Release. The install component is `plugin-runtime` so other install()
# rules in the same build (FetchContent headers, static libraries) are not
# packaged.

function(graftcode_install_plugin target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "graftcode_install_plugin: target '${target}' does not exist")
  endif()

  set(dep_dirs "$<TARGET_FILE_DIR:${target}>")
  if(DEFINED VCPKG_INSTALLED_DIR AND NOT "${VCPKG_INSTALLED_DIR}" STREQUAL "")
    # Manifest mode points VCPKG_INSTALLED_DIR at either the triplet root
    # (.../vcpkg_installed/x64-windows) or the parent (.../vcpkg_installed).
    # Search both layouts. Debug bins are used only for the Debug config.
    set(vcpkg_roots "${VCPKG_INSTALLED_DIR}")
    if(DEFINED VCPKG_TARGET_TRIPLET AND NOT "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
      list(APPEND vcpkg_roots "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    foreach(root IN LISTS vcpkg_roots)
      list(APPEND dep_dirs
        "$<IF:$<CONFIG:Debug>,${root}/debug/bin,${root}/bin>"
        "$<IF:$<CONFIG:Debug>,${root}/debug/lib,${root}/lib>"
      )
    endforeach()
  endif()

  # Flat install: consumers extract the archive next to the host executable.
  if(APPLE)
    set_property(TARGET "${target}" PROPERTY INSTALL_RPATH "@loader_path")
  elseif(UNIX)
    set_property(TARGET "${target}" PROPERTY INSTALL_RPATH "$ORIGIN")
  endif()

  set(runtime_set "graftcode_${target}_runtime")
  install(TARGETS "${target}"
    RUNTIME_DEPENDENCY_SET "${runtime_set}"
    RUNTIME DESTINATION .
      COMPONENT plugin-runtime
    LIBRARY DESTINATION .
      COMPONENT plugin-runtime
  )
  # Regex and DIRECTORIES arguments must precede the RUNTIME/LIBRARY
  # groups. CMake otherwise parses them as per-artifact options.
  install(RUNTIME_DEPENDENCY_SET "${runtime_set}"
    DIRECTORIES ${dep_dirs}
    PRE_EXCLUDE_REGEXES
      [[api-ms-win-.*]]
      [[ext-ms-.*]]
    POST_EXCLUDE_REGEXES
      # Windows system and toolchain directories. vcpkg copies are elsewhere.
      [[.*[Ss]ystem32[/\\].*]]
      [[.*[Ss]ys[Ww][Oo][Ww]64[/\\].*]]
      [[.*[Ww]in[Ss]x[Ss][/\\].*]]
      [[^[A-Za-z]:[/\\][Ww]indows[/\\].*]]
      [[.*[Ww]indows [Kk]its[/\\].*]]
      [[.*Microsoft Visual Studio[/\\].*]]
      # Linux and macOS system loaders. Homebrew is the kafka macOS OpenSSL.
      [[^/lib/.*]]
      [[^/lib64/.*]]
      [[^/usr/lib/.*]]
      [[^/usr/lib64/.*]]
      [[^/usr/local/.*]]
      [[^/System/.*]]
      [[^/opt/homebrew/.*]]
    RUNTIME DESTINATION .
      COMPONENT plugin-runtime
    LIBRARY DESTINATION .
      COMPONENT plugin-runtime
  )
endfunction()
