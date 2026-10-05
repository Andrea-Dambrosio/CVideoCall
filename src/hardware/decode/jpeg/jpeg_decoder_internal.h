#ifndef __JDEC_INTERNAL_H
#define __JDEC_INTERNAL_H
#include "hardware/decode/jpeg/jpeg_hardware.h"
#include <stdint.h>

// see https://www.w3.org/Graphics/JPEG/itu-t81.pdf

// clang-format off

/* JFIF FILE STRUCTURE RECAP */
  
 /* SINTAX */
 /*[] indicate an optional section, ECS an entropy coded section
 * this decoder only supports ECS using huffman tables (GPU constraint)
 * Markers are indicated with their names (SOI is equivalent to 0xFFD8)
 * Stands alone markers are indicated with *.
 * Parameters are 4-8-16-32 bits. Parameters of 4 bits are coupled in one byte
 * Parameters are indicated with an uppercase letter followed by optional lowercase
 * letters.
 * Parameter type is an unsigned int.
 *
 * Note one the language. I'll use the language of the standar document of reference.
 * When we talk about a destination selector we are saying that it is a number that
 * identify a specific object (it is just an index). Usually we talk about maximum 
 * 4 of thede indexes (destination selector), becouse 4 are the physical componentsj
 * supported by the baseline encoding.
 */

 /* HIGH LEVEL STRUCTURE*/
 /* *SOI - FRAME - *EOI
 * FRAME STRUCTURE
 * [Tables/misc] - Frame Header(SOF) - Scan1 - [DNL Seg] - [Scan2] - ... - [ScanN] 
 * SCAN STRUCTURE 
 * [Tables/misc] - Scan Header(SOS) - [ECS0 - *RST0 - ... - ECS_N-1 - *RST_N-1] - ECS_N
 * Each scan contains one to four image components.
 */

 /* FRAME HEADER*/
/* Page 36 into itu-t81 (linked file)
 * We only accept SOF0 markers for this section. SOF indicate the SOF0 marker.
 * SOF - Lf - P - Y - X - Nf - Component-specification parameters
 * Lf -> Frame header length (2 byte) (8 + 3*Nf)
 * P -> Sample precision (1 byte) (8 for Sequenctial DCT Baseline)
 * Y -> Number of lines in the source image (2 byte)
 * X -> Number of samples per line 
 * Nf -> Number of image components in the frame
 * The component-specification parameters are group of 4 parameters 
 * repeated Nf times (eg. there are 4 parameters for each image component in the frame)
 * A single group is formed by these
 * Ci ->    Component identifier (1 byte)
 *          (shall be used in the scan headers to identify the component)
 * Hi ->    Horizontal sampling factor (4 bits)
 *          (relation between component x dimension with X)
 * V1 ->    Vertical sampling factor (4 bits)
 * Tqi ->   Quantization table destination selector. (4 bits) (VALUES: 0-3)
 *          Specifies one of four possible quantization table destinations 
 *          from which the quantization table to use for dequantization of DCT 
 *          coefficients of component Ci is retrieved.  
 */

/* SCAN HEADER (page 37) */
/*
 * SOS - Ls - Ns - Component-specification - Ss - Se - Ah - Al
 * SOS ->   Start of scan (2 byte)
 * Ls ->    Scan header len (2 byte)
 * Ns ->    Number of image components in scan (1 byte)
 * Component-specification
 * Cs_j ->  Scan component selector(1 byte) 
 *          Specify witch of the Nf specified in the frame parameter shall be 
 *          the jth component in the scan. Each Cs_j shall march one of the Ci values.
 *          Ordering in scan header shall follow the one in frame header.
 *          If Ns > 1 the order of interleaved components in the MCU is C1, C2 ...,
 * Td_j ->  DC entropy coding table destination selector (4 bits). (VALUES: 0-1)
 *          Specifies one of four possibile DC (two for baseline DCT) 
 *          entropy coding destination from which
 *          the entropy table for decoding DC coefficients of the Cs_j component
 *          is retrived.
 * Ta_j ->  AC entropy coding table destination selector (4 bits). (VALUES: 0-1)
 *          Specifies one of four possibile (two for baseline DCT) 
 *          DC entropy coding destination from witch 
 *          the entropy table for decoding AC coefficients of the Cs_j compoenent 
 *          is retrived.
 * Ss (1byte), Se(1byte), Ah(4 bits), Al(4 bits) are all unused in Baseline encoding.
 */

