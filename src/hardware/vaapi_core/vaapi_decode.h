#ifndef __VAAPI_DECODE_H
#define __VAAPI_DECODE_H
#include <va/va.h>
typedef struct vaapi_core *VAAPI;
typedef struct image_data ImageData;
struct image_data {
    VAImageID id;
    VABufferID buf;
    uint32_t data_size;
    uint32_t pitches[3];
    uint32_t offsets[3];
};
typedef enum { VA_ERR = -1, VA_SUCCESS = 0 } VA_STATUS;
#define VAAPI_MAX_WIDTH 2048
#define VAAPI_MAX_HEIGHT 2048
/* allocate the hw manager */
VAAPI vaapi_init();

/* open the GPU and start vaapi, if drm_path is NULL
 * dev/dri/renderD128 is used */
VA_STATUS vaapi_open(VAAPI vaapi, const char *drm_path);

/* clean the context and the surfaces. This should be
 * called to edit the config without closing the drm.
 * An use case is when the image size changes.
 * If you are running tests the mapped buffers should be freed
 * before calling this function. */
VA_STATUS vaapi_reset(VAAPI va);

/* set the decoding profile and create the
 * config, context surfaces for the declared profile and
 * image size */
VA_STATUS vaapi_config(VAAPI vaapi, VAProfile profile, unsigned int w, unsigned int h);

/* remove the previously loaded buffers from VAAPI memory,
 * call this if you need to send buffers for a new frame */
VA_STATUS vaapi_cleanbuffers(VAAPI va);

/* send a buffer to VAAPI (not to the GPU) of the declared type */
VA_STATUS vaapi_setbuffer(VAAPI va, VABufferType type, unsigned int size, unsigned int n_el, void *data);

/* sends all the preloaded buffers to the GPU and start the decoding, stops until the
 * decoding is done. */
VA_STATUS vaapi_decode(VAAPI vaapi, unsigned int surfaceidx);

// clean everything
VA_STATUS vaapi_destroy(VAAPI vaapi);

/* ------------ PUBLIC UTILS --------------*/
VAProfile vaapi_get_profile(VAAPI vaapi);

#ifndef NDEBUG
/* ------------ PUBLIC TEST FUNCTIONS -------------*/
// COPY the surface with the DMA into the RAM and
// map the copied buffer into the program addresses.
// This have bit more more overhead but is FASTER for reading
// ImageData containes the size of data and shall be used to free the buffer.
VA_STATUS vaapi_copysurface(VAAPI vaapi, uint8_t **data, ImageData *img);

// MAP the surface into the virtual addresses of the
// program. If the GPU is not integrated is done a
// mapping with the VRAM memory that is uncachable.
// This is slow for reading.
VA_STATUS vaapi_mapsurface(VAAPI vaapi, uint8_t **data, ImageData *img);

// used to deallocate the resources allocated by map
// surface and copy surface
VA_STATUS vaapi_unmapsurface(VAAPI vaapi, ImageData img);
#endif
#endif
