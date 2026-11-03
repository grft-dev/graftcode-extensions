# Install one plugin as a flat set of runtime shared libraries.
#
# The plugin binary is installed without its import library or static archives.
# install(RUNTIME_DEPENDENCY_SET) then scans that binary and installs every
# non-system shared library it imports: vcpkg DLLs, OpenSSL from the runner
# image or Homebrew, librdkafka's libsasl/libz, the AWS SDK, and so on.
# The C/C++ runtime and Windows system DLLs stay on the host.
#
# A library is included only when the scan finds it. Windows Pub/Sub links
# Schannel curl plus OpenSSL::Crypto, so libssl is not one of its imports.
# Kafka's librdkafka does import libssl and libcrypto when it finds OpenSSL.
#
# Multi-config generators (Visual Studio) honor `cmake --install --config Release`.
# Single-config generators (Ninja, Makefiles) use CMAKE_BUILD_TYPE, which CI sets
# to Release. The install component is `plugin-runtime` so other install()
# rules in the same build (FetchContent headers, static libraries) are not
# packaged.

# Directories that hold OpenSSL (and similarly installed) shared libraries.
# FindOpenSSL's results live in the cache as LIB_EAY / SSL_EAY, often as an
# import library several levels below the bin or lib directory that contains
# the DLL or .so/.dylib the loader opens.
function(_graftcode_runtime_search_dirs out_var)
  set(dirs ${${out_var}})
  set(library_vars LIB_EAY LIB_EAY_RELEASE LIB_EAY_DEBUG SSL_EAY SSL_EAY_RELEASE SSL_EAY_DEBUG)
  foreach(cache_name IN LISTS library_vars)
    if(DEFINED CACHE{${cache_name}} AND NOT "$CACHE{${cache_name}}" STREQUAL "")
      set(entry "$CACHE{${cache_name}}")
      if(EXISTS "${entry}")
        get_filename_component(dir "${entry}" DIRECTORY)
        # The import library and the DLL/dylib it stands for are often in the
        # same directory (MinGW, some vcpkg layouts).
        list(APPEND dirs "${dir}")
        foreach(_i RANGE 0 6)
          if(EXISTS "${dir}/bin")
            list(APPEND dirs "${dir}/bin")
          endif()
          if(EXISTS "${dir}/lib")
            list(APPEND dirs "${dir}/lib")
          endif()
          get_filename_component(parent "${dir}" DIRECTORY)
          if(parent STREQUAL dir)
            break()
          endif()
          set(dir "${parent}")
        endforeach()
      endif()
    endif()
  endforeach()
  foreach(candidate IN ITEMS
      "C:/Program Files/OpenSSL/bin"
      "C:/Program Files/OpenSSL-Win64/bin"
      "/opt/homebrew/opt/openssl@3/lib"
      "/opt/homebrew/opt/openssl/lib"
      "/usr/local/opt/openssl@3/lib"
      "/usr/local/opt/openssl/lib")
    if(EXISTS "${candidate}")
      list(APPEND dirs "${candidate}")
    endif()
  endforeach()
  if(dirs)
    list(REMOVE_DUPLICATES dirs)
  endif()
  set(${out_var} "${dirs}" PARENT_SCOPE)
