#
# Maya-Wwise plugin
#

cmake_minimum_required(VERSION 3.6)

project(AnkiMayaWwisePlugin)

list(APPEND CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

include(maya)

set(LIB_NAME anki_maya_wwise_plugin)
set(MAYA_WWISE_PLUGIN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source)
set(SOURCE_FILES
  "${MAYA_WWISE_PLUGIN_SOURCE}/mayaAudioController.cpp"
  "${MAYA_WWISE_PLUGIN_SOURCE}/plugin.cpp"
  "${MAYA_WWISE_PLUGIN_SOURCE}/pluginCommands/reloadSoundBanksCmd.cpp"
  "${MAYA_WWISE_PLUGIN_SOURCE}/pluginCommands/stopPlaybackCmd.cpp"
  "${MAYA_WWISE_PLUGIN_SOURCE}/pluginCommands/updateMarkerDataCmd.cpp"
)

add_library(${LIB_NAME} SHARED ${SOURCE_FILES})

target_include_directories(${LIB_NAME}
PRIVATE
  $<BUILD_INTERFACE:${MAYA_WWISE_PLUGIN_SOURCE}>
  $<BUILD_INTERFACE:${MAYA_INCLUDE_PATH}>
)

# Mac Frameworks
find_library(CORE_FOUNDATION CoreFoundation)
find_library(AUDIO_TOOLBOX AudioToolbox)
find_library(CORE_AUDIO CoreAudio)
find_library(AUDIO_UNIT AudioUnit)
set(PLATFORM_LIBS
  ${CORE_FOUNDATION}
  ${AUDIO_TOOLBOX}
  ${CORE_AUDIO}
  ${AUDIO_UNIT}
)


target_link_libraries(${LIB_NAME}
  audio_engine
  util
  ${PLATFORM_LIBS}
  ${MAYA_LIBS}
)


set_target_properties(${LIB_NAME} PROPERTIES
  OUTPUT_NAME "AnkiMayaWwisePlugin"
  PREFIX ""
  SUFFIX ".bundle"
  LINK_FLAGS "-Wl,-executable_path,${MAYA_LIB_PATH}"
  -Wl,-exported_symbol,__Z16initializePluginN8Autodesk4Maya16OpenMaya201800007MObjectE
  -Wl,-exported_symbol,__Z18uninitializePluginN8Autodesk4Maya16OpenMaya201800007MObjectE
# -Wl,-exported_symbol,_MApiVersion   # NOT WORKING, but it works without
)


