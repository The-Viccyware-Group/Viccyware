cmake_minimum_required(VERSION 3.10)

# config files that live in /anki/etc
add_subdirectory(anki-trace)

add_subdirectory(config)

add_subdirectory(camera)

add_subdirectory(whiskeyToF)

add_subdirectory(switchboard)

add_subdirectory(update-engine)

add_subdirectory(diagnostics-logger)

add_subdirectory(victorCrashReports)

add_subdirectory(faultCodeDisplay)

add_subdirectory(vic-log-event)

add_subdirectory(vic-log-forward)

add_subdirectory(ion-sample)

add_subdirectory(gpio)

#
# Macro command to create build rules for full symbol files
#
macro(add_strip_target libname)
  # Add a custom command to generate the .full file,
  # then add a custom target that depends on the file
  set(libpath ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${libname})
  set(libfull ${libpath}.full)
  set(STRIP_CMD "${VICOS_TOOLCHAIN_PREFIX}strip")
  set(OBJCOPY_CMD "${VICOS_TOOLCHAIN_PREFIX}objcopy")
  add_custom_command(
    OUTPUT ${libfull}
    DEPENDS ${libpath}
    COMMAND ${CMAKE_COMMAND} -E copy ${libpath} ${libfull}
    COMMAND ${STRIP_CMD} --strip-unneeded ${libpath}
    COMMAND ${OBJCOPY_CMD} --add-gnu-debuglink ${libfull} ${libpath}
    COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${libfull}
    COMMENT "strip ${libname}"
    VERBATIM
  )
  add_custom_target(${libname} ALL DEPENDS ${libfull})
endmacro()

if (VICOS)
  # For each imported library, create a target to generate the symbol file
  set(STRIP_TARGETS
    libc++.so.1
    libopencv_calib3d.so.412
    libopencv_core.so.412
    libopencv_features2d.so.412
    libopencv_flann.so.412
    libopencv_imgcodecs.so.412
    libopencv_imgproc.so.412
    libopencv_ml.so.412
    libopencv_objdetect.so.412
    ${AVS_LIB_NAMES}
  )
  foreach (target ${STRIP_TARGETS})
    add_strip_target(${target})
  endforeach()

endif()
