# ==========================================
# MANUAL TEST: JPEG DECODER 
# ==========================================
add_executable(jdec_manual_dec jdec/jdec_manual.c)

target_link_libraries(jdec_manual_dec PRIVATE jpeg_decoder_debug asan_config)

add_test(NAME ManualTestJDEC COMMAND jdec_manual_dec)

# ==========================================
# FUZZ TEST: JPEG DECODER
# ==========================================
add_executable(jdec_fuzz jdec/jdec_fuzz.c)
target_link_libraries(jdec_fuzz PRIVATE jpeg_decoder asan_config)
add_test(NAME FuzzJDEC COMMAND jdec_fuzz)

# ==========================================
# COVERAGE-GUIDED FUZZ TEST (libFuzzer)
# ==========================================
option(USE_LIBFUZZER "Enable libFuzzer (Requires Clang)" OFF)
if(USE_LIBFUZZER)
    add_executable(jdec_libfuzzer jdec/jdec_libfuzzer.c)
    # libFuzzer uses its own special ASan and Fuzzer flags
    target_compile_options(jdec_libfuzzer PRIVATE -fsanitize=fuzzer,address -g)
    target_link_options(jdec_libfuzzer PRIVATE -fsanitize=fuzzer,address)
    target_link_libraries(jdec_libfuzzer PRIVATE jpeg_decoder)
endif()
