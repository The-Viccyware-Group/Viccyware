set(LIBARCHIVE_INCLUDE_PATH "${ANKI_THIRD_PARTY_DIR}/libarchive/vicos/include")
set(LIBARCHIVE_LIB_PATH "${ANKI_THIRD_PARTY_DIR}/libarchive/vicos/lib/libarchive.a")

set(ARCHIVE_LIBS archive)

add_library(archive STATIC IMPORTED)
anki_build_target_license(archive "BSD-4,${CMAKE_SOURCE_DIR}/licenses/archive.license")

set_target_properties(archive PROPERTIES
  IMPORTED_LOCATION "${LIBARCHIVE_LIB_PATH}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBARCHIVE_INCLUDE_PATH}")
