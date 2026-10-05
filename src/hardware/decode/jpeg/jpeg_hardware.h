#ifndef __JDEC_HARDWARE
#define __JDEC_HARDWARE
#include "jpeg_decoder.h"

/* abstraction level over the machine hardware */
typedef struct JDEC_HW *JDEC_HW;
struct JDEC_HW {
    void *context;
    int (*config)(JDEC jdec);
    int (*setmetadata)(JDEC jdec);
    int (*decode)(JDEC jdec);
    int (*destroy_hw)(JDEC jdec);

#ifndef NDEBUG
    int (*savetest)(JDEC jdec, const char *fn);
#endif
};
/* functions are delared in the expected order of execution */

/* init the hw accelerator fot the jpeg decoding.
 *  It finds at runtime the available hardware (NVIDIA or INTEL/AMD) and
 *  load the relative api (NVAPI / VAAPI) manager for jpeg decoding.
 *  It opens the GPU.
 */
JDEC_HW jdec_hw_init();

/* Allocate the buffers for decoding output inside the GPU. The image size
 * must be declared. This function shall be called when the image size
 * change to clean the old buffers and allocate new buffers of the correct
 * size */
int jdec_hw_config(JDEC jdec);

/* send the parsed metadata and the encoded data
 * to the API (not to the GPU) */
int jdec_hw_setdata(JDEC jdec);

/* send the metadata and the encoded data to the GPU, then start
 * the decoding and waits for the GPU to end */
int jdec_hw_decode(JDEC jdec);

/* destroy the hw accelerator */
int jdec_hw_destroy(JDEC jdec);

#ifndef NDEBUG
/* save a decoded image into fn for testing purposes */
int jdec_hw_testsaveimage(JDEC jdec, const char *fn);
#endif

#endif
