project(update-engine)

include(archive)
include(anki_build_cxx)

if (VICOS)

  anki_build_cxx_executable(update-engine ${ANKI_SRCLIST_DIR})
  anki_build_target_license(update-engine "ANKI")

  target_compile_options(update-engine
    PRIVATE
    -Wno-unused-result
  )

  target_include_directories(update-engine
    PRIVATE
    ${CMAKE_SOURCE_DIR}
  )
  
  target_link_libraries(update-engine
    PRIVATE
    ${ARCHIVE_LIBS}
    pthread
    crypto 
    z
    zstd
    bz2
    lzma
  )

  anki_build_strip(TARGET update-engine)

endif(VICOS)
