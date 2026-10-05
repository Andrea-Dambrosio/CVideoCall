#include "jpeg_decoder.h"
#include "hardware/decode/jpeg/jpeg_hardware.h"
#include "jpeg_decoder_internal.h"
#include "std_huff_tbls.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// clang-format off
#ifndef NDEBUG
    #define wrDLog(fmt, args...) do { \
       printf("JDEC: "); \
       printf(fmt, ## args); \
       printf("\n"); \
    } while(0)
#else
    #define wrDLog(fmt, args...)
#endif
// clang-format on

static int err(JDEC jdec, char *format, ...) __attribute__((format(printf, 2, 3)));

// return the error code and set the string error
static int err(JDEC jdec, char *format, ...) {
    wrDLog("Saving error string.");
    assert(jdec != NULL);
    va_list args;
    va_start(args, format);
    if (-1 == vsnprintf(jdec->err_str, JDEC_ERRSTRSIZE, format, args))
        fprintf(stderr, "Error with snprintf while generating error string for JDEC");
    va_end(args);
    return -1;
}
// checks to do after parsing the meta data
static int check_HV_factors(JDEC jdec) {
    assert(jdec != NULL);
    assert(jdec->n_cmp > 0 && jdec->n_cmp <= MAX_COMPONENTS);
    int sum = 0;
    wrDLog("Checking HV factors");
    for (int i = 0; i < jdec->n_cmp; i++)
        sum += jdec->components[i].Hfactor * jdec->components[i].Vfactor;
    if (sum > 10)
        return -1;
    return 0;
}
static int check_tables(JDEC jdec) {
    assert(jdec != NULL);
    assert(jdec->n_cmp != 0);
    wrDLog("Checking if all the tables are present");
    for (int i = 0; i < jdec->n_cmp; i++) {
        uint8_t idx = jdec->components[i].Q_table_idx;
        if (jdec->Q_tables[idx][0] == 0)
            return err(jdec, "Q table requested by %dth cmp of dest %d is not present", idx, i);

        idx = jdec->components[i].AC_huff_dest;
        if (jdec->H_tables[HUFF_AC_TABLES][idx].valid == false)
            return err(jdec, "AC table %d is requested but not loaded", idx);

        idx = jdec->components[i].DC_huff_dest;
        if (jdec->H_tables[HUFF_DC_TABLES][idx].valid == false)
            return err(jdec, "DC table %d is requested but not loaded", idx);
    }
    wrDLog("Check completed with success");
    return 0;
}
static int skip_padding(JDEC jdec) {
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);

    while (STREAM_BYTE_TO_END(jdec) >= 2 && jdec->stream[0] == 0xFF && jdec->stream[1] == 0xFF) {
        jdec->stream++;
    }
    if (STREAM_BYTE_TO_END(jdec) < 2)
        return err(jdec, "Truncated stream in padding region");
    return 0;
}
/* get the length parameter after the marker, checks if the
 * value is valid, align the pointer to the first parameter,
 * also set the value of end as the pointer to the next marker
 * both len_p and end_p can be NULL
 */
