if(DEFINED TFLITE_CMAKE_INCLUDED)
  return()
endif()
set(TFLITE_CMAKE_INCLUDED TRUE)

set(TENSORFLOW_PATH ${ANKI_THIRD_PARTY_DIR}/tensorflow2)

set(TFLITE_INCLUDE_PATHS
  ${TENSORFLOW_PATH}
  ${TENSORFLOW_PATH}/tensorflow/lite
  ${TENSORFLOW_PATH}/flatbuffers/include
)

if(VICOS)

  set(TFLITE_LIBS
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

elseif(MACOSX)

  set(TFLITE_LIBS
    ${TENSORFLOW_PATH}/tensorflow/contrib/lite/gen_OSX/lib/libtensorflow-lite.a
  )

endif()
