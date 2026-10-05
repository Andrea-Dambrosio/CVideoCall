#ifndef __MJDEC_H
#define __MJDEC_H
#include <stdbool.h>
#include <stdint.h>

typedef struct jpeg_decoder *JDEC;

typedef enum {
    JDEC_ERR = -1,
    JDEC_SUCCESS = 0,
} JDEC_RETCODE;

/* The functions are declared in the expected order of execution */

/* Init the decoder */
JDEC JDEC_init(void);

/* set (or reset) the expected size of the images,
 * in production you should preset the image size.
 * A reset of the image size will reallocate all the GPU
 * surfaces. */
JDEC_RETCODE JDEC_presetsize(JDEC jdec, unsigned int w, unsigned int h);

/* Parse the headers contained in the JFIF buffer
 * to load the metadata for decoding. */
JDEC_RETCODE JDEC_parse_source(JDEC jdec, uint8_t *buf, unsigned int buff_size);

/* Use the parsed metadata to decode the image with the GPU.
 * For now this call is blocking. It waits the end of the decoding. */
JDEC_RETCODE JDEC_decode(JDEC jdec);

/* destroy the parser */
JDEC_RETCODE JDEC_destroy(JDEC jdec);

/* Print a string of the last error is the ret code is JDEC_ERR */
void JDEC_perror(JDEC jdec);

#ifndef NDEBUG
/* ---------- TEST FUNCTIONS ------------- */
/* save the decoded image into a raw file */
JDEC_RETCODE JDEC_testsaveimage(JDEC jdec, const char *fn);

#endif
#endif