/* TABLE SPECIFICATIONS AND MISCELLANEAOUS MARKER SEGMENTS */
/* These marker sections may be present in any order without limits on the number 
 * of segments(In the locations specified into the high level structure).
 * If a table specification for a destination is founded, it shall replace
 * any previous specification for that destination, and shall be used whenever 
 * this destination is specified in the remaining scans.
 *
 * This marker segments can be of the following types:
     * Quantization-table specification
     * Huffman-table specification
     * Aritmetic conditioning table-specification (unused here)
     * Restart interval definition
     * Comment
     * Application data
 */

/* QUANTIZATION TABLE */
/* STRUCTURE:
 * DQT - Lq - Pq - Tq - Q_0 - Q_1 - ... - Q_63
 *
 * DQT  ->  Define quantization table marker (2 bytes)
 * Lq   ->  Quantization table definition length (2 bytes)
 * The following parameters can be repeated n times
 * Pq   ->  Quantization table element precision (4 bits)
 *          Specity the precision of the Q_k values.
 *          0 for 8-bit Q_k values, 1 for 16-bit.
 *          If P is 8 Pq must be 0 (our case)
 * Tq   ->  Quantization table destination identifier (4 bits)
 * Q_k  ->  Quantization table element. (Size defined by P or Pq)
 *          The kth element of 64, where 
 *          k is the index in zig-zag ordering of the coefficients.
 */

/*  HUFFMAN TABLE */
/*
 * DHT - Lh - Tc - Th - L1 - L2 - ... - L16 - Symbol length assignment
 * DHT  ->  Huffman table marker (2 bytes)
 * Lh   ->  Huffman table definition length (2 bytes)
 * The following section can be repeated n times
 * Tc   ->  Table class (4 bits).
 *          0 for DC table, 1 for AC table.
 * Th   ->  Huffman table destination identifier. (4 bits)
 *          Specifies one of four (two for baseline DCT) destination.
 * This region is used to reconstruct the huffman tree using only two linear 
 * arrays.
 * L_i  ->  Number of Huffman codes of length i. 16 lengths are allowed by the 
 *          standerd.
 * Symbol length assignment => 
 * V_1,1 - V_1,2 - ... - V_1,L1 - V_2,1 - ... - V_2,L2 - ... - ... - V_16,L16
 * 
 * V_i,j -> Value associated with the jth huffman code of length i.
 *
 * Once a Huffman table has been defined for a particular destination, it 
 * replaces the previous tables stored in that destination.
 */

/* NOTE FOR THIS SPECIFIC IMPLEMENTATION
 * This jpes decoder is supposed to work with USB camera devices. The 
 * previous documentation show almost all the features of the JFIF file format.
 * To do a fast, simple and easy maintaniable JPEG decoder we will only suppport the 
 * JFIF file format (and JPEG type of encoding) supported by the UVC (USB Video Class) 
 * protocol and by the hardware acceleration apis. 
 * So we do not need a decoder that can manage all the 
 * possibile JFIF formats and all the JPEG encoding techniques, when the USB standard
 * only allows for a specific format and encoding and when the GPUs only support 
 * one type of encoding (mostly).
 * 
 * ENCODING STANDARD:
 * These standards only support the Baseline Sequential DCT, so all the SOF(1-15)
 * markers are not supported. We only decode frames with SOF0 marker. This will 
 * specify the values of some parameters through the file.
 *
 * COMPONENTS:
 * The components are only 3. Y, Cb, Cr
 *
 * SCANS:
 * Only a single interlived Scan is supported.
 *
 * HUFFMAN TABLES:
 * Usually 4 huffman tables are used, 2 for the Y component (DC and AC values)
 * and 2 for the Cb,Cr components (DC and AC values).
 * Some entry-level cameras may use only 2 huffman tables (one for DC and one for AC).
 *
 *
 * FOR THE FUTURE:
 * In the future the cameras will not start to use more powerful JPEG encodig techniques,
 * but for the video streming we are moving towards the H.264, HVEC and AV1. So this 
 * work for JPEG is done only one time in a lifetime (for this project), i hope.
 *
 * FOR THE UVC STANDARD:
 * https://www.usb.org/document-library/video-class-v15-document-set
 *
 */

