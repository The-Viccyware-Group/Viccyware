# coretech/neuralnets/CMakeLists.txt

# Set this variable to the type of neural net platform to use
set(NEURAL_NET_PLATFORM TFLite)

# List of neural net options to validate the above with
set(VALID_NEURAL_NET_PLATFORMS None TensorFlow TFLite)

# Validate the setting
list(FIND VALID_NEURAL_NET_PLATFORMS ${NEURAL_NET_PLATFORM} index)
if(index EQUAL -1)
  message(FATAL_ERROR "NEURAL_NET_PLATFORM must be one of ${VALID_NEURAL_NET_PLATFORMS}")
endif()

if (NEURAL_NET_PLATFORM STREQUAL "None")
  return()
endif()

project(cti_neuralnets)

include(anki_build_cxx)

cmake_minimum_required(VERSION 3.6)

message(STATUS "Building NeuralNets process using ${NEURAL_NET_PLATFORM}")

include(anki_build_cxx)
include(anki_build_strip)
include(protobuf)

set(PLATFORM_LIBS "")
set(COMMON_LIBS
  jsoncpp
  util
  cti_common
  cti_messaging    # For offboard neural net comms
  cti_vision
  ${OPENCV_LIBS}
)

set(PLATFORM_INCLUDES "")

set(PLATFORM_FLAGS "")
set(COMMON_FLAGS
  -Wno-undef                    # Tensorflow includes don't adhere to our lofty standards
  -Wno-shorten-64-to-32         #  "
)

set(PLATFORM_DEFINITIONS "")

set(TENSORFLOW_PATH ${ANKI_THIRD_PARTY_DIR}/tensorflow2)

if(NEURAL_NET_PLATFORM STREQUAL "TensorFlow")

  if (VICOS)

    set(PLATFORM_FLAGS
      --std=c++11 
      -stdlib=libc++    # NOTE: VicOS uses libc++, so TensorFlow must be built to match
      -fPIE 
      -mfloat-abi=softfp 
      -mfpu=neon 
      -pie 
    )
    
    set(PLATFORM_INCLUDES
      ${ANKI_THIRD_PARTY_DIR}/opencv/vicos/include
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/proto
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/protobuf/armeabi-v7a/include
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/downloads/eigen
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/downloads/nsync/public
      ${TENSORFLOW_PATH}
    )

    set(PLATFORM_LIBS
      victorCrashReports
      -Wl,--whole-archive 
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/lib/libtensorflow-core.a
      -Wl,--no-whole-archive
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/protobuf/armeabi-v7a/lib/libprotobuf.a
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen_VICOS/downloads/nsync/builds/default.vicos.c++11/libnsync.a
      c++   # NOTE: VicOS uses libc++, so TensorFlow must be built to match
      log
      z
    )
    
  elseif (MACOSX)

    set(PLATFORM_FLAGS
      --std=c++11
    )
    
    set(PLATFORM_INCLUDES
      ${ANKI_THIRD_PARTY_DIR}/opencv/mac
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/proto
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/protobuf/include
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/downloads/eigen
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/downloads/nsync/public
      ${TENSORFLOW_PATH}
    )

    set(PLATFORM_LIBS
      -Wl,-force_load
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/lib/libtensorflow-core.a
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/protobuf/lib/libprotobuf.a
      ${TENSORFLOW_PATH}/tensorflow/contrib/makefile/gen/downloads/nsync/builds/default.macos.c++11/libnsync.a
      ${WEBOTS_LIBS}
      stdc++
      m
    )

  endif()

  set(PLATFORM_DEFINITIONS 
    ANKI_NEURALNETS_USE_TENSORFLOW
  )