static int get_region_len(JDEC jdec, uint16_t *len_p, uint8_t **end_p) {
    uint16_t len;
    wrDLog("Getting marker region length");
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);

    if (STREAM_BYTE_TO_END(jdec) < 4)
        return err(jdec, "Unexpected end of stream while reading region length");

    if (jdec->stream[0] != 0xFF)
        return err(jdec, "get_region_length must start with a marker");
    len = jdec->stream[2] << 8 | jdec->stream[3];

    if (len > jdec->stream_end - jdec->stream - 2)
        return err(jdec, "Marker length (%u) overflow the buffer", len);

    wrDLog("Valid region of length %d", len);
    if (end_p != NULL)
        *end_p = jdec->stream + 2 + len;
    if (len_p != NULL)
        *len_p = len;

    jdec->stream += 4;

    return 0;
}
/* check and set the size of the image */
static int chkset_size(JDEC jdec, unsigned int w, unsigned int h) {
    assert(jdec != NULL);
    JDEC_RETCODE r;

    if (w == 0 || h == 0 || w > JPEG_MAX_WIDTH || h > JPEG_MAX_HEIGHT)
        return err(jdec, "Width, Heigth (%d,%d) are invalid or too big", w, h);
    if (w == 0 || h == 0)
        return err(jdec, "Image size is 0");

    if (jdec->width == 0 || jdec->height == 0) {
        wrDLog("WARNING: Setting jpeg image as the size of the first frame. You should preset it.");
        wrDLog("Captured image size: (%dx%d)", w, h);
        r = JDEC_presetsize(jdec, w, h);
        if (r != JDEC_SUCCESS)
            return -1;
    }

    if (jdec->width != w || jdec->height != h)
        return err(jdec, "Unexpected img size. It must be costant or you should reconfig the hw accel");

    return 0;
}
/* skip a marker that have parameters */
static int skip_marker_p(JDEC jdec) {
    uint8_t *end;
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);

    if (jdec->stream[0] != 0xFF)
        return err(jdec, "skip_marker needs the stream on 0xFF");

    wrDLog("Skipping marker %02X", jdec->stream[1]);

    if (get_region_len(jdec, NULL, &end) == -1)
        return -1;

    jdec->stream = end;
    return 0;
}
/* parse the restart interval region*/
static int parse_DRI(JDEC jdec) {
    uint8_t *end;
    uint16_t len;
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);
    wrDLog("DRI parsing starts\n");

    if (jdec->stream[0] != 0xFF || jdec->stream[1] != DRI)
        return err(jdec, "parse_DRI must start with 0xFFDD");

    if (get_region_len(jdec, &len, &end) == -1)
        return -1;

    if (len != 4)
        return err(jdec, "Invalid len parameter for DRI segment");

    jdec->Ri = jdec->stream[0] << 8 | jdec->stream[1];
    wrDLog("Setting restart interval value to %u", jdec->Ri);
    jdec->stream = end;
    return 0;
}
/* Parse the quantization tables */
static int parse_DQT(JDEC jdec) {
    int nQt = 0;
    uint8_t *stream, *Q_end;
    uint16_t len;
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);

    wrDLog("DQT parsing starts");

    if (jdec->stream[0] != 0xFF || jdec->stream[1] != DQT)
        return err(jdec, "parse_DQT must start with 0xFF - DQT");

    if (get_region_len(jdec, &len, &Q_end) == -1)
        return -1;

    stream = jdec->stream;

    // the number of quantization tables is (len-2)/65
    wrDLog("loading quantization tables with %u parameters...", len - 2);

    while (stream < Q_end) {

        if (stream + 1 + Q_Nvalues > Q_end)
            return err(jdec, "DQT segment truncated, not enough bytes for table");

        if ((*stream & DQT_PRECISION_MASK) != 0)
            return err(jdec, "Precision parameter must be 0 in the quantization table");

        uint8_t Q_index = *stream & DQT_INDEX_MASK;
        stream++;
        if (Q_index >= MAX_Q_TABLES)
            return err(jdec, "Invalid %d Q_index (must be 0-3)\n", Q_index);

        wrDLog("Loading table of index %d...", Q_index);

        memcpy(jdec->Q_tables[Q_index], stream, Q_Nvalues);

        wrDLog("Table of index %d loaded", Q_index);
        stream += Q_Nvalues;
        nQt++;
    }
    if (stream != Q_end)
        return err(jdec, "Mismatch between DQT length and processed bytes");

    wrDLog("%d Quantization tables reads", nQt);

    jdec->stream = stream;
    return 0;
}
/* parse Define Huffman Tables
 * Note that if the DHT marker is not present in a frame
 * the decoder will use the last loaded huffman tables.
 * In the majority of case the H tables will be loaded for each
 * frame, but in the case of low-level camera that not sends them,
 * the standard tables will be used (loaded in the JDEC_init function).
 * As soon as a DHT marker is founded the standard tables are invalidated
 * (or tables previously loaded)
 * */