// clang-format on

#define MAX_COMPONENTS 3 // only YUV components

#define JDEC_ERRSTRSIZE 128
#define MAX_Q_TABLES MAX_COMPONENTS
#define Q_Nvalues 64

#define HUFF_TABLE_CLASSES 2
#define HUFF_TABLES_PER_CLASS 2
#define HUFF_MAX_P_LEN 16
#define HUFF_MAX_VALUES 256 // HUFF_MAX_P_LEN * 2^sizeof(bits[i])

#define HUFF_DC_TABLES 0
#define HUFF_AC_TABLES 1

#define DQT_PRECISION_MASK 0xF0
#define DQT_INDEX_MASK 0x0F
#define HTABLE_CLASS_MASK 0xF0
#define HTABLE_DEST_MASK 0x0F

#define JPEG_MAX_WIDTH 2048
#define JPEG_MAX_HEIGHT 2048

#define MIN_SOF_LEN 14

#define MAX_H_V_FACTOR 4
#define EOF_SPEC_SECT_VAL 63

typedef struct {
    // bits[i] is the number of parameters of length i+1 in the table
    uint8_t bits[HUFF_MAX_P_LEN];
    uint8_t huffval[HUFF_MAX_VALUES];
    bool valid;
} H_table;

typedef struct {
    uint8_t Hfactor;
    uint8_t Vfactor;
    uint8_t Q_table_idx;
    uint8_t Cid;
    uint8_t DC_huff_dest;
    uint8_t AC_huff_dest;
} Component;

struct jpeg_decoder {
    // --- Parsing hotpath ---
    uint8_t *stream;
    uint8_t *stream_end;
    unsigned int stream_len;

    // --- Hardware setup hotpath ---
    uint8_t *slice_start;
    uint8_t *slice_end;

    unsigned int width, height;
    uint16_t Ri;
    uint8_t n_cmp;
    bool decoded;

    Component components[MAX_COMPONENTS];

    JDEC_HW hw;

    // --- Tables ---
    // with Baseline DCT the quantization values are on 8 bits
    uint8_t Q_tables[MAX_Q_TABLES][Q_Nvalues];
    // the single huffman tables should by allocated manually
    H_table H_tables[HUFF_TABLE_CLASSES][HUFF_TABLES_PER_CLASS];

    // --- Cold / Error paths ---
    char err_str[JDEC_ERRSTRSIZE];
};
// see https://www.w3.org/Graphics/JPEG/itu-t81.pdf (page 32)
// Checking only for markers that are production-used in this context
enum std_markers : uint8_t {
    DQT = 0xDB,  /* Define Quantization Table */
    SOF0 = 0xC0, /* Start of Frame, Baseline DCT*/
    DHT = 0xC4,  /* Huffman Table */
    SOI = 0xD8,  /* Start of Image */
    SOS = 0xDA,  /* Start of Scan */
    RST = 0xD0,  /* Reset Marker d0 -> .. */
    RST7 = 0xD7, /* Reset Marker .. -> d7 */
    EOI = 0xD9,  /* End of Image */
    DRI = 0xDD,  /* Define Restart Interval */
};
enum unused_markers : uint8_t {
    COM = 0xFE,
    APP0 = 0xE0, // actually we ignore from APP0 to APP15
    APP15 = 0xEF,
    DAC = 0xCC
};
// Check if the byte x is a SOF but hte frame is not Baseline DCT
// 0xCF is the last SOF marker type (SOF15)
#define IS_SOF(x) (x >= SOF0 && x <= 0xCF)
#define IS_UNVALID_SOF(x) (x > SOF0 && x <= 0xCF)
#define IS_MISC_MARKER(x)                                                                                    \
    (x == DHT || x == DQT || (x >= APP0 && x <= APP15) || x == COM || x == DAC || x == DRI)
#define STREAM_BYTE_TO_END(jdec) (jdec->stream_end - jdec->stream)

#define load16bits(x) ((x[0] << 8) | x[1])

#endif
