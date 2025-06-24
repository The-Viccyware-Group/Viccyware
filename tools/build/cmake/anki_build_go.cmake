function(anki_build_go)
  cmake_parse_arguments(ANKI "" "NAME;DIR;OUTPUT_NAME;BUILD_TAGS" "SRC_DIRS;INCLUDE_DIRS;LINK_FLAGS;PKG_CONFIG_PATHS;EXTRA_DEPENDS" ${ARGN})
  if(NOT ANKI_NAME OR NOT ANKI_DIR)
    message(FATAL_ERROR "anki_build_go: NAME and DIR are required")
  endif()

  set(output_name ${ANKI_OUTPUT_NAME})
  if(NOT output_name)
    set(output_name ${ANKI_NAME})
  endif()

  find_program(GO_EXECUTABLE NAMES go REQUIRED)
  find_program(UPX_EXECUTABLE NAMES upx)
  if(UPX_EXECUTABLE)
  else()
    message(WARNING "anki_build_go: upx not found, skipping")
  endif()

  get_filename_component(go_project_dir ${ANKI_DIR} ABSOLUTE)
  set(go_out ${CMAKE_BINARY_DIR}/${output_name})
  set(stamp_file ${go_out}.built)

  set(go_srcs "")
  set(go_build_paths "")
  if(ANKI_SRC_DIRS)
    foreach(d IN LISTS ANKI_SRC_DIRS)
      set(abs ${go_project_dir}/${d})
      if(IS_DIRECTORY ${abs})
        file(GLOB_RECURSE tmp ${abs}/*.go)
        list(APPEND go_srcs ${tmp})
        file(RELATIVE_PATH rel ${go_project_dir} ${abs})
        list(APPEND go_build_paths ./${rel})
      elseif(EXISTS ${abs})
        list(APPEND go_srcs ${abs})
        file(RELATIVE_PATH rel ${go_project_dir} ${abs})
        list(APPEND go_build_paths ${rel})
      else()
        message(FATAL_ERROR "anki_build_go: invalid SRC_DIRS: ${d}")
      endif()
    endforeach()
  else()
    file(GLOB_RECURSE go_srcs ${go_project_dir}/*.go)
    list(APPEND go_build_paths ./...)
  endif()

  set(go_env
    CGO_ENABLED=1
    GOOS=linux
    GOARCH=arm
    GOARM=7
    CC=${CMAKE_C_COMPILER}
    CXX=${CMAKE_CXX_COMPILER}
  )
  set(go_c_flags "")
  if(ANKI_INCLUDE_DIRS)
    foreach(i IN LISTS ANKI_INCLUDE_DIRS)
      string(APPEND go_c_flags "-I${i} ")
    endforeach()
  endif()
  #string(APPEND go_c_flags "-Wno-implicit-function-declaration ")
  list(APPEND go_env "CGO_CFLAGS=${go_c_flags}")
  if(ANKI_PKG_CONFIG_PATHS)
    list(JOIN ANKI_PKG_CONFIG_PATHS ":" pkg_paths_str)
    list(APPEND go_env "PKG_CONFIG_PATH=${pkg_paths_str}")
  endif()
  if(ANKI_LINK_FLAGS)
    string(REPLACE ";" " " link_str ${ANKI_LINK_FLAGS})
    list(APPEND go_env "CGO_LDFLAGS=${link_str}")
  endif()

  set(build_flags "")
  if(ANKI_BUILD_TAGS)
    list(APPEND build_flags "-tags" "${ANKI_BUILD_TAGS}")
  endif()
  list(APPEND build_flags "-ldflags" "-s -w")

  set(cmds
    COMMAND ${CMAKE_COMMAND} -E env ${go_env} ${GO_EXECUTABLE} mod download
    COMMAND ${CMAKE_COMMAND} -E env ${go_env} ${GO_EXECUTABLE} build -o ${go_out} ${build_flags} ${go_build_paths}
  )
  if(UPX_EXECUTABLE)
    list(APPEND cmds
      COMMAND ${CMAKE_SOURCE_DIR}/tools/build/tools/upx-if-packed.sh "${go_out}"
    )
  endif()
  list(APPEND cmds COMMAND ${CMAKE_COMMAND} -E touch ${stamp_file})

  add_custom_command(
    OUTPUT ${stamp_file}
    ${cmds}
    WORKING_DIRECTORY ${go_project_dir}
    DEPENDS ${go_srcs} ${go_project_dir}/go.mod ${go_project_dir}/go.sum ${ANKI_EXTRA_DEPENDS}
    COMMENT "Building Go target ${output_name}"
    VERBATIM
  )
  add_custom_target(${ANKI_NAME} ALL DEPENDS ${stamp_file})
  install(PROGRAMS ${go_out} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endfunction()