#include "hardware/decode/jpeg/jpeg_decoder.h"
#include <stddef.h>
#include <stdint.h>

// This is the entrypoint libFuzzer calls.
// It provides its own main() function internally.
JDEC global_jdec = NULL;

// Called exactly once at startup by libFuzzer
int LLVMFuzzerInitialize(int *argc, char ***argv) {
    global_jdec = JDEC_init();
    return 0;
}

// Called millions of times
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 2 || !global_jdec)
        return 0;

    // Feed the fuzzer's AI-generated buffer directly into the parser
    // JDEC_parse_source safely zeroes out internal state on every call
    if (JDEC_parse_source(global_jdec, (uint8_t *)Data, Size) == JDEC_SUCCESS)
        if (JDEC_decode(global_jdec) != JDEC_SUCCESS)
            fprintf(stderr, "Cannot decode");

    return 0;
}