endfunction()

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
  _graftcode_runtime_search_dirs(dep_dirs)

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
      # Platform C/C++ runtime. Matched against the SONAME, so libssl and
      # libcrypto under /usr/lib or Homebrew are still installed.
      [[libc\.so.*]]
      [[libm\.so.*]]
      [[libdl\.so.*]]
      [[libpthread\.so.*]]
      [[librt\.so.*]]
      [[libgcc_s\.so.*]]
      [[libstdc\+\+\.so.*]]
      [[ld-linux.*]]
      [[linux-vdso.*]]
      [[libSystem\..*]]
      [[libc\+\+\..*]]
      [[libc\+\+abi\..*]]
    POST_EXCLUDE_REGEXES
      # Windows system and toolchain directories. vcpkg and OpenSSL copies
      # live elsewhere and must be packaged.
      [[.*[Ss]ystem32[/\\].*]]
      [[.*[Ss]ys[Ww][Oo][Ww]64[/\\].*]]
      [[.*[Ww]in[Ss]x[Ss][/\\].*]]
      [[^[A-Za-z]:[/\\][Ww]indows[/\\].*]]
      [[.*[Ww]indows [Kk]its[/\\].*]]
      [[.*Microsoft Visual Studio[/\\].*]]
      [[^/System/.*]]
    RUNTIME DESTINATION .
      COMPONENT plugin-runtime
    LIBRARY DESTINATION .
      COMPONENT plugin-runtime
  )

  # CMake copies runtime dependencies with their original load paths.
  # Point each bundled ELF/Mach-O library at the flat directory so libssl
  # finds libcrypto beside it on a machine that does not have that OpenSSL.
  if(UNIX AND NOT APPLE)
    # file(RPATH_SET) only rewrites an existing ELF RPATH/RUNPATH. System
    # OpenSSL and libsasl have none, so patchelf has to add $ORIGIN.
    install(CODE [[
      find_program(_graftcode_patchelf patchelf)
      if(NOT _graftcode_patchelf)
        message(FATAL_ERROR "patchelf is required to set $ORIGIN on bundled shared libraries")
      endif()
      file(GLOB _graftcode_runtime_libs
        "${CMAKE_INSTALL_PREFIX}/*.so"
        "${CMAKE_INSTALL_PREFIX}/*.so.*")
      foreach(_graftcode_lib IN LISTS _graftcode_runtime_libs)
        if(IS_SYMLINK "${_graftcode_lib}")
          continue()
        endif()
        execute_process(
          COMMAND "${_graftcode_patchelf}" --set-rpath "\$ORIGIN" "${_graftcode_lib}"
          RESULT_VARIABLE _graftcode_rc
          ERROR_VARIABLE _graftcode_err
        )
        if(NOT _graftcode_rc EQUAL 0)
          message(FATAL_ERROR "patchelf --set-rpath failed for ${_graftcode_lib}: ${_graftcode_err}")
        endif()
      endforeach()
    ]] COMPONENT plugin-runtime)
  elseif(APPLE)
    install(CODE [[
      file(GLOB _graftcode_runtime_libs "${CMAKE_INSTALL_PREFIX}/*.dylib")
      set(_graftcode_bundled "")
      foreach(_graftcode_lib IN LISTS _graftcode_runtime_libs)
        cmake_path(GET _graftcode_lib FILENAME _graftcode_name)
        list(APPEND _graftcode_bundled "${_graftcode_name}")
      endforeach()
      foreach(_graftcode_lib IN LISTS _graftcode_runtime_libs)
        if(IS_SYMLINK "${_graftcode_lib}")
          continue()
        endif()
        cmake_path(GET _graftcode_lib FILENAME _graftcode_name)
        execute_process(
          COMMAND install_name_tool -id "@loader_path/${_graftcode_name}" "${_graftcode_lib}"
          RESULT_VARIABLE _graftcode_rc
          ERROR_VARIABLE _graftcode_err
        )
        if(NOT _graftcode_rc EQUAL 0)
          execute_process(COMMAND codesign --remove-signature "${_graftcode_lib}")
          execute_process(
            COMMAND install_name_tool -id "@loader_path/${_graftcode_name}" "${_graftcode_lib}"
            RESULT_VARIABLE _graftcode_rc
            ERROR_VARIABLE _graftcode_err
          )
        endif()
        if(NOT _graftcode_rc EQUAL 0)
          message(FATAL_ERROR "install_name_tool -id failed for ${_graftcode_lib}: ${_graftcode_err}")
        endif()
        execute_process(
          COMMAND otool -L "${_graftcode_lib}"
          OUTPUT_VARIABLE _graftcode_otool
          RESULT_VARIABLE _graftcode_rc
        )
        if(NOT _graftcode_rc EQUAL 0)
          message(FATAL_ERROR "otool -L failed for ${_graftcode_lib}")
        endif()
        string(REPLACE "\n" ";" _graftcode_lines "${_graftcode_otool}")
        foreach(_graftcode_line IN LISTS _graftcode_lines)
          string(STRIP "${_graftcode_line}" _graftcode_line)
          if(_graftcode_line MATCHES "^(.+) \\(compatibility version")
            set(_graftcode_dep "${CMAKE_MATCH_1}")
            cmake_path(GET _graftcode_dep FILENAME _graftcode_dep_name)
            list(FIND _graftcode_bundled "${_graftcode_dep_name}" _graftcode_found)
            if(NOT _graftcode_found EQUAL -1 AND NOT _graftcode_dep STREQUAL "@loader_path/${_graftcode_dep_name}")
              execute_process(
                COMMAND install_name_tool -change "${_graftcode_dep}" "@loader_path/${_graftcode_dep_name}" "${_graftcode_lib}"
                RESULT_VARIABLE _graftcode_rc
                ERROR_VARIABLE _graftcode_err
              )
              if(NOT _graftcode_rc EQUAL 0)
                execute_process(COMMAND codesign --remove-signature "${_graftcode_lib}")
                execute_process(
                  COMMAND install_name_tool -change "${_graftcode_dep}" "@loader_path/${_graftcode_dep_name}" "${_graftcode_lib}"
                  RESULT_VARIABLE _graftcode_rc
                  ERROR_VARIABLE _graftcode_err
                )
              endif()
              if(NOT _graftcode_rc EQUAL 0)
                message(FATAL_ERROR "install_name_tool -change failed for ${_graftcode_lib}: ${_graftcode_err}")
              endif()
            endif()
          endif()
        endforeach()
        execute_process(
          COMMAND codesign --force --sign - "${_graftcode_lib}"
          RESULT_VARIABLE _graftcode_rc
          ERROR_VARIABLE _graftcode_err
        )
        if(NOT _graftcode_rc EQUAL 0)
          message(FATAL_ERROR "codesign failed for ${_graftcode_lib}: ${_graftcode_err}")
        endif()
      endforeach()
    ]] COMPONENT plugin-runtime)
  endif()
endfunction()
