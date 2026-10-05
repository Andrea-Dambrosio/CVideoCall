#include "hardware/decode/jpeg/jpeg_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#define TEST_IMAGE "jpeg420.jpg"

// shoot frame:
// ffmpeg -f v4l2 -video_size 1280x720 -i /dev/video0 -vframes 1 -q:v 2 test_frame.jpg
// show frame:
// ffplay -f rawvideo -pixel_format nv12 -video_size 1280x720 -color_range pc output_yuv420.raw
static int decode_image() {
    printf("JDEC init...\n");
    JDEC jdec = JDEC_init();
    if (jdec == NULL) {
        fprintf(stderr, "Cannot init JDEC");
        goto failed;
    }

    printf("Opening test file...\n");
    FILE *fin = fopen("test_frame.jpg", "r");
    if (fin == NULL) {
        perror("Cannot open file");
        goto failed;
    }

    fseek(fin, 0, SEEK_END);
    long size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *buf = malloc(size * sizeof(uint8_t));

    if (buf == NULL) {
        perror("Cannot allocate buffer");
        goto failed;
    }
    printf("Loading image in buffer...\n");
    fread(buf, size, 1, fin);
    fclose(fin);
    fin = NULL;

    printf("Parsing source...\n");
    if (JDEC_parse_source(jdec, buf, size) != JDEC_SUCCESS) {
        JDEC_perror(jdec);
        goto failed;
    }
    printf("TEST: Source parsed correctly.\n");
    printf("Start decoding...\n");
    if (JDEC_decode(jdec) != JDEC_SUCCESS) {
        JDEC_perror(jdec);
        goto failed;
    }
    printf("TEST: Decoding completed.\n");
    printf("Saving image...\n");
    if (JDEC_testsaveimage(jdec, "output_yuv420.raw") != JDEC_SUCCESS) {
        JDEC_perror(jdec);
        goto failed;
    }
    printf("TEST: Image saved\n");

    free(buf);
    buf = NULL;
    JDEC_destroy(jdec);
    printf("Decoding complete.\n");
    return 0;

failed:
    fprintf(stderr, "Decoding failed.\n");
    if (fin != NULL)
        fclose(fin);

    if (buf != NULL)
        free(buf);

    if (jdec != NULL)
        JDEC_destroy(jdec);
    return -1;
}
int main(int argc, char **argv) {
    int r = 0;
    fprintf(stderr, "Decoding test image...\n");
    if (-1 == decode_image())
        r = -1;

    return r;
}