static int parse_DHT(JDEC jdec) {
    uint16_t len;
    uint8_t *stream, *H_end;
    int nH = 0;
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);
    wrDLog("DHT parsing starts");

    if (jdec->stream[0] != 0xFF || jdec->stream[1] != DHT)
        return err(jdec, "parse_DHT must start with 0xFF - DHT");

    if (get_region_len(jdec, &len, &H_end) == -1)
        return -1;

    stream = jdec->stream;
    // if the DHT is present invalid the standard tables or the old
    // ones
    jdec->H_tables[HUFF_AC_TABLES][0].valid = false;
    jdec->H_tables[HUFF_AC_TABLES][1].valid = false;
    jdec->H_tables[HUFF_DC_TABLES][0].valid = false;
    jdec->H_tables[HUFF_DC_TABLES][1].valid = false;

    wrDLog("loading huffman table tables with %u parameters", len - 2);

    while (stream < H_end) {
        if (stream + 1 + HUFF_MAX_P_LEN > H_end)
            return err(jdec, "Truncated hufftable region");

        uint8_t t_class = (stream[0] & HTABLE_CLASS_MASK) >> 4;
        uint8_t t_dest = stream[0] & HTABLE_DEST_MASK;

        wrDLog("Founded H table with metadata:");
        wrDLog("class: %d | dest: %d", t_class, t_dest);
        if (t_class >= HUFF_TABLE_CLASSES)
            return err(jdec, "Invalid %d t_class value for H table", t_class);

        if (t_dest >= HUFF_TABLES_PER_CLASS)
            return err(jdec, "Invalid %d t_dest value for H table", t_dest);

        uint8_t *bits = jdec->H_tables[t_class][t_dest].bits;
        uint8_t *huffval = jdec->H_tables[t_class][t_dest].huffval;

        stream++;
        wrDLog("Copying the bits list...");
        memcpy(bits, stream, HUFF_MAX_P_LEN);
        wrDLog("Copy completed");

        stream += HUFF_MAX_P_LEN;
        unsigned int n_values = 0;
        for (int i = 0; i < HUFF_MAX_P_LEN; i++)
            n_values += bits[i];
        wrDLog("Huffman table of %d values", n_values);

        if (n_values > HUFF_MAX_VALUES)
            return err(jdec, "Corrupted number of values %d in H table", n_values);

        if (stream + n_values > H_end)
            return err(jdec, "Truncated huffval list");

        wrDLog("Copying huffval list...");
        memcpy(huffval, stream, n_values);
        wrDLog("Copy completed");
        jdec->H_tables[t_class][t_dest].valid = true;
        stream += n_values;
        nH++;
    }
    if (stream != H_end)
        return err(jdec, "Mismatch between DHT length and processed bytes");
    jdec->stream = stream;
    wrDLog("%d huffman tables loaded", nH);
    return 0;
}
/* parse misc/area before SOF or SOS */
static int parse_tables_area(JDEC jdec) {
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    wrDLog("Parsing misc area...");
    int retcod = 0;
    while (1) {
        if (STREAM_BYTE_TO_END(jdec) < 2)
            return err(jdec, "Truncated stream in misc/tables area");

        if (*jdec->stream != 0xFF)
            return err(jdec, "(misc/tables)Misaligned JDEC stream");

        if (skip_padding(jdec) == -1)
            return -1;

        if (!IS_MISC_MARKER(jdec->stream[1]))
            break;

        wrDLog("Parsing marker %x", jdec->stream[1]);
        switch (jdec->stream[1]) {
        case DQT:
            retcod = parse_DQT(jdec);
            break;
        case DHT:
            retcod = parse_DHT(jdec);
            break;
        case DRI:
            retcod = parse_DRI(jdec);
            break;
        case DAC:
            return err(jdec, "Arithmetic encoding is not supported");
            break;
        default: // ignore COM and APP0-15 markers
            retcod = skip_marker_p(jdec);
        }
        if (retcod == -1)
            return -1;
    }
    wrDLog("Parsing misc/tables region completed with success");
    return 0;
}
/* load component meta-data contained in SOF header */
static int load_component(JDEC jdec, unsigned int cidx, uint8_t *stream) {
    assert(cidx < MAX_COMPONENTS);
    Component *cmp = &jdec->components[cidx];

    cmp->Cid = stream[0];
    cmp->Hfactor = (stream[1] & 0xF0) >> 4;
    cmp->Vfactor = stream[1] & 0x0F;
    cmp->Q_table_idx = stream[2];

    wrDLog("Cid: %d | Hf: %d | Vf: %d | Q_dest: %d", cmp->Cid, cmp->Hfactor, cmp->Vfactor, cmp->Q_table_idx);

    if (cmp->Hfactor > MAX_H_V_FACTOR || cmp->Hfactor == 0)
        return err(jdec, "Invalid Hfactor for jpeg component");
    if (cmp->Vfactor > MAX_H_V_FACTOR || cmp->Vfactor == 0)
        return err(jdec, "Invalid Vfactor for jpeg component");
    if (cmp->Q_table_idx >= MAX_Q_TABLES)
        return err(jdec, "Invaild Qtable dest for jpeg component");

    return 0;
}
/* parse Start Of Frame header */
static int parse_SOF(JDEC jdec) {
    uint16_t len;
    uint8_t *F_end, *stream;
    unsigned int width, height;
    JDEC_RETCODE r;
    wrDLog("Starting SOF0 parsing");
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    assert(jdec->stream_end != NULL);

    if (skip_padding(jdec) == -1)
        return -1;
    if (jdec->stream[1] != SOF0)
        return err(jdec, "unexpected %X marker. Expecting SOF0", jdec->stream[1]);
    if (get_region_len(jdec, &len, &F_end) == -1)
        return -1;
    if (len < 8)
        return err(jdec, "Invalid SOF0 length value");

    wrDLog("Founded valid SOF0 marker with length %d", len);
    stream = jdec->stream;
    if (stream[0] != 8)
        return err(jdec, "Sampling precision must be 8");

    stream++;
    height = load16bits(stream);
    stream += 2;
    width = load16bits(stream);
    if (chkset_size(jdec, width, height) == -1)
        return -1;

    stream += 2;
    jdec->n_cmp = stream[0];
    wrDLog("Frame composed of %d components", jdec->n_cmp);

    if (jdec->n_cmp != 1 && jdec->n_cmp != 3)
        return err(jdec, "(%d) components not supported - supports 1 (Grayscale) or 3 (YUV)", jdec->n_cmp);

    if (len != 8 + 3 * jdec->n_cmp)
        return err(jdec, "Invalid len value in Frame Header");
    stream++;

    for (int i = 0; i < jdec->n_cmp; i++) {
        wrDLog("Parsing %dth component", i);
        if (load_component(jdec, i, stream) == -1)
            return -1;
        stream += 3;
    }
    if (stream != F_end)
        return err(jdec, "Processed bytes in SOF region are less than expected");
    wrDLog("SOF region parsing completed");
    if (check_HV_factors(jdec) == -1)
        return err(jdec, "Failed check on horizontal and vertical sampling factors");

    jdec->stream = stream;
    return 0;
}
/* set the huff tables destinations of a component defined in the SOS header */
static int set_components_HT(JDEC jdec, unsigned int cidx, uint8_t *stream) {
    assert(cidx < jdec->n_cmp);
    uint8_t dc_dest, ac_dest;
    Component *cmp = &jdec->components[cidx];

    if (cmp->Cid != stream[0])
        return err(jdec, "Mismatch between the id of %dth component | %d in scan | %d in frame header", cidx,
                   stream[0], cmp->Cid);

    dc_dest = (stream[1] & 0xF0) >> 4;
    if (dc_dest >= HUFF_TABLES_PER_CLASS)
        return err(jdec, "Invalid dc_huffdest (%d)", dc_dest);
    if (jdec->H_tables[HUFF_DC_TABLES][dc_dest].valid == false)
        return err(jdec, "Component request an uninitialized dc_huffdest (%d)", dc_dest);

    ac_dest = (stream[1] & 0x0F);
    if (ac_dest >= HUFF_TABLES_PER_CLASS)
        return err(jdec, "Invalid ac_huffdest (%d)", ac_dest);
    if (jdec->H_tables[HUFF_AC_TABLES][ac_dest].valid == false)
        return err(jdec, "Component request an uninitialized ac_huffdest (%d)", ac_dest);

    wrDLog("%dth component AC_huff: %d | DC_huff: %d", cidx, ac_dest, dc_dest);

    cmp->DC_huff_dest = dc_dest;
    cmp->AC_huff_dest = ac_dest;
    return 0;
}
/* parse Start Of Frame header */
static int parse_SOS(JDEC jdec) {
    uint8_t *S_end;
    uint16_t len;
    assert(jdec != NULL);
    assert(jdec->stream != NULL);
    wrDLog("Expecting the SOS marker, skipping padding...");

    if (skip_padding(jdec) == -1)
        return -1;
    if (jdec->stream[1] != SOS)
        return err(jdec, "Invalid marker %x, expected SOS", jdec->stream[1]);
    if (-1 == get_region_len(jdec, &len, &S_end))
        return -1;

    uint8_t *stream = jdec->stream;

    if (len != 3 + jdec->n_cmp * 2 + 3)
        return err(jdec, "Scan header length is not conform with the values in frame header");
    wrDLog("SOS region of length %d\n", len);

    if (jdec->n_cmp != stream[0])
        return err(jdec, "n of components in scan (%d) is different from frame header (%d)", jdec->n_cmp,
                   stream[0]);
    stream++;
    wrDLog("Parsing meta-data about components...");
    for (int i = 0; i < jdec->n_cmp; i++) {
        wrDLog("Setting component %dth huffman tables destinations", i);
        if (set_components_HT(jdec, i, stream) == -1)
            return -1;
        stream += 2;
    }

    if (stream[0] != 0)
        return err(jdec, "Start of spectral section must be 0 for Baseline DCT");
    if (stream[1] != EOF_SPEC_SECT_VAL)
        return err(jdec, "End of spectral section shall be %d for Baseline DCT", EOF_SPEC_SECT_VAL);
    if (stream[2] != 0)
        return err(jdec, "Ah and Al parameters shall be 0 in Baseline DCT");

    stream += 3;
    wrDLog("SOS parsing completed");
    if (stream != S_end)
        return err(jdec, "Mismatch of processed bytes vs declared length in SOS header");

    jdec->stream = stream;
    jdec->slice_start = stream;
    return 0;
}
/* Find End Of Input marker starding from the buffer end */
static int findEOI(JDEC jdec) {
    assert(jdec != NULL);
    wrDLog("Finding EOI marker...");
    uint8_t *p = jdec->stream_end - 2;
    while (p >= jdec->slice_start) {
        if (p[0] == 0xFF && p[1] == EOI) {
            jdec->slice_end = p;
            return 0;
        }
        p--;
    }
    return err(jdec, "Cannot find EOI marker");
}
/* parse JPEG File Interchange Format to get metadata for decoding */
static int parse_JFIF(JDEC jdec) {
    int retcd;
    assert(jdec != NULL);
    assert(jdec->stream_end == jdec->stream + jdec->stream_len);
    wrDLog("Parsing the JFIF buffer...");

    if (jdec->stream_len < 3)
        return err(jdec, "Too short buffer");
    if (jdec->stream[0] != 0xFF || jdec->stream[1] != SOI)
        return err(jdec, "Not a jdec buffer");

    jdec->stream = jdec->stream + 2;

    // parse until frame header
    if (parse_tables_area(jdec) == -1)
        return -1;
    if (parse_SOF(jdec) == -1)
        return -1;
    if (parse_tables_area(jdec) == -1)
        return -1;
    if (parse_SOS(jdec) == -1)
        return -1;
    if (check_tables(jdec) == -1)
        return -1;
    if (findEOI(jdec) == -1)
        return -1;

    wrDLog("JFIF parsing completed");
    return 0;
}
/* clean metadata to start new parsing. The other data will be cleaned/rewritten
 * only if we need it
 * */
