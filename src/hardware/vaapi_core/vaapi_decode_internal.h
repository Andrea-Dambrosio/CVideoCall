#ifndef __VAAPI_DEC_INTERNAL
#define __VAAPI_DEC_INTERNAL
#include <va/va.h>
#define N_RENDER_TARGETS 1
#define VAAPI_MAX_BUFFERS 16
struct vaapi_core {
    int fd;
    VADisplay dpy;
    VAProfile profile;
    VAConfigID config_id;
    VASurfaceID surfaces[N_RENDER_TARGETS];
    VAContextID context_id;
    VABufferID buffers[VAAPI_MAX_BUFFERS];
    unsigned int n_bufs;
    unsigned int w, h;
    int surface_ready;
    unsigned int images_mapped;
};
#endif
