set(PICOVOICE_HOME "${ANKI_THIRD_PARTY_DIR}/picovoice")
set(PICOVOICE_INCLUDE_PATH "${PICOVOICE_HOME}/vicos/include")

if (VICOS)
    set(PICOVOICE_LIB_PATH "${PICOVOICE_HOME}/vicos/lib")
endif()

# not actually porcupine. actually an interface program for a server program

set(PICOVOICE_LIBS
  pv_porcupine_interface
)

foreach(LIB ${PICOVOICE_LIBS})
  add_library(${LIB} STATIC IMPORTED GLOBAL)
  set_target_properties(${LIB} PROPERTIES
    IMPORTED_LOCATION
    "${PICOVOICE_LIB_PATH}/lib${LIB}.a"
    INTERFACE_INCLUDE_DIRECTORIES
    "${PICOVOICE_INCLUDE_PATH}")
  anki_build_target_license(${LIB} "Apache-2.0,${CMAKE_SOURCE_DIR}/licenses/picovoice.license")
endforeach()

set(PICOVOICE_SERVER_BIN "${PICOVOICE_HOME}/vicos/bin/pv_server")
get_filename_component(PV_SERVER_NAME "${PICOVOICE_SERVER_BIN}" NAME)
set(PV_SERVER_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${PV_SERVER_NAME}")

add_custom_command(
  OUTPUT "${PV_SERVER_DST}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${PICOVOICE_SERVER_BIN}" "${PV_SERVER_DST}"
  COMMENT "copy pv_server into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  VERBATIM
)

add_custom_target(copy_picovoice_server ALL
  DEPENDS "${PV_SERVER_DST}"
)