void clean_data(JDEC jdec) {
    jdec->n_cmp = 0;
    jdec->slice_start = NULL;
    jdec->slice_end = NULL;
    jdec->decoded = false;
    jdec->Ri = 0;
    for (int i = 0; i < MAX_Q_TABLES; i++)
        jdec->Q_tables[i][0] = 0;
}
/* ----------------------------------------
 * --------- PUBLIC FUNCTIONS -------------
 * ----------------------------------------
 *  */
/* parse the headers of the buffer */
JDEC_RETCODE JDEC_parse_source(JDEC jdec, uint8_t *buf, unsigned int buff_size) {
    wrDLog("Start parsing source");
    if (jdec == NULL)
        return JDEC_ERR;

    if (buf == NULL)
        return err(jdec, "NULL buffer");

    if (buff_size == 0)
        return err(jdec, "Buffer size is 0");

    clean_data(jdec);
    jdec->stream = buf;
    jdec->stream_len = buff_size;
    jdec->stream_end = buf + buff_size;

    if (parse_JFIF(jdec) == -1)
        return JDEC_ERR;

    return JDEC_SUCCESS;
}
/* decode the actual image using the metadata in the headers */
JDEC_RETCODE JDEC_decode(JDEC jdec) {
    if (jdec == NULL)
        return JDEC_ERR;
    wrDLog("Starting hw decoding...");
    if (jdec->slice_start == NULL)
        return err(jdec, "you must parse the JFIF file before decoding");

    if (jdec->decoded == true)
        return err(jdec, "This buffer have arleady been decoded");

    if (jdec_hw_setdata(jdec) == -1)
        return err(jdec, "Cannot set buffers for vaapi jpeg decoding");

    if (jdec_hw_decode(jdec) == -1)
        return err(jdec, "Cannot decode jpeg image");

    jdec->decoded = true;
    return JDEC_SUCCESS;
}
/* set (or reset) the size of the expected images */
JDEC_RETCODE JDEC_presetsize(JDEC jdec, unsigned int w, unsigned int h) {
    if (jdec == NULL)
        return JDEC_ERR;

    wrDLog("Setting jpeg decoder image size and configurin the hw accelerator.");
    if (w > JPEG_MAX_WIDTH || h > JPEG_MAX_HEIGHT)
        return err(jdec, "Invalid width and height (%d, %d)", w, h);

    if (w % 16 != 0 || h % 16 != 0)
        return err(jdec, "Width and height must be multilples of 16");

    jdec->width = w;
    jdec->height = h;
    if (-1 == jdec_hw_config(jdec))
        return err(jdec, "Error while config the hardware api");
    return JDEC_SUCCESS;
}
JDEC_RETCODE JDEC_destroy(JDEC jdec) {
    int r = 0;
    if (jdec == NULL)
        return JDEC_ERR;

    if (jdec_hw_destroy(jdec) == -1)
        fprintf(stderr, "Error while cleaning hw jpeg decoder");

    free(jdec);
    return JDEC_SUCCESS;
}
void JDEC_perror(JDEC jdec) {
    if (jdec == NULL)
        fprintf(stderr, "JDEC error: NULL jdec\n");
    else
        fprintf(stderr, "JDEC error: %s\n", jdec->err_str);
}
JDEC JDEC_init(void) {
    JDEC jdec;
    jdec = (JDEC)calloc(1, sizeof(struct jpeg_decoder));

    if (jdec == NULL)
        return NULL;

    jdec->hw = jdec_hw_init();
    if (jdec->hw == NULL) {
        fprintf(stderr, "Cannot init jpeg hardware decoder");
        free(jdec);
        return NULL;
    }

    // de-facto fallback for missing H tables
    jdec->H_tables[HUFF_DC_TABLES][0] = HUFF_STD_Y_DC;
    jdec->H_tables[HUFF_DC_TABLES][1] = HUFF_STD_CR_DC;

    jdec->H_tables[HUFF_AC_TABLES][0] = HUFF_STD_Y_AC;
    jdec->H_tables[HUFF_AC_TABLES][1] = HUFF_STD_CR_AC;

    return jdec;
}
#ifndef NDEBUG
/* ------------------------------------------
 * ----------PUBLIC TEST FUNCTIONS-----------
 *  -----------------------------------------*/
/* save the decoded image (only for tests) */
JDEC_RETCODE JDEC_testsaveimage(JDEC jdec, const char *fn) {
    if (jdec == NULL)
        return JDEC_ERR;
    if (jdec->decoded != true)
        return err(jdec, "You should decode the image before saving it");

    if (-1 == jdec_hw_testsaveimage(jdec, fn))
        return err(jdec, "Error while saving the image");
    return JDEC_SUCCESS;
}

#endif
