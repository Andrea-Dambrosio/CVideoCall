#include "hardware/decode/jpeg/jpeg_decoder.h"
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define USE_GPU 1
#define FUZZ_ITERATIONS 500000

// Helper to find a marker in the JPEG buffer
uint8_t *find_marker(uint8_t *buf, size_t size, uint8_t marker) {
    for (size_t i = 0; i < size - 1; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == marker) {
            return &buf[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    FILE *f = fopen("test_frame.jpg", "rb");
    unsigned int parsed = 0;
    unsigned int decoded = 0;
    if (!f) {
        perror("Failed to open test_frame.jpg for fuzzing");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *seed_buf = malloc(fsize);
    if (fread(seed_buf, 1, fsize, f) != fsize) {
        perror("Failed to read file");
        return 1;
    }
    fclose(f);

    uint8_t *mut_buf = malloc(fsize);

    // Initialize Decoder once.
    // We pass stderr as logfp to avoid creating thousands of log files.
    // Verbose is strictly set to 'false' so we don't spam the console.
    JDEC jdec = JDEC_init();
    if (!jdec) {
        fprintf(stderr, "JDEC_init failed\n");
        return 1;
    }

    srand(time(NULL));
    printf("Starting %d fuzzing iterations...\n", FUZZ_ITERATIONS);

    for (int i = 0; i < FUZZ_ITERATIONS; i++) {
        // 1. Reset buffer to clean seed
        memcpy(mut_buf, seed_buf, fsize);
        size_t mut_size = fsize;

        // 2. Pick an attack strategy
        int strategy = rand() % 5;

        switch (strategy) {
        case 0: { // A: Truncator
            mut_size = rand() % fsize;
            break;
        }
        case 1: { // B: Constant Buster (SOF)
            uint8_t *sof = find_marker(mut_buf, mut_size, 0xC0);
            if (sof && (sof - mut_buf + 10) < mut_size) {
                // Overwrite height
                uint16_t h = (rand() % 2) ? 0 : ((rand() % 2) ? 2049 : 65535);
                sof[4] = (h >> 8) & 0xFF;
                sof[5] = h & 0xFF;
                // Overwrite width
                uint16_t w = (rand() % 2) ? 0 : ((rand() % 2) ? 2049 : 65535);
                sof[6] = (w >> 8) & 0xFF;
                sof[7] = w & 0xFF;
                // Overwrite components
                sof[9] = (rand() % 2) ? 0 : ((rand() % 2) ? 4 : 255);
            }
            break;
        }
        case 2: { // C: Huffman Breaker (DHT)
            uint8_t *dht = find_marker(mut_buf, mut_size, 0xC4);
            if (dht && (dht - mut_buf + 21) < mut_size) {
                // The 16 bytes starting at offset 5 are the counts
                for (int c = 0; c < 16; c++) {
                    dht[5 + c] = rand() % 256; // Try to overflow sum > 256
                }
            }
            break;
        }
        case 3: { // D: Table Swapper (SOS)
            uint8_t *sos = find_marker(mut_buf, mut_size, 0xDA);
            if (sos && (sos - mut_buf + 8) < mut_size) {
                // Number of components in scan is at offset 4
                int n_cmp = sos[4];
                if ((size_t)(sos - mut_buf + 5 + n_cmp * 2) < mut_size) {
                    for (int c = 0; c < n_cmp; c++) {
                        // offset 5 + c*2 + 1 contains the table destinations
                        uint8_t val = (rand() % 16) << 4 | (rand() % 16);
                        sos[5 + c * 2 + 1] = val; // Try invalid AC/DC IDs like 4 or 15
                    }
                }
            }
            break;
        }
        case 4: { // E: Bit Flipper
            int flips = (rand() % 20) + 1;
            for (int f = 0; f < flips; f++) {
                int idx = rand() % mut_size;
                mut_buf[idx] ^= (1 << (rand() % 8)); // flip a random bit
            }
            break;
        }
        }

        // 3. Fuzz the parser!
        if (JDEC_parse_source(jdec, mut_buf, mut_size) == JDEC_SUCCESS) {
            parsed++;
            if (USE_GPU)
                if (JDEC_decode(jdec) == JDEC_SUCCESS)
                    decoded++;
        }

        if (i > 0 && i % 50000 == 0) {
            printf("Completed %d iterations...\n", i);
        }
    }

    printf("Fuzzing complete! No memory corruption detected by ASan!\n");
    printf("Parsed succesfuly: %d | Decoded succesfuly: %d\n", parsed, decoded);

    JDEC_destroy(jdec);
    free(seed_buf);
    free(mut_buf);
    return 0;
}
