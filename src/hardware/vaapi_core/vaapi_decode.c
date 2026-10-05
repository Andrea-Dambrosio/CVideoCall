#include "vaapi_decode.h"
#include "vaapi_decode_internal.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#define SURFACE_FORMAT VA_RT_FORMAT_YUV420
#define IS_VALID_JPEG_BUFFER(x)                                                                              \
    ((x) == VAPictureParameterBufferType || (x) == VAIQMatrixBufferType ||                                   \
     (x) == VAHuffmanTableBufferType || (x) == VASliceParameterBufferType || (x) == VASliceDataBufferType)

static int log_err(VAStatus st, const char *format, ...) __attribute__((format(printf, 2, 3)));

static int log_err(VAStatus st, const char *format, ...) {
    fprintf(stderr, "VAAPI Core: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    if (st != 0)
        fprintf(stderr, " | %s", vaErrorStr(st));

    fprintf(stderr, "\n");
    return VA_ERR;
}

static VA_STATUS query_entrypoints(VAAPI va) {
    int n_entry, valid_entry;
    VAStatus status;
    VAEntrypoint entrypts[5];
    assert(va != NULL);
    assert(va->profile == VAProfileJPEGBaseline || va->profile == VAProfileHEVCMain);
    assert(va->dpy != NULL);
    status = vaQueryConfigEntrypoints(va->dpy, va->profile, entrypts, &n_entry);
    if (status != VA_STATUS_SUCCESS)
        return log_err(status, "Cannot query entrypoints");

    for (valid_entry = 0; valid_entry < n_entry; valid_entry++) {
        if (entrypts[valid_entry] == VAEntrypointVLD)
            break;
    }
    if (valid_entry == n_entry)
        return log_err(0, "Cannot find an entrypoint for %d decoding", va->profile);
    return VA_SUCCESS;
}
static VA_STATUS query_RTFormat(VAAPI va, VAConfigAttrib *attrib) {
    VAStatus status;
    assert(va != NULL);
    assert(va->profile == VAProfileJPEGBaseline || va->profile == VAProfileHEVCMain);
    assert(va->dpy != NULL);
    if (va->profile == VAProfileJPEGBaseline) {
        attrib->type = VAConfigAttribRTFormat;
        status = vaGetConfigAttributes(va->dpy, VAProfileJPEGBaseline, VAEntrypointVLD, attrib, 1);
        if (status != VA_STATUS_SUCCESS)
            return log_err(status, "Cannot query supported format");
        if ((attrib->value & VA_RT_FORMAT_YUV420) == 0)
            return log_err(0, "Format YUV420 not founded for JPEG decoding");
    }
    if (va->profile == VAProfileHEVCMain)
        return log_err(0, "HEVC decoding is not supported now");
    attrib->value = VA_RT_FORMAT_YUV420;

    return VA_SUCCESS;
}
static VA_STATUS createSurface(VAAPI va) {
    assert(va != NULL);
    assert(va->dpy != NULL);
    VASurfaceAttrib Sattrib;
    Sattrib.type = VASurfaceAttribPixelFormat;
    Sattrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    Sattrib.value.type = VAGenericValueTypeInteger;
    Sattrib.value.value.i = VA_FOURCC_NV12;
    VAStatus st =
        vaCreateSurfaces(va->dpy, SURFACE_FORMAT, va->w, va->h, va->surfaces, N_RENDER_TARGETS, &Sattrib, 1);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot create surface for decoding");
    return VA_SUCCESS;
}
/* ------------------------------------------
 * ------------ PUBLIC FUNCTIONS -------------
 * -------------------------------------------
 */

VA_STATUS vaapi_reset(VAAPI va) {
    VAStatus st;
    if (va == NULL)
        return log_err(0, "NULL vaapi");
#ifndef NDEGUB
    if (va->images_mapped > 0)
        return log_err(0, "You must unmap al the mapped/copied surfaces before deletaing VAAPI context");
#endif
    if (vaapi_cleanbuffers(va) != VA_SUCCESS)
        log_err(0, "Error while cleaning VAAPI buffers");

    if (va->profile != 0) {
        st = vaDestroySurfaces(va->dpy, va->surfaces, N_RENDER_TARGETS);
        if (st != VA_STATUS_SUCCESS)
            log_err(st, "Cannot destroy surfaces");
        st = vaDestroyConfig(va->dpy, va->config_id);
        if (st != VA_STATUS_SUCCESS)
            log_err(st, "Cannot destry config");
        st = vaDestroyContext(va->dpy, va->context_id);
        if (st != VA_STATUS_SUCCESS)
            log_err(st, "Cannot destroy context");
    }
    va->profile = 0;
    return VA_SUCCESS;
}
VA_STATUS vaapi_destroy(VAAPI va) {
    int err = 0;
    if (va == NULL)
        return log_err(0, "NULL vaapi");
    if (va->images_mapped > 0)
        return log_err(0, "You must unmap al the mapped/copied surfaces before destroy VAAPI");
    VAStatus st;

    if (vaapi_reset(va) != VA_SUCCESS)
        log_err(st, "error while cleaning context");

    if (va->dpy != NULL) {
        st = vaTerminate(va->dpy);
        if (st != VA_STATUS_SUCCESS)
            log_err(st, "Cannot terminate vaapi");
    }

    if (close(va->fd) == -1)
        log_err(0, "Error closing drm file descriptor | %s", strerror(errno));

    free(va);
    return VA_SUCCESS;
}
VA_STATUS vaapi_cleanbuffers(VAAPI va) {
    VAStatus st;
    int err = 0;
    if (va == NULL)
        return log_err(0, "NULL vaapi");

    for (int i = 0; i < va->n_bufs; i++) {
        st = vaDestroyBuffer(va->dpy, va->buffers[i]);
        if (st != VA_STATUS_SUCCESS) {
            err = 1;
            log_err(st, "Cannot destroy %dth buffer", i);
        }
    }
    va->n_bufs = 0;
    va->surface_ready = 0;
    if (err == 1)
        return VA_ERR;
    return VA_SUCCESS;
}
VA_STATUS vaapi_decode(VAAPI va, unsigned int surfaceidx) {
    VAStatus st;
    if (va == NULL)
        return log_err(0, "vaapi_decode called with null vaapi");

    if (va->dpy == NULL)
        return log_err(0, "vaapi_decode called before vaapi_open");

    if (va->context_id == VA_INVALID_ID || va->config_id == VA_INVALID_ID ||
        va->surfaces[0] == VA_INVALID_SURFACE || va->profile == 0)
        return log_err(0, "vaapi_decode called before vaapi_config");

    if (va->n_bufs == 0)
        return log_err(0, "vaapi_decode called before vaapi_setmetadata");

    assert(va->profile == VAProfileJPEGBaseline || va->profile == VAProfileHEVCMain);

    st = vaBeginPicture(va->dpy, va->context_id, va->surfaces[0]);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot begin picture");

    st = vaRenderPicture(va->dpy, va->context_id, va->buffers, va->n_bufs);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot send metadata buffers to che GPU");

    st = vaEndPicture(va->dpy, va->context_id);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot process the image");

    st = vaSyncSurface(va->dpy, va->surfaces[0]);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot sync the surface");

    va->surface_ready = 1;

    return VA_SUCCESS;
}
/* Send a buffer to the VA-API, note that here the buffer are still
 * into the program memory, are not sended to the GPU*/
VA_STATUS vaapi_setbuffer(VAAPI va, VABufferType type, unsigned int size, unsigned int n_el, void *data) {
    VAStatus st;
    if (va == NULL)
        return log_err(0, "vaapi_config called with null vaapi");

    if (va->dpy == NULL)
        return log_err(0, "vaapi_config called before vaapi_open");

    if (va->context_id == VA_INVALID_ID || va->config_id == VA_INVALID_ID ||
        va->surfaces[0] == VA_INVALID_SURFACE || va->profile == 0)
        return log_err(0, "vaapi_setmetadata called before vaapi_config");

    if (va->n_bufs == VAAPI_MAX_BUFFERS)
        return log_err(0, "max number of usable buffers reached");

    assert(va->profile == VAProfileJPEGBaseline || va->profile == VAProfileHEVCMain);

    if (va->profile == VAProfileJPEGBaseline) {
        if (!IS_VALID_JPEG_BUFFER(type))
            return log_err(0, "Buffer type %d is not valid for jpeg decoding", type);
    }
    if (va->profile == VAProfileHEVCMain)
        return log_err(0, "HEVEC is now not supported");

    st = vaCreateBuffer(va->dpy, va->context_id, type, size, n_el, data, &va->buffers[va->n_bufs]);
    if (st != VA_STATUS_SUCCESS)
        return log_err(0, "Cannot create va buffer");

    va->n_bufs++;
    return VA_SUCCESS;
}
VA_STATUS vaapi_config(VAAPI va, VAProfile profile, unsigned int w, unsigned int h) {
    VAConfigAttrib attrib;
    VAStatus st;

    if (profile != VAProfileJPEGBaseline && profile != VAProfileHEVCMain)
        return log_err(0, "the %d profile is not supported", profile);

    if (va == NULL)
        return log_err(0, "vaapi_config called with null vaapi");

    if (va->dpy == NULL)
        return log_err(0, "vaapi_config called before vaapi_open");

    if (w > VAAPI_MAX_WIDTH || h > VAAPI_MAX_HEIGHT || w % 16 != 0 || h % 16 != 0)
        return log_err(0, "Invalid width or heigth value");
    if (va->profile != 0)
        return log_err(0, "You should clean the context before creating a new one");

    va->profile = profile;
    va->w = w;
    va->h = h;
    // check if the VLD entrypoint is vailable
    if (query_entrypoints(va) == VA_ERR)
        return VA_ERR;
    // check the available format
    if (query_RTFormat(va, &attrib) == VA_ERR)
        return VA_ERR;

    st = vaCreateConfig(va->dpy, profile, VAEntrypointVLD, &attrib, 1, &va->config_id);

    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot create VA config");

    if (createSurface(va) == VA_ERR)
        return log_err(st, "Cannot allocate surfaces");

    st = vaCreateContext(va->dpy, va->config_id, va->w, va->h, VA_PROGRESSIVE, va->surfaces, N_RENDER_TARGETS,
                         &va->context_id);

    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot create context");

    return VA_SUCCESS;
}
VA_STATUS vaapi_open(VAAPI vapi, const char *drm_path) {
    int major, minor;
    if (drm_path == NULL)
        vapi->fd = open("/dev/dri/renderD128", O_RDWR);
    else
        vapi->fd = open(drm_path, O_RDWR);

    if (vapi->fd == -1)
        return log_err(0, "Cannot open GPU | %s", strerror(errno));
    vapi->dpy = vaGetDisplayDRM(vapi->fd);
    if (vapi->dpy == NULL) {
        close(vapi->fd);
        return log_err(0, "Cannot create connection with hardware");
    }
    VAStatus st = vaInitialize(vapi->dpy, &major, &minor);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot inizialize vaapi");
    return VA_SUCCESS;
}
VAProfile vaapi_get_profile(VAAPI va) {
    if (va == NULL)
        return -1;
    return va->profile;
}
VAAPI vaapi_init() {
    VAAPI vapi = calloc(sizeof(struct vaapi_core), 1);
    if (vapi == NULL)
        return NULL;
    vapi->fd = -1;
    vapi->surfaces[0] = VA_INVALID_SURFACE;
    vapi->context_id = VA_INVALID_ID;
    vapi->config_id = VA_INVALID_ID;
    return vapi;
}
#ifndef NDEBUG
VA_STATUS vaapi_copysurface(VAAPI va, uint8_t **data, ImageData *img) {
    VAStatus st;
    VAImage image;
    VAImageFormat format;
    void *p = NULL;
    if (va == NULL)
        return log_err(0, "NULL vaapi");
    if (va->surface_ready == 0)
        return log_err(0, "No surface is ready to copy");
    if (data == NULL)
        return log_err(0, "Invalid pointer for data pointer");
    if (img == NULL)
        return log_err(0, "Invalid pointer for ImageData");

    format.fourcc = VA_FOURCC_NV12;
    format.byte_order = VA_LSB_FIRST;
    format.bits_per_pixel = 12;

    st = vaCreateImage(va->dpy, &format, va->w, va->h, &image);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot create allocate RAM for copy a surface");

    st = vaGetImage(va->dpy, va->surfaces[0], 0, 0, va->w, va->h, image.image_id);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot copy surface");

    st = vaMapBuffer(va->dpy, image.buf, &p);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot map image in the program addresses");

    *data = p;
    img->id = image.image_id;
    img->buf = image.buf;
    img->data_size = image.data_size;
    for (int i = 0; i < 3; i++) {
        img->pitches[i] = image.pitches[i];
        img->offsets[i] = image.offsets[i];
    }
    va->images_mapped++;
    return VA_SUCCESS;
}
VA_STATUS vaapi_mapsurface(VAAPI va, uint8_t **data, ImageData *img) {
    VAStatus st;
    VAImage image;
    void *p = NULL;
    if (va == NULL)
        return log_err(0, "NULL vaapi");
    if (va->surface_ready == 0)
        return log_err(0, "No surface is ready to copy");
    if (data == NULL)
        return log_err(0, "Invalid pointer for data pointer");
    if (img == NULL)
        return log_err(0, "Invalid pointer for ImageData");

    st = vaDeriveImage(va->dpy, va->surfaces[0], &image);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot derive image from surface");

    st = vaMapBuffer(va->dpy, image.buf, &p);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot map image in the program addresses");

    *data = p;
    img->id = image.image_id;
    img->buf = image.buf;
    img->data_size = image.data_size;
    for (int i = 0; i < 3; i++) {
        img->pitches[i] = image.pitches[i];
        img->offsets[i] = image.offsets[i];
    }
    va->images_mapped++;
    return VA_SUCCESS;
}
VA_STATUS vaapi_unmapsurface(VAAPI va, ImageData img) {
    VAStatus st;
    st = vaUnmapBuffer(va->dpy, img.buf);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot unmap %d buffer for image %d", img.buf, img.id);
    st = vaDestroyImage(va->dpy, img.id);
    if (st != VA_STATUS_SUCCESS)
        return log_err(st, "Cannot destroy image %d", img.id);
    va->images_mapped--;
    return VA_SUCCESS;
}
#endif
