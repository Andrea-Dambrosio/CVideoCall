#ifndef __VAAPI_MJPEG
#define __VAAPI_MJPEG
#include "hardware/vaapi_core/vaapi_decode.h"
#include "jpeg_decoder.h"

/* the functions are declared into the expected order
 * of execution */

/* init the api and open the drm */
VAAPI vaapi_jpeg_init();

/* config the vaapi for JPEG decoding of the specified image size.
 * It allocate the GPU surfaces to store the decoded images.
 * If called more than 1 time it destroy the previous configuration and
 * reallocate all the GPU surfaces.
 * Use this if you have a new image resolution.*/
VA_STATUS vaapi_jpeg_config(JDEC jdec);

/* Sends all the parsed metadata to VAAPI.
 * Send the encoded data to VAAPI.
 * These buffers are still
 * not sended to the GPU */
VA_STATUS vaapi_jpeg_setdata(JDEC jdec);

/* Sends all the preloaded buffers to the GPU and start the decoding.
 * BLOKS the program until the decoding is completed */
VA_STATUS vaapi_jpeg_decode(JDEC jdec);
/* destroy the api and close the drm */
VA_STATUS vaapi_jpeg_destroy(JDEC jdec);

#ifndef NDEBUG
/* --------- TEST FUNCTIONS -------------*/

/* Save into the file the decoded image
 * if fn is NULL it saves into jpeg_to_yuv420.raw */
VA_STATUS vaapi_jpeg_testsaveimage(JDEC jdec, const char *fn);

#endif

#endif