elseif(NEURAL_NET_PLATFORM STREQUAL "TFLite")

  if (VICOS)

  # derived from label_image tflite example's linking command
      set(PLATFORM_LIBS
        victorCrashReports
        ${TENSORFLOW_PATH}/lib/libtensorflow-lite.a

        ${TENSORFLOW_PATH}/lib/libabsl_flags.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_marshalling.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_reflection.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_config.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_program_name.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_private_handle_accessor.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_commandlineflag.a
        ${TENSORFLOW_PATH}/lib/libabsl_flags_commandlineflag_internal.a

        ${TENSORFLOW_PATH}/lib/libabsl_status.a
        ${TENSORFLOW_PATH}/lib/libabsl_str_format_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_strerror.a

        ${TENSORFLOW_PATH}/lib/libfarmhash.a

        ${TENSORFLOW_PATH}/lib/libfft2d_fftsg2d.a
        ${TENSORFLOW_PATH}/lib/libfft2d_fftsg.a

        -lm

        ${TENSORFLOW_PATH}/lib/libflatbuffers.a
        ${TENSORFLOW_PATH}/lib/libprotobuf.a
        ${TENSORFLOW_PATH}/lib/libprotoc.a

        ${TENSORFLOW_PATH}/lib/libxnnpack-delegate.a
        ${TENSORFLOW_PATH}/lib/libXNNPACK.a
        ${TENSORFLOW_PATH}/lib/libmicrokernels-all.a
        ${TENSORFLOW_PATH}/lib/libmicrokernels-prod.a
        ${TENSORFLOW_PATH}/lib/libcpuinfo_internals.a
        ${TENSORFLOW_PATH}/lib/libcpuinfo.a
        ${TENSORFLOW_PATH}/lib/libpthreadpool.a

        ${TENSORFLOW_PATH}/lib/libruy_context_get_ctx.a
        ${TENSORFLOW_PATH}/lib/libruy_context.a
        ${TENSORFLOW_PATH}/lib/libruy_frontend.a
        ${TENSORFLOW_PATH}/lib/libruy_kernel_arm.a
        ${TENSORFLOW_PATH}/lib/libruy_kernel_avx.a
        ${TENSORFLOW_PATH}/lib/libruy_kernel_avx2_fma.a
        ${TENSORFLOW_PATH}/lib/libruy_kernel_avx512.a
        ${TENSORFLOW_PATH}/lib/libruy_apply_multiplier.a
        ${TENSORFLOW_PATH}/lib/libruy_pack_arm.a
        ${TENSORFLOW_PATH}/lib/libruy_pack_avx.a
        ${TENSORFLOW_PATH}/lib/libruy_pack_avx2_fma.a
        ${TENSORFLOW_PATH}/lib/libruy_pack_avx512.a
        ${TENSORFLOW_PATH}/lib/libruy_prepare_packed_matrices.a
        ${TENSORFLOW_PATH}/lib/libruy_trmul.a
        ${TENSORFLOW_PATH}/lib/libruy_ctx.a
        ${TENSORFLOW_PATH}/lib/libruy_allocator.a
        ${TENSORFLOW_PATH}/lib/libruy_prepacked_cache.a
        ${TENSORFLOW_PATH}/lib/libruy_system_aligned_alloc.a
        ${TENSORFLOW_PATH}/lib/libruy_have_built_path_for_avx.a
        ${TENSORFLOW_PATH}/lib/libruy_have_built_path_for_avx2_fma.a
        ${TENSORFLOW_PATH}/lib/libruy_have_built_path_for_avx512.a
        ${TENSORFLOW_PATH}/lib/libruy_thread_pool.a
        ${TENSORFLOW_PATH}/lib/libruy_blocking_counter.a
        ${TENSORFLOW_PATH}/lib/libruy_wait.a
        ${TENSORFLOW_PATH}/lib/libruy_denormal.a
        ${TENSORFLOW_PATH}/lib/libruy_block_map.a
        ${TENSORFLOW_PATH}/lib/libruy_tune.a
        ${TENSORFLOW_PATH}/lib/libruy_cpuinfo.a
        ${TENSORFLOW_PATH}/lib/libruy_profiler_instrumentation.a
        

        -pthread
        -ldl
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_message.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_format.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_proto.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_globals.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_log_sink_set.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_nullguard.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_check_op.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_globals.a
        ${TENSORFLOW_PATH}/lib/libabsl_examine_stack.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_sink.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_internal_log_sink_set.a
        ${TENSORFLOW_PATH}/lib/libabsl_bad_any_cast_impl.a
        ${TENSORFLOW_PATH}/lib/libabsl_hash.a
        ${TENSORFLOW_PATH}/lib/libabsl_bad_variant_access.a
        ${TENSORFLOW_PATH}/lib/libabsl_city.a
        ${TENSORFLOW_PATH}/lib/libabsl_low_level_hash.a
        ${TENSORFLOW_PATH}/lib/libabsl_cord.a
        ${TENSORFLOW_PATH}/lib/libabsl_cordz_info.a
        ${TENSORFLOW_PATH}/lib/libabsl_cord_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_cordz_functions.a
        ${TENSORFLOW_PATH}/lib/libabsl_cordz_handle.a
        ${TENSORFLOW_PATH}/lib/libabsl_crc_cord_state.a
        ${TENSORFLOW_PATH}/lib/libabsl_crc32c.a
        ${TENSORFLOW_PATH}/lib/libabsl_crc_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_crc_cpu_detect.a
        ${TENSORFLOW_PATH}/lib/libabsl_raw_hash_set.a
        ${TENSORFLOW_PATH}/lib/libabsl_bad_optional_access.a
        ${TENSORFLOW_PATH}/lib/libabsl_hashtablez_sampler.a
        ${TENSORFLOW_PATH}/lib/libabsl_exponential_biased.a
        ${TENSORFLOW_PATH}/lib/libabsl_synchronization.a
        ${TENSORFLOW_PATH}/lib/libabsl_stacktrace.a
        ${TENSORFLOW_PATH}/lib/libabsl_symbolize.a
        ${TENSORFLOW_PATH}/lib/libabsl_kernel_timeout_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_debugging_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_demangle_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_graphcycles_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_malloc_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_time.a
        ${TENSORFLOW_PATH}/lib/libabsl_strings.a
        ${TENSORFLOW_PATH}/lib/libabsl_strings_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_throw_delegate.a
        ${TENSORFLOW_PATH}/lib/libabsl_int128.a
        ${TENSORFLOW_PATH}/lib/libabsl_civil_time.a
        ${TENSORFLOW_PATH}/lib/libabsl_time_zone.a
        ${TENSORFLOW_PATH}/lib/libabsl_base.a
        ${TENSORFLOW_PATH}/lib/libabsl_raw_logging_internal.a
        ${TENSORFLOW_PATH}/lib/libabsl_log_severity.a
        ${TENSORFLOW_PATH}/lib/libabsl_spinlock_wait.a

        -lrt
      )


  elseif (MACOSX)

    #macos is a lost cause, sorry

    set(TFLITE_DOWNLOADS_PATH ${TENSORFLOW_PATH}/tensorflow/contrib/lite/gen_OSX/downloads)

    set(PLATFORM_LIBS
      ${TENSORFLOW_PATH}/tensorflow/contrib/lite/gen_OSX/lib/libtensorflow-lite.a
    )

  endif()

  set(PLATFORM_INCLUDES
    ${TENSORFLOW_PATH}
    ${TENSORFLOW_PATH}/tensorflow/lite
    ${TENSORFLOW_PATH}/flatbuffers/include
  )

  set(PLATFORM_DEFINITIONS 
    ANKI_NEURALNETS_USE_TFLITE
    TFLITE_PROFILING_ENABLED
  )
  
