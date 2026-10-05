#include "jpeg_vaapi_manager.h"
#include "jpeg_decoder_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <va/va_dec_jpeg.h>

/* differently from the jpeg parser, the hardware errors are directly printed */
static VA_STATUS log_err(const char *format, ...) __attribute__((format(printf, 1, 2)));

static VA_STATUS log_err(const char *format, ...) {
    fprintf(stderr, "VAAPI JPEG: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    return VA_ERR;
}
static VA_STATUS param_check(JDEC jdec) {
    if (jdec == NULL)
        return log_err("NULL jdec");
    if (jdec->hw == NULL)
        return log_err("NULL hw");
    if (jdec->hw->context == NULL)
        return log_err("NULL vaapi");
    return VA_SUCCESS;
}
/* Send informations about the
 * picture size and components to VAAPI */
static VA_STATUS set_picparams(VAAPI va, JDEC jdec) {
    VA_STATUS st;
    VAPictureParameterBufferJPEGBaseline params = {0};
    params.picture_height = jdec->height;
    params.picture_width = jdec->width;
    params.num_components = jdec->n_cmp;
    for (int i = 0; i < jdec->n_cmp; i++) {
        params.components[i].component_id = jdec->components[i].Cid;
        params.components[i].h_sampling_factor = jdec->components[i].Hfactor;
        params.components[i].v_sampling_factor = jdec->components[i].Vfactor;
        params.components[i].quantiser_table_selector = jdec->components[i].Q_table_idx;
    }
    st = vaapi_setbuffer(va, VAPictureParameterBufferType, sizeof(VAPictureParameterBufferJPEGBaseline), 1,
                         &params);
    if (st == VA_ERR)
        return VA_ERR;

    return VA_SUCCESS;
}
/* Send the quantization tables to VAAPI
 * maybe in the future the copy can be omitted. */
static VA_STATUS set_Qtables(VAAPI va, JDEC jdec) {
    VA_STATUS st;
    VAIQMatrixBufferJPEGBaseline iq_matrix = {0};
    for (int i = 0; i < MAX_Q_TABLES; i++) {
        if (jdec->Q_tables[i][0] == 0) // is not loaded
            continue;
        iq_matrix.load_quantiser_table[i] = 1;
        memcpy(iq_matrix.quantiser_table[i], jdec->Q_tables[i], Q_Nvalues);
    }
    st = vaapi_setbuffer(va, VAIQMatrixBufferType, sizeof(iq_matrix), 1, &iq_matrix);
    if (st == VA_ERR)
        return VA_ERR;
    return VA_SUCCESS;
}
// clang-format off
/* Send the huffman tables to VAAPI
 * maybe in the future the copy can be omitted */
static VA_STATUS set_huffman(VAAPI va, JDEC jdec) {
    VAHuffmanTableBufferJPEGBaseline Htables = {0};
    VA_STATUS st;

    assert(HUFF_MAX_VALUES >= sizeof(Htables.huffman_table[0].dc_values));
    assert(HUFF_MAX_VALUES >= sizeof(Htables.huffman_table[0].ac_values));
    assert(HUFF_MAX_P_LEN >= sizeof(Htables.huffman_table[0].num_dc_codes));
    assert(HUFF_MAX_P_LEN >= sizeof(Htables.huffman_table[0].num_ac_codes));

    for (int i = 0; i < HUFF_TABLE_CLASSES; i++) {
        if (jdec->H_tables[HUFF_DC_TABLES][i].valid == false)
            continue;
        if (jdec->H_tables[HUFF_AC_TABLES][i].valid == false)
            continue;
        Htables.load_huffman_table[i] = 1;
        memcpy(Htables.huffman_table[i].num_dc_codes, 
               jdec->H_tables[HUFF_DC_TABLES][i].bits,
               sizeof(Htables.huffman_table[i].num_dc_codes
        ));
        memcpy(Htables.huffman_table[i].dc_values,
               jdec->H_tables[HUFF_DC_TABLES][i].huffval,
               sizeof(Htables.huffman_table[i].dc_values
        ));
        memcpy(Htables.huffman_table[i].num_ac_codes, 
               jdec->H_tables[HUFF_AC_TABLES][i].bits,
               sizeof(Htables.huffman_table[i].num_ac_codes
        ));
        memcpy(Htables.huffman_table[i].ac_values,
               jdec->H_tables[HUFF_AC_TABLES][i].huffval,
               sizeof(Htables.huffman_table[i].ac_values
        ));
    }
    st = vaapi_setbuffer(va, VAHuffmanTableBufferType, sizeof(Htables), 1, &Htables);
    if(st == VA_ERR)
        return VA_ERR;

    return VA_SUCCESS;
}
// clang-format on
/* Send to VAAPI the region of the JFIF buffer that contains the
 * actual data to be decoded (Entropy-Coded-Sections).
 * It actually send two buffer, one for the metadata about restart intervals
 * and size, and another is the actual slice with the data. */
static VA_STATUS set_slice(VAAPI va, JDEC jdec) {
    VA_STATUS st;
    VASliceParameterBufferJPEGBaseline slice_param;
    memset(slice_param.va_reserved, 0, sizeof(slice_param.va_reserved));
    assert(jdec->slice_end - jdec->slice_start > 0);
    slice_param.slice_data_size = jdec->slice_end - jdec->slice_start;
    slice_param.slice_data_offset = 0;
    slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
    slice_param.slice_horizontal_position = 0;
    slice_param.slice_vertical_position = 0;
    slice_param.num_components = jdec->n_cmp;
    slice_param.restart_interval = jdec->Ri;
    for (int i = 0; i < jdec->n_cmp; i++) {
        slice_param.components[i].component_selector = jdec->components[i].Cid;
        slice_param.components[i].dc_table_selector = jdec->components[i].DC_huff_dest;
        slice_param.components[i].ac_table_selector = jdec->components[i].AC_huff_dest;
    }
    int max_h_factor = jdec->components[0].Hfactor;
    int max_v_factor = jdec->components[0].Vfactor;
    slice_param.num_mcus = ((jdec->width + max_h_factor * 8 - 1) / (max_h_factor * 8)) *
                           ((jdec->height + max_v_factor * 8 - 1) / (max_v_factor * 8));

    st = vaapi_setbuffer(va, VASliceParameterBufferType, sizeof(slice_param), 1, &slice_param);
    if (st == VA_ERR)
        return VA_ERR;

    st =
        vaapi_setbuffer(va, VASliceDataBufferType, jdec->slice_end - jdec->slice_start, 1, jdec->slice_start);
    if (st == VA_ERR)
        return VA_ERR;
    return VA_SUCCESS;
}
/*********************************
 * --------- PUBLIC --------------
 *********************************/
/* init the api and open the drm */
VAAPI vaapi_jpeg_init() {
    VAAPI va = vaapi_init();
    if (va == NULL) {
        fprintf(stderr, "cannot init vaapi\n");
        return NULL;
    }
    if (vaapi_open(va, NULL) != VA_SUCCESS) {
        fprintf(stderr, "Cannot open drm device for vaapi\n");
        vaapi_destroy(va);
        return NULL;
    }
    return va;
}
/* config the vaapi for JPEG decoding of the specified image size.
 * It allocate the GPU surfaces to store the decoded images.
 * If called more than 1 time it destroy the previous configuration and
 * reallocate all the GPU surfaces. */
VA_STATUS vaapi_jpeg_config(JDEC jdec) {
    if (param_check(jdec) != VA_SUCCESS)
        return VA_ERR;
    VAAPI va = jdec->hw->context;
    unsigned int w = jdec->width, h = jdec->height;
    if (va == NULL)
        return log_err("NULL VAAPI to start config");
    if (w > VAAPI_MAX_HEIGHT || h > VAAPI_MAX_HEIGHT || w % 16 != 0 || h % 16 != 0)
        return log_err("Invalid w/h params for config (%d, %d)", w, h);

    if (vaapi_reset(va) != VA_SUCCESS)
        return VA_ERR;

    if (vaapi_config(va, VAProfileJPEGBaseline, w, h) == VA_ERR)
        return VA_ERR;

    return VA_SUCCESS;
}
/* Sends all the parsed metadata to VAAPI. These buffers are still
 * not sended to the GPU */
VA_STATUS vaapi_jpeg_setdata(JDEC jdec) {
    if (param_check(jdec) != VA_SUCCESS)
        return VA_ERR;
    VAAPI va = jdec->hw->context;

    if (vaapi_get_profile(va) != VAProfileJPEGBaseline)
        return log_err("This istance was not configured for JPEG");
    if (jdec->slice_start == NULL)
        return log_err("JFIF parsing is not done");

    if (vaapi_cleanbuffers(va) == VA_ERR)
        return VA_ERR;

    if (set_picparams(va, jdec) == VA_ERR)
        return VA_ERR;
    set_Qtables(va, jdec);
    set_huffman(va, jdec);
    set_slice(va, jdec);

    return VA_SUCCESS;
}
/* Sends all the preloaded buffers to the GPU and start the decoding.
 * BLOKS the program until the decoding is completed */
VA_STATUS vaapi_jpeg_decode(JDEC jdec) {
    if (param_check(jdec) != VA_SUCCESS)
        return VA_ERR;
    VAAPI va = jdec->hw->context;
    if (vaapi_decode(va, 0) == VA_ERR)
        return VA_ERR;

    return VA_SUCCESS;
}
/* destroy the api and close the drm */
VA_STATUS vaapi_jpeg_destroy(JDEC jdec) {
    if (param_check(jdec) != VA_SUCCESS)
        return VA_ERR;
    if (vaapi_destroy(jdec->hw->context) != VA_SUCCESS)
        return VA_ERR;
    return VA_SUCCESS;
}
#ifndef NDEBUG
/* --------- PUBLIC TEST FUNCTIONS -------------*/
/* Save into the raw file the decoded image */
VA_STATUS vaapi_jpeg_testsaveimage(JDEC jdec, const char *fn) {
    char fnstd[] = "jpeg_to_yuv420.raw";
    if (param_check(jdec) != VA_SUCCESS)
        return VA_ERR;
    if (jdec->decoded != true)
        return log_err("You should decode the image before saving it");
    if (fn == NULL)
        fn = fnstd;
    VAAPI va = jdec->hw->context;
    uint8_t *data;
    ImageData imgData;
    if (vaapi_copysurface(va, &data, &imgData) != VA_SUCCESS)
        return log_err("Error while copying the jpeg decoded image");
    FILE *fp = fopen(fn, "wb");
    if (fp == NULL) {
        vaapi_unmapsurface(va, imgData);
        return log_err("Cannot open output file for writing");
    }

    // Write Y plane (Luma) line by line
    for (unsigned int y = 0; y < jdec->height; y++) {
        uint8_t *line_ptr = data + imgData.offsets[0] + (y * imgData.pitches[0]);
        fwrite(line_ptr, 1, jdec->width, fp);
    }

    // Write UV plane (Chroma interleaved) line by line
    for (unsigned int y = 0; y < jdec->height / 2; y++) {
        uint8_t *line_ptr = data + imgData.offsets[1] + (y * imgData.pitches[1]);
        fwrite(line_ptr, 1, jdec->width, fp);
    }

    fclose(fp);

    if (vaapi_unmapsurface(va, imgData))
        return log_err("Cannot unmap the jpeg image");
    return VA_SUCCESS;
}
#endif
