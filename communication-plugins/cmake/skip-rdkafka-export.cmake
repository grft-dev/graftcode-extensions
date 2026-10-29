# Drop librdkafka's install(EXPORT). It pulls zlibstatic into an export set
# this plugin does not ship. Run from the librdkafka source root.
file(READ "CMakeLists.txt" _graftcode_text)
string(REPLACE
  "install(
    EXPORT \"\${targets_export_name}\"
    NAMESPACE \"\${namespace}\"
    DESTINATION \"\${config_install_dir}\"
)"
  ""
  _graftcode_text "${_graftcode_text}")
file(WRITE "CMakeLists.txt" "${_graftcode_text}")

foreach(_graftcode_list IN ITEMS "src/CMakeLists.txt" "src-cpp/CMakeLists.txt")
  file(READ "${_graftcode_list}" _graftcode_text)
  string(REPLACE "    EXPORT \"\${targets_export_name}\"\n" "" _graftcode_text "${_graftcode_text}")
  file(WRITE "${_graftcode_list}" "${_graftcode_text}")
endforeach()