else()

  message(FATAL_ERROR "NEURAL_NET_PLATFORM should be either 'TensorFlow' or 'TFLite'")

endif()

#
# cti_neuralnets library
#
anki_build_cxx_library(cti_neuralnets ${ANKI_SRCLIST_DIR} STATIC)
anki_build_target_license(cti_neuralnets "ANKI" "Apache-2.0,${CMAKE_SOURCE_DIR}/licenses/tensorflow.license")

target_compile_options(cti_neuralnets
  PRIVATE
  ${COMMON_FLAGS}
  ${PLATFORM_FLAGS}
  ${ASAN_CXX_FLAGS} 
)

target_include_directories(cti_neuralnets
  PRIVATE
  ${PLATFORM_INCLUDES}
  ${OPENCV_INCLUDE_PATHS}
)

target_compile_definitions(cti_neuralnets
  PUBLIC
  ${PLATFORM_DEFINITIONS}
)
target_link_libraries(cti_neuralnets
  PRIVATE
  ${COMMON_LIBS}
  ${PLATFORM_LIBS}
  ${ASAN_LINKER_FLAGS}
)


#
# Unit test target
#

set(TEST_TARGET_NAME cti_neuralnet_test)

if (MACOSX)

  enable_testing()

  include(gtest)

  anki_build_cxx_executable(${TEST_TARGET_NAME} ${ANKI_SRCLIST_DIR})
  anki_build_target_license(${TEST_TARGET_NAME} "ANKI")

  target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
    TEST_DATA_PATH=${CMAKE_CURRENT_SOURCE_DIR}
    ${PLATFORM_DEFINITIONS}
  )

  target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    ${COMMON_LIBS}
    cti_neuralnets
    gtest
  )

  add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})

  set_tests_properties(${TEST_TARGET_NAME}
    PROPERTIES
    ENVIRONMENT "GTEST_OUTPUT=xml:ctiNeuralNetGoogleTest.xml"
  )

endif()
