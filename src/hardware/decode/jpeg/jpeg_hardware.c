#include "jpeg_hardware.h"
#include "jpeg_decoder_internal.h"
#include "jpeg_vaapi_manager.h"
#include <stdio.h>
#include <stdlib.h>
/* read jpeg_hardware.h for explanation */
/* TO-DO: ADD NVAPI support */
JDEC_HW jdec_hw_init() {
    JDEC_HW hw = malloc(sizeof(struct JDEC_HW));
    if (hw == NULL)
        return NULL;
    hw->context = vaapi_jpeg_init();

    if (hw->context == NULL) {
        free(hw);
        return NULL;
    }

    hw->config = vaapi_jpeg_config;
    hw->setmetadata = vaapi_jpeg_setdata;
    hw->decode = vaapi_jpeg_decode;
    hw->destroy_hw = vaapi_jpeg_destroy;
#ifndef NDEBUG
    hw->savetest = vaapi_jpeg_testsaveimage;
#endif

    return hw;
};
int check_param(JDEC jdec) {
    if (jdec == NULL)
        goto failed;
    if (jdec->hw == NULL)
        goto failed;
    if (jdec->hw->context == NULL)
        goto failed;
    return 0;
failed:
    fprintf(stderr, "Invalid parameter to jdec_hw accelerator\n");
    return -1;
}
int jdec_hw_config(JDEC jdec) {
    if (check_param(jdec) == -1)
        return -1;
    return jdec->hw->config(jdec);
}
int jdec_hw_setdata(JDEC jdec) {
    if (check_param(jdec) == -1)
        return -1;
    return jdec->hw->setmetadata(jdec);
}
int jdec_hw_decode(JDEC jdec) {
    if (check_param(jdec) == -1)
        return -1;
    return jdec->hw->decode(jdec);
}
int jdec_hw_destroy(JDEC jdec) {
    if (check_param(jdec) == -1)
        return -1;
    int r = jdec->hw->destroy_hw(jdec);
    free(jdec->hw);
    return r;
}
#ifndef NDEBUG
int jdec_hw_testsaveimage(JDEC jdec, const char *fn) {
    if (check_param(jdec) == -1)
        return -1;
    return jdec->hw->savetest(jdec, fn);
}
#endif
