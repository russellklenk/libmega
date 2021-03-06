/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines the interface to a JPEG-like lossy image compression
/// engine. The primary difference is that the color space is YCoCg, and the
/// sub-sampling ratio is fixed at 4:2:0. The compressor operates on blocks of
/// 16x16 pixels and supports images with alpha channels. For more information,
/// see the Real-Time Texture Streaming & Decompression paper, available at:
///   http://mrelusive.com/publications/papers/
/// and also the public-domain JPEG compressor code, available at:
///   https://jpeg-compressor.googlecode.com/svn/trunk/
/// and also Fabian 'ryg' Giesen's blog postings about Bink 2 IDCT at:
///   http://fgiesen.wordpress.com/2013/11/04/bink-2-2-integer-dct-design-part-1
///   http://fgiesen.wordpress.com/2013/11/10/bink-2-2-integer-dct-design-part-2
/// and also this source file, when referencing cbloom code:
///   http://www.ict.kth.se/courses/IL2226/Spring2013/Lab/Lab%20II/Lab_II.c
/// and also this paper on YCoCg color space conversions for compression:
///   Lifting-Based Reversable Color Transformations for Image Compression
///   http://research.microsoft.com/pubs/102040/2008_colortransforms_malvarsullivansrinivasan.pdf
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "imutils.hpp"

/*////////////////
//  Data Types  //
////////////////*/
/// @summary A lookup table of array indices used to access DCT coefficients
/// in zig-zag order, post-FDCT. Accessing the coefficients in zig-zag order
/// increases the length of runs of zeroes.
static const size_t ZigZag[64] =
{
     0,   1,   8,  16,    9,   2,   3,  10,
    17,  24,  32,  25,   18,  11,   4,   5,
    12,  19,  26,  33,   40,  48,  41,  34,
    27,  20,  13,   6,    7,  14,  21,  28,
    35,  42,  49,  56,   57,  50,  43,  36,
    29,  22,  15,  23,   30,  37,  44,  51,
    58,  59,  52,  45,   38,  31,  39,  46,
    53,  60,  61,  54,   47,  55,  62,  63
};

/// @summary A lookup table of array indices used to access DCT coefficients
/// in normal order, post-FDCT.
static const size_t NoZigZag[64] =
{
     0,   1,   2,   3,    4,   5,   6,   7,
     8,   9,  10,  11,   12,  13,  14,  15,
    16,  17,  18,  19,   20,  21,  22,  23,
    24,  25,  26,  27,   28,  29,  30,  31,
    32,  33,  34,  35,   36,  37,  38,  39,
    40,  41,  42,  43,   44,  45,  46,  47,
    48,  49,  50,  51,   52,  53,  54,  55,
    56,  57,  58,  59,   60,  61,  62,  63
};

/// @summary The base quantization coefficients for the luma channel, as
/// specified in the JPEG standard.
static const int16_t JPEGLumaQuant[64] =
{
    16,  11,  12,  14,   12,  10,  16,  14,
    13,  14,  18,  17,   16,  19,  24,  40,
    26,  24,  22,  22,   24,  49,  35,  37,
    29,  40,  58,  51,   61,  60,  57,  51,
    56,  55,  64,  72,   92,  78,  64,  68,
    87,  69,  55,  56,   80, 109,  81,  87,
    95,  98, 103, 104,  103,  62,  77, 113,
    121,112, 100, 120,   92, 101, 103,  99
};

/// @summary The base quantization coefficients for the chroma channels, as
/// specified in the JPEG standard.
static const int16_t JPEGChromaQuant[64] =
{
    17,  18,  18,  24,   21,  24,  47,  26,
    26,  47,  99,  66,   56,  66,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99,
    99,  99,  99,  99,   99,  99,  99,  99
};

/// @summary AA&N scale factor values. The scale factors are computed as:
/// AANScaleFactor[0] = 1.0
/// AANScaleFactor[k] = cos(k*PI/16) * sqrt(2) for k in [1, 7]
static const float AANScaleFactor_float[8] =
{
    1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
    1.0f, 0.785694958f, 0.541196100f, 0.275899379f
};

/// @summary AA&N scale factors for inverse DCT. These are the values that
/// are output if you specify a Qtable = NULL (or all 1.0f) to the function
/// scaled_qtable_float(), and result in a unitary transform.
static const float AAN_IDCT_Factors_float[64] =
{
    0.12500f, 0.17338f, 0.16332f, 0.14698f, 0.12500f, 0.09821f, 0.06765f, 0.03449f,
    0.17338f, 0.24048f, 0.22653f, 0.20387f, 0.17338f, 0.13622f, 0.09383f, 0.04784f,
    0.16332f, 0.22653f, 0.21339f, 0.19204f, 0.16332f, 0.12832f, 0.08839f, 0.04506f,
    0.14698f, 0.20387f, 0.19204f, 0.17284f, 0.14698f, 0.11548f, 0.07955f, 0.04055f,
    0.12500f, 0.17338f, 0.16332f, 0.14698f, 0.12500f, 0.09821f, 0.06765f, 0.03449f,
    0.09821f, 0.13622f, 0.12832f, 0.11548f, 0.09821f, 0.07716f, 0.05315f, 0.02710f,
    0.06765f, 0.09383f, 0.08839f, 0.07955f, 0.06765f, 0.05315f, 0.03661f, 0.01866f,
    0.03449f, 0.04784f, 0.04506f, 0.04055f, 0.03449f, 0.02710f, 0.01866f, 0.00952f
};

/// @summary AA&N scale factors for forward DCT. These are the values that
/// are output if you specify a Qtable = NULL (or all 1.0f) to the function
/// scaled_qtable_float(), and result in a unitary transform.
static const float AAN_FDCT_Factors_float[64] =
{
    0.12500f, 0.09012f, 0.09567f, 0.10630f, 0.12500f, 0.15909f, 0.23097f, 0.45306f,
    0.09012f, 0.06497f, 0.06897f, 0.07664f, 0.09012f, 0.11470f, 0.16652f, 0.32664f,
    0.09567f, 0.06897f, 0.07322f, 0.08136f, 0.09567f, 0.12177f, 0.17678f, 0.34676f,
    0.10630f, 0.07664f, 0.08136f, 0.09040f, 0.10630f, 0.13530f, 0.19642f, 0.38530f,
    0.12500f, 0.09012f, 0.09567f, 0.10630f, 0.12500f, 0.15909f, 0.23097f, 0.45306f,
    0.15909f, 0.11470f, 0.12177f, 0.13530f, 0.15909f, 0.20249f, 0.29397f, 0.57664f,
    0.23097f, 0.16652f, 0.17678f, 0.19642f, 0.23097f, 0.29397f, 0.42678f, 0.83715f,
    0.45306f, 0.32664f, 0.34676f, 0.38530f, 0.45306f, 0.57664f, 0.83715f, 1.64213f
};

/// @summary The Contrast Sensitivity Function coefficients for the luma
/// channel, for the base JPEG luma quantization table JPEGLumaQuant. They are
/// calculated from the quantization table Q using  CSF[i] = Q[0] / Q[i] and
/// represent the ratio of the AC coefficient to the DC coefficient. The values
/// in this table are also stored in zig-zag order.
static const float CSFLuma_float[64] =
{
    1.000000f, 1.454545f, 1.600000f, 1.000000f, 0.666667f, 0.400000f, 0.313726f, 0.262295f,
    1.333333f, 1.333333f, 1.142857f, 0.842105f, 0.615385f, 0.275862f, 0.266667f, 0.290909f,
    1.142857f, 1.230769f, 1.000000f, 0.666667f, 0.400000f, 0.280702f, 0.231884f, 0.285714f,
    1.142857f, 0.941176f, 0.727273f, 0.551724f, 0.313726f, 0.183908f, 0.200000f, 0.258065f,
    0.888889f, 0.727273f, 0.432432f, 0.285714f, 0.235294f, 0.146789f, 0.155340f, 0.207792f,
    0.666667f, 0.457143f, 0.290909f, 0.250000f, 0.197531f, 0.153846f, 0.141593f, 0.173913f,
    0.326531f, 0.250000f, 0.205128f, 0.183908f, 0.155340f, 0.132231f, 0.133333f, 0.158416f,
    0.222222f, 0.173913f, 0.168421f, 0.163265f, 0.142857f, 0.160000f, 0.155340f, 0.161616f,
};

/// @summary The Contrast Sensitivity Function coefficients for the chroma
/// channel, for the base JPEG luma quantization table JPEGChromaQuant. They
/// are calculated from the quantization table Q using  CSF[i] = Q[0] / Q[i]
/// and represent the ratio of the AC coefficient to the DC coefficient. The
/// values in this table are also stored in zig-zag order.
static const float CSFChroma_float[64] =
{
    1.000000f, 0.944444f, 0.708333f, 0.361702f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.944444f, 0.809524f, 0.653846f, 0.257576f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.708333f, 0.653846f, 0.303571f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.361702f, 0.257576f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
    0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f, 0.171717f,
};

/// @summary The result of multiplying the AAN_FDCT_Factors against CSFLuma.
static const float FDCTLuma_float[64] =
{
    0.125000f, 0.131084f, 0.153072f, 0.106300f, 0.083333f, 0.063636f, 0.072461f, 0.118835f,
    0.120160f, 0.086627f, 0.078823f, 0.064539f, 0.055458f, 0.031641f, 0.044405f, 0.095023f,
    0.109337f, 0.084886f, 0.073220f, 0.054240f, 0.038268f, 0.034181f, 0.040992f, 0.099074f,
    0.121486f, 0.072132f, 0.059171f, 0.049876f, 0.033349f, 0.024883f, 0.039284f, 0.099432f,
    0.111111f, 0.065542f, 0.041371f, 0.030371f, 0.029412f, 0.023353f, 0.035879f, 0.094142f,
    0.106060f, 0.052434f, 0.035424f, 0.033825f, 0.031425f, 0.031152f, 0.041624f, 0.100285f,
    0.075419f, 0.041630f, 0.036263f, 0.036123f, 0.035879f, 0.038872f, 0.056904f, 0.132618f,
    0.100680f, 0.056807f, 0.058402f, 0.062906f, 0.064723f, 0.092262f, 0.130043f, 0.265394f
};

/// @summary The result of multiplying the AAN_FDCT_Factors against CSFChroma.
static const float FDCTChroma_float[64] =
{
    0.125000f, 0.085113f, 0.067766f, 0.038449f, 0.021465f, 0.027318f, 0.039661f, 0.077798f,
    0.085113f, 0.052595f, 0.045096f, 0.019741f, 0.015475f, 0.019696f, 0.028594f, 0.056090f,
    0.067766f, 0.045096f, 0.022227f, 0.013971f, 0.016428f, 0.020910f, 0.030356f, 0.059545f,
    0.038449f, 0.019741f, 0.013971f, 0.015523f, 0.018254f, 0.023233f, 0.033729f, 0.066163f,
    0.021465f, 0.015475f, 0.016428f, 0.018254f, 0.021465f, 0.027318f, 0.039661f, 0.077798f,
    0.027318f, 0.019696f, 0.020910f, 0.023233f, 0.027318f, 0.034771f, 0.050480f, 0.099019f,
    0.039661f, 0.028594f, 0.030356f, 0.033729f, 0.039661f, 0.050480f, 0.073285f, 0.143753f,
    0.077798f, 0.056090f, 0.059545f, 0.066163f, 0.077798f, 0.099019f, 0.143753f, 0.281982f
};

/// @summary Determines the smaller of two values.
/// @param a The first value.
/// @param b The second value.
/// @return The smaller of the two values.
static inline int32_t qmin(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

/// @summary Determines the larger of two values.
/// @param a The first value.
/// @param b The second value.
/// @return The larger of the two values.
static inline int32_t qmax(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

/// @summary Clamp a value to the range [0, 255] and return it as a byte.
/// @param v The value to clamp.
/// @return The byte in [0, 255].
static inline uint8_t clamp(int32_t v)
{
    return (uint8_t) ((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

/// @summary Convert a block of 16x16 RGBA pixels to YCoCgA format. The alpha
/// channel is extracted and stored in a separate output buffer.
/// @param ycocg The 768 element output buffer for storing the YCoCg channels.
/// The YCoCg data has a range of [-255, 255] for any 8-bit unsigned RGB tuple.
/// @param alpha The 256 byte output buffer for storing the alpha channel.
/// @param rgba The 1024 byte input buffer of 256 pixels in RGBA8 format.
static void rgba_to_ycocga(
    int16_t       * restrict ycocg,
    uint8_t       * restrict alpha,
    uint8_t const * restrict rgba)
{
    uint8_t const * RGBA   = (uint8_t const*) rgba;
    int16_t       * YCoCg  = (int16_t      *) ycocg;
    uint8_t       * A      = (uint8_t      *) alpha;
    for (size_t i = 0; i < 256; ++i)
    {
        int16_t r = *RGBA++;
        int16_t g = *RGBA++;
        int16_t b = *RGBA++;
        *A++      = *RGBA++;
        int16_t Co= r  -  b;
        int16_t t = b  + (Co >> 1);
        int16_t Cg= g  -  t;
        int16_t Y = t  + (Cg >> 1);
        *YCoCg++  = Y;
        *YCoCg++  = Co;
        *YCoCg++  = Cg;
    }
}

/// @summary Convert a block of 16x16 YCoCgA pixels to RGBA format. The alpha
/// channel is stored separately from the luma and chroma channels.
/// @param rgba The 1024 byte buffer to which RGBA values will be written.
/// @param ycocg The 768 byte input buffer storing the YCoCg channels. The
/// YCoCg data has a range of [-255, 255] for any 8-bit unsigned RGB tuple; the
/// Y channel is always in [0, 255] while Co and Cg are in [-255, 255].
/// @param alpha The 256 byte buffer storing the alpha channel.
static void ycocga_to_rgba(
    uint8_t       *          rgba,
    int16_t const * restrict ycocg,
    uint8_t const * restrict alpha)
{
    int16_t const *YCoCg  = (int16_t const*) ycocg;
    uint8_t const *A      = (uint8_t const*) alpha;
    uint8_t       *RGBA   = (uint8_t      *) rgba;
    for (size_t i = 0; i < 256; ++i)
    {
        int16_t y =*YCoCg++;
        int16_t co=*YCoCg++;
        int16_t cg=*YCoCg++;
        int16_t t = y  - (cg >> 1);
        int16_t g = cg +  t;
        int16_t b = t  - (co >> 1);
        int16_t r = b  +  co;
        *RGBA++   = clamp(r);
        *RGBA++   = clamp(g);
        *RGBA++   = clamp(b);
        *RGBA++   = *A++;
    }
}

/// @summary Determines the border color to use at the edge of a tile.
/// @param config The tiler configuration.
/// @param edge A pointer to the edge pixel.
/// @return The packed RGBA8 color to use for the border.
static inline uint32_t sample_border(image_tiler_config_t const *config, uint32_t const *edge)
{
    switch (config->BorderMode)
    {
        case BORDER_CLAMP_TO_EDGE:  return *edge;
        case BORDER_CONSTANT_COLOR: return config->BorderColor;
        default: break;
    }
    return 0;
}

/// @summary Reads one row of pixels for a tile from the source image, applying
/// borders to the left and right edges, and padding to the right edge.
/// @param row_buf A buffer capable of holding one row of pixels in the tile.
/// @param src_row Pointer to the first pixel in the source image to copy.
/// @param src_num The number of pixels to copy from the source image.
/// @param pad_right The number of pixels of padding to apply to the right side
/// of the row, if the tile extends past the right edge of the source image.
/// @param config The tiler configuration, specifying how to sample borders.
static void read_row(
    uint32_t       * restrict   row_buf,
    uint32_t const * restrict   src_row,
    size_t                      src_num,
    size_t                      pad_right,
    image_tiler_config_t const *config)
{
    uint32_t const *left_edge    = src_row;
    uint32_t const *right_edge   = src_row + (src_num - 1);

    // write the left-side border.
    uint32_t left_border = sample_border(config, left_edge);
    for (size_t i = 0; i < config->BorderSize; ++i)
        *row_buf++= left_border;

    // copy the source data from the image.
    for (size_t i = 0; i < src_num; ++i)
        *row_buf++= *src_row++;

    // add padding to the right side of the image.
    uint32_t right_pixel = *right_edge;
    for (size_t i = 0; i < pad_right; ++i)
        *row_buf++= right_pixel;

    // write the right-side border.
    uint32_t right_border= sample_border(config, right_edge);
    for (size_t i = 0; i < config->BorderSize; ++i)
        *row_buf++= right_border;
}

/// @summary Reads one row of pixels for a tile from the source image, applying
/// borders to the left and right edges, and padding to the right edge, for
/// rows that are part of the top and bottom borders of the image.
/// @param row_buf A buffer capable of holding one row of pixels in the tile.
/// @param row_num The number of pixels in a single row of the tile.
/// @param src_row Pointer to the first pixel in the source image to copy.
/// @param src_num The number of pixels to copy from the source image.
/// @param pad_right The number of pixels of padding to apply to the right side
/// of the row, if the tile extends past the right edge of the source image.
/// @param config The tiler configuration, specifying how to sample borders.
static void read_row_border(
    uint32_t       * restrict   row_buf,
    size_t                      row_num,
    uint32_t const * restrict   src_row,
    size_t                      src_num,
    size_t                      pad_right,
    image_tiler_config_t const *config)
{
    switch (config->BorderMode)
    {
        case BORDER_CLAMP_TO_EDGE:
            {
                // the standard read_row() already does the right thing.
                read_row(row_buf, src_row, src_num, pad_right, config);
            }
            break;

        case BORDER_CONSTANT_COLOR:
            {
                // duplicate the constant color across the entire row.
                for (size_t i = 0; i < row_num; ++i)
                    *row_buf++= config->BorderColor;
            }
            break;
    }
}

/// @summary Generates a set of Contrast Sensitivity Function coefficients from
/// an existing quantization table, and places the output coefficients into the
/// zig-zag order to increase the length of zero-runs in the quantized DCT
/// coefficients.
/// @param CSFtable A 64-element array to store the CSF coefficients.
/// @param Qtable A 64-element array of quantization coefficients such as those
/// generated by the quantization_table() function.
static void csf_from_qtable(
    float         * restrict CSFtable,
    int16_t const * restrict Qtable)
{
    #define DCTSIZE 8U
    for (size_t i = 0; i < DCTSIZE * DCTSIZE; ++i)
    {
        CSFtable[i] = (float) Qtable[0] / (float) Qtable[i];
    }
}

/// @summary Calculates quantization tables for FDCT and IDCT that account for
/// the scaling introduced by the AA&N methods. The FDCT and IDCT each scale
/// 8-bit input values by 2^3 (=8); this function includes a baked in de-scale
/// by 8 for use after the FDCT and a before using a value as input to the IDCT.
/// @param Qidct A 64-element array that will store the scaling and
/// quantization coefficients for use with the IDCT method.
/// @param Qfdct A 64-element array that will store the scaling and
/// quantization coefficients for use with the FDCT method.
/// @param CSFTable A 64-element array specifying the Contrast Sensitivity
/// Function coefficients generated from the quantization table such as those
/// generated from the csf_from_qtable() function.
static void scaled_qtable_float(
    float       * restrict Qidct,
    float       * restrict Qfdct,
    float const * restrict CSFtable)
{
    #define DCTSIZE 8U
    float const * restrict AAN = AANScaleFactor_float;
    size_t  i = 0;
    for (size_t r = 0; r < DCTSIZE; ++r)
    {
        for (size_t c = 0; c < DCTSIZE; ++c)
        {
            float  q    = CSFtable ? CSFtable[i] : 1.0f;
            double aans = AAN[r] * AAN[c];
            double qaan = aans   * q;
            Qidct[i]    = (float) (qaan / 8.0);
            Qfdct[i]    = (float) (1.0  / (qaan * 8.0));
            ++i;
        }
    }
}

/// @summary Calculates quantization tables for FDCT and IDCT that account for
/// the scaling introduced by the FDCT and IDCT. The FDCT and IDCT each scale
/// input values by 2^3 (=8); this function includes a baked in de-scale by 8
/// for use after the DCT (divide each DCT coefficient by the scaled qtable
/// value) and before the IDCT (multiply each quantized DCT coefficient by the
/// scaled qtable value). The scaled quantization tables are output using
/// zig-zag ordering to increase the length of zero-runs.
/// @param Qidct A 64-element array that will store the scaling and
/// quantization coefficients for use with the IDCT method.
/// @param Qfdct A 64-element array that will store the scaling and
/// quantization coefficients for use with the FDCT method.
/// @param Qbase A 64-element array specifying the quantization coefficients
/// as output by the quantization_table_base() function. Values in Qbase are in
/// the range [1, 255].
static void scaled_qtable_int16(
    int16_t       * restrict Qidct,
    int16_t       * restrict Qfdct,
    int16_t const * restrict Qbase)
{
    #define DCTSIZE 8U
    for (size_t i = 0; i < DCTSIZE * DCTSIZE; ++i)
    {
        Qidct[i] = Qbase[i]; // multiplier
        Qfdct[i] = Qbase[i]; // divisor
    }
}

/// @summary Performs a 2D forward DCT on an 8x8 block of a single channel.
/// This is performed after the RGBA pixels are converted to YCoCg, and after
/// subsampling to 4:2:0, so this routine is called four times for luma and
/// one time for each of the chroma channels. The output coefficients are not
/// quantized, and are scaled by a factor of 8 to the range [-8192, +8192].
/// @param dst A 64-element array to be filled with DCT coefficients.
/// @param src A 64-element array representing an 8x8 block of input samples.
static void fdct8x8f_base(float * restrict dst, float const * restrict src)
{
    #define DCTSIZE     8U
    #define f13         0.707106781f
    #define f05         0.382683433f
    #define f02         0.541196100f
    #define f04         1.306563965f

    float const *inp = (float const*) src;
    float       *out = (float*) dst;

    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process rows in the input.
        float t00 = inp[0] + inp[7];
        float t07 = inp[0] - inp[7];
        float t01 = inp[1] + inp[6];
        float t06 = inp[1] - inp[6];
        float t02 = inp[2] + inp[5];
        float t05 = inp[2] - inp[5];
        float t03 = inp[3] + inp[4];
        float t04 = inp[3] - inp[4];
        float t10 = t00    + t03;
        float t13 = t00    - t03;
        float t11 = t01    + t02;
        float t12 = t01    - t02;
        out[0]    = t10    + t11;
        out[4]    = t10    - t11;
        float z01 =(t12    + t13) * f13;
        out[2]    = t13    + z01;
        out[6]    = t13    - z01;
        t10       = t04    + t05;
        t11       = t05    + t06;
        t12       = t06    + t07;
        float z05 =(t10    - t12) * f05;
        float z02 = f02    * t10  + z05;
        float z04 = f04    * t12  + z05;
        float z03 = f13    * t11;
        float z11 = t07    + z03;
        float z13 = t07    - z03;
        out[5]    = z13    + z02;
        out[3]    = z13    - z02;
        out[1]    = z11    + z04;
        out[7]    = z11    - z04;
        out      += DCTSIZE;
        inp      += DCTSIZE;
    }

    out = dst;
    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process columns in the input.
        float t00 = out[DCTSIZE * 0] + out[DCTSIZE * 7];
        float t07 = out[DCTSIZE * 0] - out[DCTSIZE * 7];
        float t01 = out[DCTSIZE * 1] + out[DCTSIZE * 6];
        float t06 = out[DCTSIZE * 1] - out[DCTSIZE * 6];
        float t02 = out[DCTSIZE * 2] + out[DCTSIZE * 5];
        float t05 = out[DCTSIZE * 2] - out[DCTSIZE * 5];
        float t03 = out[DCTSIZE * 3] + out[DCTSIZE * 4];
        float t04 = out[DCTSIZE * 3] - out[DCTSIZE * 4];
        float t10 = t00 + t03;
        float t13 = t00 - t03;
        float t11 = t01 + t02;
        float t12 = t01 - t02;
        out[DCTSIZE*0]  = t10   + t11;
        out[DCTSIZE*4]  = t10   - t11;
        float z01 =(t12 + t13)  * f13;
        out[DCTSIZE*2]  = t13   + z01;
        out[DCTSIZE*6]  = t13   - z01;
        t10       = t04 + t05;
        t11       = t05 + t06;
        t12       = t06 + t07;
        float z05 =(t10 - t12)  * f05;
        float z02 = f02 * t10   + z05;
        float z04 = f04 * t12   + z05;
        float z03 = f13 * t11;
        float z11 = t07 + z03;
        float z13 = t07 - z03;
        out[DCTSIZE*5]  = z13   + z02;
        out[DCTSIZE*3]  = z13   - z02;
        out[DCTSIZE*1]  = z11   + z04;
        out[DCTSIZE*7]  = z11   - z04;
        out++;
    }
}

/// @summary Performs a 2D forward DCT on an 8x8 block of a single channel.
/// This is performed after the RGBA pixels are converted to YCoCg, and after
/// subsampling to 4:2:0, so this routine is called four times for luma and
/// one time for each of the chroma channels. The output coefficients are not
/// quantized, and are scaled by a factor of 8. This integer DCT is not
/// perfectly reversable. For more information, see the following:
/// http://fgiesen.wordpress.com/2013/11/04/bink-2-2-integer-dct-design-part-1/
/// http://fgiesen.wordpress.com/2013/11/10/bink-2-2-integer-dct-design-part-2/
/// https://github.com/rygorous/dct_blog/blob/master/bink_dct_B2.m
/// Also, this DCT is not a derivative of AA&N, so the q-tables are computed
/// differently than they are for the AA&N method.
/// @param dst A 64-element array to be filled with DCT coefficients.
/// @param src A 64-element array representing an 8x8 block of input samples.
static void fdct8x8i_base(int16_t * restrict dst, int16_t const * restrict src)
{
    #define DCTSIZE 8U

    int16_t const *inp = (int16_t const*) src;
    int16_t       *out = (int16_t*) dst;

    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process rows in the input.
        int32_t i0 =  inp[0];
        int32_t i1 =  inp[1];
        int32_t i2 =  inp[2];
        int32_t i3 =  inp[3];
        int32_t i4 =  inp[4];
        int32_t i5 =  inp[5];
        int32_t i6 =  inp[6];
        int32_t i7 =  inp[7];
        int32_t a0 =  i0  +  i7;
        int32_t a1 =  i1  +  i6;
        int32_t a2 =  i2  +  i5;
        int32_t a3 =  i3  +  i4;
        int32_t a4 =  i0  -  i7;
        int32_t a5 =  i1  -  i6;
        int32_t a6 =  i2  -  i5;
        int32_t a7 =  i3  -  i4;
        int32_t b0 =  a0  +  a3;
        int32_t b1 =  a1  +  a2;
        int32_t b2 =  a0  -  a3;
        int32_t b3 =  a1  -  a2;
        int32_t c0 =  b0  +  b1;
        int32_t c1 =  b0  -  b1;
        int32_t c2 =  b2  + (b2 >> 2) + (b3 >> 1);
        int32_t c3 = (b2 >> 1)  - b3  - (b3 >> 2);
        int32_t b4 = (a7 >> 2)  + a4  + (a4 >> 2) - (a4 >> 4);
        int32_t b7 = (a4 >> 2)  - a7  - (a7 >> 2) + (a7 >> 4);
        int32_t b5 =  a5        + a6  - (a6 >> 2) - (a6 >> 4);
        int32_t b6 =  a6        - a5  + (a5 >> 2) + (a5 >> 4);
        int32_t c4 =  b4  +  b5;
        int32_t c5 =  b4  -  b5;
        int32_t c6 =  b6  +  b7;
        int32_t c7 =  b6  -  b7;
        int32_t d4 =  c4;
        int32_t d5 =  c5  +  c7;
        int32_t d6 =  c5  -  c7;
        int32_t d7 =  c6;
        out[0]  = (int16_t) c0;
        out[1]  = (int16_t) d4;
        out[2]  = (int16_t) c2;
        out[3]  = (int16_t) d6;
        out[4]  = (int16_t) c1;
        out[5]  = (int16_t) d5;
        out[6]  = (int16_t) c3;
        out[7]  = (int16_t) d7;
        out    +=  DCTSIZE;
        inp    +=  DCTSIZE;
    }

    out = dst;
    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process columns in the input.
        int32_t i0  = out[DCTSIZE * 0];
        int32_t i1  = out[DCTSIZE * 1];
        int32_t i2  = out[DCTSIZE * 2];
        int32_t i3  = out[DCTSIZE * 3];
        int32_t i4  = out[DCTSIZE * 4];
        int32_t i5  = out[DCTSIZE * 5];
        int32_t i6  = out[DCTSIZE * 6];
        int32_t i7  = out[DCTSIZE * 7];
        int32_t a0  = i0  +  i7;
        int32_t a1  = i1  +  i6;
        int32_t a2  = i2  +  i5;
        int32_t a3  = i3  +  i4;
        int32_t a4  = i0  -  i7;
        int32_t a5  = i1  -  i6;
        int32_t a6  = i2  -  i5;
        int32_t a7  = i3  -  i4;
        int32_t b0  = a0  +  a3;
        int32_t b1  = a1  +  a2;
        int32_t b2  = a0  -  a3;
        int32_t b3  = a1  -  a2;
        int32_t c0  = b0  +  b1;
        int32_t c1  = b0  -  b1;
        int32_t c2  = b2  + (b2 >> 2) + (b3 >> 1);
        int32_t c3  =(b2 >> 1)  - b3  - (b3 >> 2);
        int32_t b4  =(a7 >> 2)  + a4  + (a4 >> 2) - (a4 >> 4);
        int32_t b7  =(a4 >> 2)  - a7  - (a7 >> 2) + (a7 >> 4);
        int32_t b5  = a5        + a6  - (a6 >> 2) - (a6 >> 4);
        int32_t b6  = a6        - a5  + (a5 >> 2) + (a5 >> 4);
        int32_t c4  = b4  +  b5;
        int32_t c5  = b4  -  b5;
        int32_t c6  = b6  +  b7;
        int32_t c7  = b6  -  b7;
        int32_t d4  = c4;
        int32_t d5  = c5  +  c7;
        int32_t d6  = c5  -  c7;
        int32_t d7  = c6;
        out[DCTSIZE * 0]  = (int16_t) c0;
        out[DCTSIZE * 1]  = (int16_t) d4;
        out[DCTSIZE * 2]  = (int16_t) c2;
        out[DCTSIZE * 3]  = (int16_t) d6;
        out[DCTSIZE * 4]  = (int16_t) c1;
        out[DCTSIZE * 5]  = (int16_t) d5;
        out[DCTSIZE * 6]  = (int16_t) c3;
        out[DCTSIZE * 7]  = (int16_t) d7;
        out++;
    }
}

/// @summary Performs a 2D forward DCT on an 8x8 block of a single channel.
/// This is performed after the RGBA pixels are converted to YCoCg, and after
/// subsampling to 4:2:0, so this routine is called four times for luma and
/// one time for each of the chroma channels. Additionally, the output DCT
/// coefficients are quantized and descaled; the output coefficient values have
/// the range [-1024, 1023] for 8-bit input values.
/// @param dst A 64-element array to be filled with quantized DCT coefficients.
/// @param src A 64-element array representing an 8x8 block of input.
/// @param quant The 64-element scaled quantization coefficient array, as
/// output by the scaled_qtable_float() function.
static void fdct8x8fq_base(
    float       * restrict dst,
    float const * restrict src,
    float const * restrict quant)
{
    #define DCTSIZE    8U
    #define f13        0.707106781f
    #define f05        0.382683433f
    #define f02        0.541196100f
    #define f04        1.306563965f

    float const *inp = (float const*) src;
    float       *out = (float*) dst;

    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process rows in the input.
        float t00 = inp[0] + inp[7];
        float t07 = inp[0] - inp[7];
        float t01 = inp[1] + inp[6];
        float t06 = inp[1] - inp[6];
        float t02 = inp[2] + inp[5];
        float t05 = inp[2] - inp[5];
        float t03 = inp[3] + inp[4];
        float t04 = inp[3] - inp[4];
        float t10 = t00    + t03;
        float t13 = t00    - t03;
        float t11 = t01    + t02;
        float t12 = t01    - t02;
        out[0]    = t10    + t11;
        out[4]    = t10    - t11;
        float z01 =(t12    + t13) * f13;
        out[2]    = t13    + z01;
        out[6]    = t13    - z01;
        t10       = t04    + t05;
        t11       = t05    + t06;
        t12       = t06    + t07;
        float z05 =(t10    - t12) * f05;
        float z02 = f02    * t10  + z05;
        float z04 = f04    * t12  + z05;
        float z03 = f13    * t11;
        float z11 = t07    + z03;
        float z13 = t07    - z03;
        out[5]    = z13    + z02;
        out[3]    = z13    - z02;
        out[1]    = z11    + z04;
        out[7]    = z11    - z04;
        out      += DCTSIZE;
        inp      += DCTSIZE;
    }

    out = dst;
    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process columns in the input.
        float t00 = out[DCTSIZE * 0] + out[DCTSIZE * 7];
        float t07 = out[DCTSIZE * 0] - out[DCTSIZE * 7];
        float t01 = out[DCTSIZE * 1] + out[DCTSIZE * 6];
        float t06 = out[DCTSIZE * 1] - out[DCTSIZE * 6];
        float t02 = out[DCTSIZE * 2] + out[DCTSIZE * 5];
        float t05 = out[DCTSIZE * 2] - out[DCTSIZE * 5];
        float t03 = out[DCTSIZE * 3] + out[DCTSIZE * 4];
        float t04 = out[DCTSIZE * 3] - out[DCTSIZE * 4];
        float t10 = t00 + t03;
        float t13 = t00 - t03;
        float t11 = t01 + t02;
        float t12 = t01 - t02;
        out[DCTSIZE*0]  = t10   + t11;
        out[DCTSIZE*4]  = t10   - t11;
        float z01 =(t12 + t13)  * f13;
        out[DCTSIZE*2]  = t13   + z01;
        out[DCTSIZE*6]  = t13   - z01;
        t10       = t04 + t05;
        t11       = t05 + t06;
        t12       = t06 + t07;
        float z05 =(t10 - t12)  * f05;
        float z02 = f02 * t10   + z05;
        float z04 = f04 * t12   + z05;
        float z03 = f13 * t11;
        float z11 = t07 + z03;
        float z13 = t07 - z03;
        out[DCTSIZE*5]  = z13   + z02;
        out[DCTSIZE*3]  = z13   - z02;
        out[DCTSIZE*1]  = z11   + z04;
        out[DCTSIZE*7]  = z11   - z04;
        out++;
    }

    // quantize and descale the DCT coefficients.
    for (size_t i = 0;  i < DCTSIZE * DCTSIZE; ++i)
        dst[i] *= quant[i];
}

/// @summary Performs a 2D forward DCT on an 8x8 block of a single channel.
/// This is performed after the RGBA pixels are converted to YCoCg, and after
/// subsampling to 4:2:0, so this routine is called four times for luma and
/// one time for each of the chroma channels. Additionally, the output DCT
/// coefficients are quantized and descaled.
/// @param dst A 64-element array to be filled with quantized DCT coefficients.
/// @param src A 64-element array representing an 8x8 block of input.
/// @param quant The 64-element scaled quantization coefficient array, as
/// output by the scaled_qtable_int16() function.
static void fdct8x8iq_base(
    int16_t       * restrict dst,
    int16_t const * restrict src,
    int16_t const * restrict quant)
{
    #define DCTSIZE 8U

    int16_t const *inp = (int16_t const*) src;
    int16_t       *out = (int16_t*) dst;

    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process rows in the input.
        int32_t i0 =  inp[0];
        int32_t i1 =  inp[1];
        int32_t i2 =  inp[2];
        int32_t i3 =  inp[3];
        int32_t i4 =  inp[4];
        int32_t i5 =  inp[5];
        int32_t i6 =  inp[6];
        int32_t i7 =  inp[7];
        int32_t a0 =  i0  +  i7;
        int32_t a1 =  i1  +  i6;
        int32_t a2 =  i2  +  i5;
        int32_t a3 =  i3  +  i4;
        int32_t a4 =  i0  -  i7;
        int32_t a5 =  i1  -  i6;
        int32_t a6 =  i2  -  i5;
        int32_t a7 =  i3  -  i4;
        int32_t b0 =  a0  +  a3;
        int32_t b1 =  a1  +  a2;
        int32_t b2 =  a0  -  a3;
        int32_t b3 =  a1  -  a2;
        int32_t c0 =  b0  +  b1;
        int32_t c1 =  b0  -  b1;
        int32_t c2 =  b2  + (b2 >> 2) + (b3 >> 1);
        int32_t c3 = (b2 >> 1)  - b3  - (b3 >> 2);
        int32_t b4 = (a7 >> 2)  + a4  + (a4 >> 2) - (a4 >> 4);
        int32_t b7 = (a4 >> 2)  - a7  - (a7 >> 2) + (a7 >> 4);
        int32_t b5 =  a5        + a6  - (a6 >> 2) - (a6 >> 4);
        int32_t b6 =  a6        - a5  + (a5 >> 2) + (a5 >> 4);
        int32_t c4 =  b4  +  b5;
        int32_t c5 =  b4  -  b5;
        int32_t c6 =  b6  +  b7;
        int32_t c7 =  b6  -  b7;
        int32_t d4 =  c4;
        int32_t d5 =  c5  +  c7;
        int32_t d6 =  c5  -  c7;
        int32_t d7 =  c6;
        out[0]  = (int16_t) c0;
        out[1]  = (int16_t) d4;
        out[2]  = (int16_t) c2;
        out[3]  = (int16_t) d6;
        out[4]  = (int16_t) c1;
        out[5]  = (int16_t) d5;
        out[6]  = (int16_t) c3;
        out[7]  = (int16_t) d7;
        out    +=  DCTSIZE;
        inp    +=  DCTSIZE;
    }

    out = dst;
    for (int i = DCTSIZE - 1; i >= 0; --i)
    {
        // process columns in the input.
        int32_t i0  = out[DCTSIZE * 0];
        int32_t i1  = out[DCTSIZE * 1];
        int32_t i2  = out[DCTSIZE * 2];
        int32_t i3  = out[DCTSIZE * 3];
        int32_t i4  = out[DCTSIZE * 4];
        int32_t i5  = out[DCTSIZE * 5];
        int32_t i6  = out[DCTSIZE * 6];
        int32_t i7  = out[DCTSIZE * 7];
        int32_t a0  = i0  +  i7;
        int32_t a1  = i1  +  i6;
        int32_t a2  = i2  +  i5;
        int32_t a3  = i3  +  i4;
        int32_t a4  = i0  -  i7;
        int32_t a5  = i1  -  i6;
        int32_t a6  = i2  -  i5;
        int32_t a7  = i3  -  i4;
        int32_t b0  = a0  +  a3;
        int32_t b1  = a1  +  a2;
        int32_t b2  = a0  -  a3;
        int32_t b3  = a1  -  a2;
        int32_t c0  = b0  +  b1;
        int32_t c1  = b0  -  b1;
        int32_t c2  = b2  + (b2 >> 2) + (b3 >> 1);
        int32_t c3  =(b2 >> 1)  - b3  - (b3 >> 2);
        int32_t b4  =(a7 >> 2)  + a4  + (a4 >> 2) - (a4 >> 4);
        int32_t b7  =(a4 >> 2)  - a7  - (a7 >> 2) + (a7 >> 4);
        int32_t b5  = a5        + a6  - (a6 >> 2) - (a6 >> 4);
        int32_t b6  = a6        - a5  + (a5 >> 2) + (a5 >> 4);
        int32_t c4  = b4  +  b5;
        int32_t c5  = b4  -  b5;
        int32_t c6  = b6  +  b7;
        int32_t c7  = b6  -  b7;
        int32_t d4  = c4;
        int32_t d5  = c5  +  c7;
        int32_t d6  = c5  -  c7;
        int32_t d7  = c6;
        out[DCTSIZE * 0]  = (int16_t) c0;
        out[DCTSIZE * 1]  = (int16_t) d4;
        out[DCTSIZE * 2]  = (int16_t) c2;
        out[DCTSIZE * 3]  = (int16_t) d6;
        out[DCTSIZE * 4]  = (int16_t) c1;
        out[DCTSIZE * 5]  = (int16_t) d5;
        out[DCTSIZE * 6]  = (int16_t) c3;
        out[DCTSIZE * 7]  = (int16_t) d7;
        out++;
    }
    for (size_t i = 0;  i < DCTSIZE * DCTSIZE; ++i)
        dst[i]   /= quant[i];
}

/// @summary Performs an inverse 2D DCT on an 8x8 block of DCT coefficients to
/// retrieve sample values. The DCT coefficients are assumed to have been
/// de-quantized, and to have been scaled down by a factor of 8.
/// @summary dst A 64-element array where the output will be written.
/// @summary src A 64-element array of dequantized DCT coefficient values.
static void idct8x8f_base(float * restrict dst, float const * restrict src)
{
    #define DCTSIZE     8U
    #define COLUMNF(x) (inp[DCTSIZE*x])
    #define i13         1.414213562f
    #define i11         1.414213562f
    #define i05         1.847759065f
    #define i10         1.08239220f
    #define i12        -2.61312593f

    float        workspace[64];
    float const *inp = (float const*) src;
    float       *wsp = (float*) workspace;

    for (size_t i = DCTSIZE; i > 0; --i)
    {
        // process columns from the input; write the result to workspace.
        float t00 = COLUMNF(0);
        float t01 = COLUMNF(2);
        float t02 = COLUMNF(4);
        float t03 = COLUMNF(6);
        float t10 = t00 + t02;
        float t11 = t00 - t02;
        float t13 = t01 + t03;
        float t12 =(t01 - t03) * i13 - t13;
        t00 = t10 + t13;
        t03 = t10 - t13;
        t01 = t11 + t12;
        t02 = t11 - t12;
        float t04 = COLUMNF(1);
        float t05 = COLUMNF(3);
        float t06 = COLUMNF(5);
        float t07 = COLUMNF(7);
        float z13 = t06  + t05;
        float z10 = t06  - t05;
        float z11 = t04  + t07;
        float z12 = t04  - t07;
        t07 = z11 + z13;
        t11 =(z11 - z13) * i11;
        float z05 =(z10  + z12) * i05;
        t10 = i10 * z12  - z05;
        t12 = i12 * z10  + z05;
        t06 = t12 - t07;
        t05 = t11 - t06;
        t04 = t10 + t05;
        wsp[DCTSIZE * 0] = t00 + t07;
        wsp[DCTSIZE * 1] = t01 + t06;
        wsp[DCTSIZE * 2] = t02 + t05;
        wsp[DCTSIZE * 3] = t03 - t04;
        wsp[DCTSIZE * 4] = t03 + t04;
        wsp[DCTSIZE * 5] = t02 - t05;
        wsp[DCTSIZE * 6] = t01 - t06;
        wsp[DCTSIZE * 7] = t00 - t07;
        wsp++;
        inp++;
    }

    wsp = workspace;
    for (size_t i = 0; i < DCTSIZE; ++i)
    {
        // now process rows from the work array; write to dst.
        // note that we must descale the results by a factor of 8 (2^3).
        float t10 = wsp[0] + wsp[4];
        float t11 = wsp[0] - wsp[4];
        float t13 = wsp[2] + wsp[6];
        float t12 =(wsp[2] - wsp[6]) * i13 - t13;
        float t00 = t10    + t13;
        float t03 = t10    - t13;
        float t01 = t11    + t12;
        float t02 = t11    - t12;
        float z13 = wsp[5] + wsp[3];
        float z10 = wsp[5] - wsp[3];
        float z11 = wsp[1] + wsp[7];
        float z12 = wsp[1] - wsp[7];
        float t07 = z11    + z13;
        t11       =(z11    - z13) * i11;
        float z05 =(z10    + z12) * i05;
        t10       = i10    * z12  - z05;
        t12       = i12    * z10  + z05;
        float t06 = t12    - t07;
        float t05 = t11    - t06;
        float t04 = t10    + t05;
        dst[0] = t00 + t07;
        dst[7] = t00 - t07;
        dst[1] = t01 + t06;
        dst[6] = t01 - t06;
        dst[2] = t02 + t05;
        dst[5] = t02 - t05;
        dst[4] = t03 + t04;
        dst[3] = t03 - t04;
        dst   += DCTSIZE;
        wsp   += DCTSIZE;
    }
}

/// @summary Performs an inverse 2D DCT on an 8x8 block of DCT coefficients to
/// retrieve sample values. The DCT coefficients are assumed to have been
/// de-quantized, and to have been scaled down by a factor of 8. The integer
/// DCT is not perfectly reversable. For more information, see:
/// http://fgiesen.wordpress.com/2013/11/04/bink-2-2-integer-dct-design-part-1/
/// http://fgiesen.wordpress.com/2013/11/10/bink-2-2-integer-dct-design-part-2/
/// https://github.com/rygorous/dct_blog/blob/master/bink_idct_B2_partial.m
/// @summary dst A 64-element array where the output will be written.
/// @summary src A 64-element array of dequantized DCT coefficient values.
static void idct8x8i_base(int16_t * restrict dst, int16_t const * restrict src)
{
    #define DCTSIZE       8U
    #define COLUMNI(x)   (inp[DCTSIZE*x])

    int32_t        workspace[64];
    int16_t const *inp = (int16_t const*) src;
    int32_t       *wsp = (int32_t*) workspace;

    // @note: intermediate values are stored in 32-bit integers in C
    // since overflow of signed integer values is technically undefined.

    for (size_t i = DCTSIZE; i > 0; --i)
    {
        // process columns from the input; write the result to workspace.
        int32_t c0 = COLUMNI(0);
        int32_t d4 = COLUMNI(1);
        int32_t c2 = COLUMNI(2);
        int32_t d6 = COLUMNI(3);
        int32_t c1 = COLUMNI(4);
        int32_t d5 = COLUMNI(5);
        int32_t c3 = COLUMNI(6);
        int32_t d7 = COLUMNI(7);
        int32_t c4 = d4;
        int32_t c5 = d5 + d6;
        int32_t c7 = d5 - d6;
        int32_t c6 = d7;
        int32_t b4 = c4 + c5;
        int32_t b5 = c4 - c5;
        int32_t b6 = c6 + c7;
        int32_t b7 = c6 - c7;
        int32_t b0 = c0 + c1;
        int32_t b1 = c0 - c1;
        int32_t b2 = c2 +(c2 >> 2) + (c3 >> 1);
        int32_t b3 =(c2 >> 1) - c3 - (c3 >> 2);
        int32_t a4 =(b7 >> 2) + b4 + (b4 >> 2) - (b4 >> 4);
        int32_t a7 =(b4 >> 2) - b7 - (b7 >> 2) + (b7 >> 4);
        int32_t a5 = b5       - b6 + (b6 >> 2) + (b6 >> 4);
        int32_t a6 = b6       + b5 - (b5 >> 2) - (b5 >> 4);
        int32_t a0 = b0 + b2;
        int32_t a3 = b0 - b2;
        int32_t a1 = b1 + b3;
        int32_t a2 = b1 - b3;
        wsp[DCTSIZE*0]  = a0 + a4;
        wsp[DCTSIZE*1]  = a1 + a5;
        wsp[DCTSIZE*2]  = a2 + a6;
        wsp[DCTSIZE*3]  = a3 + a7;
        wsp[DCTSIZE*4]  = a3 - a7;
        wsp[DCTSIZE*5]  = a2 - a6;
        wsp[DCTSIZE*6]  = a1 - a5;
        wsp[DCTSIZE*7]  = a0 - a4;
        wsp++;
        inp++;
    }

    wsp = workspace;
    for (size_t i = 0; i < DCTSIZE; ++i)
    {
        // now process rows from the work array; write the result to dst.
        int32_t c0 = wsp[0];
        int32_t d4 = wsp[1];
        int32_t c2 = wsp[2];
        int32_t d6 = wsp[3];
        int32_t c1 = wsp[4];
        int32_t d5 = wsp[5];
        int32_t c3 = wsp[6];
        int32_t d7 = wsp[7];
        int32_t c4 = d4;
        int32_t c5 = d5 + d6;
        int32_t c7 = d5 - d6;
        int32_t c6 = d7;
        int32_t b4 = c4 + c5;
        int32_t b5 = c4 - c5;
        int32_t b6 = c6 + c7;
        int32_t b7 = c6 - c7;
        int32_t b0 = c0 + c1;
        int32_t b1 = c0 - c1;
        int32_t b2 = c2 +(c2 >> 2) + (c3 >> 1);
        int32_t b3 =(c2 >> 1) - c3 - (c3 >> 2);
        int32_t a4 =(b7 >> 2) + b4 + (b4 >> 2) - (b4 >> 4);
        int32_t a7 =(b4 >> 2) - b7 - (b7 >> 2) + (b7 >> 4);
        int32_t a5 = b5       - b6 + (b6 >> 2) + (b6 >> 4);
        int32_t a6 = b6       + b5 - (b5 >> 2) - (b5 >> 4);
        int32_t a0 = b0 + b2;
        int32_t a3 = b0 - b2;
        int32_t a1 = b1 + b3;
        int32_t a2 = b1 - b3;
        dst[0]     =(int16_t)  (a0 + a4);
        dst[1]     =(int16_t)  (a1 + a5);
        dst[2]     =(int16_t)  (a2 + a6);
        dst[3]     =(int16_t)  (a3 + a7);
        dst[4]     =(int16_t)  (a3 - a7);
        dst[5]     =(int16_t)  (a2 - a6);
        dst[6]     =(int16_t)  (a1 - a5);
        dst[7]     =(int16_t)  (a0 - a4);
        dst       += DCTSIZE;
        wsp       += DCTSIZE;
    }
}

/// @summary Dequantizes and performs an inverse 2D DCT on an 8x8 block of
/// quantized DCT coefficients to retrieve sample values.
/// @summary dst A 64-element array where the output will be written.
/// @summary src A 64-element array of quantized DCT coefficient values.
/// @summary quant The 64-element scaled quantization coefficient array, as
/// output by the scaled_qtable_float() function.
static void idct8x8fd_base(
   float       * restrict dst,
   float const * restrict src,
   float const * restrict quant)
{
    #define DCTSIZE      8U
    #define COLUMNFD(x) (inp[DCTSIZE*x] * qtp[DCTSIZE*x])
    #define i13          1.414213562f
    #define i11          1.414213562f
    #define i05          1.847759065f
    #define i10          1.08239220f
    #define i12         -2.61312593f

    float        workspace[64];
    float const *qtp = (float const*) quant;
    float const *inp = (float const*) src;
    float       *wsp = (float*) workspace;

    for (size_t i = DCTSIZE; i > 0; --i)
    {
        // process columns from the input; write the result to workspace.
        float t00 = COLUMNFD(0);
        float t01 = COLUMNFD(2);
        float t02 = COLUMNFD(4);
        float t03 = COLUMNFD(6);
        float t10 = t00 + t02;
        float t11 = t00 - t02;
        float t13 = t01 + t03;
        float t12 =(t01 - t03) * i13 - t13;
        t00 = t10 + t13;
        t03 = t10 - t13;
        t01 = t11 + t12;
        t02 = t11 - t12;
        float t04 = COLUMNFD(1);
        float t05 = COLUMNFD(3);
        float t06 = COLUMNFD(5);
        float t07 = COLUMNFD(7);
        float z13 = t06  + t05;
        float z10 = t06  - t05;
        float z11 = t04  + t07;
        float z12 = t04  - t07;
        t07 = z11 + z13;
        t11 =(z11 - z13) * i11;
        float z05 =(z10  + z12) * i05;
        t10 = i10 * z12  - z05;
        t12 = i12 * z10  + z05;
        t06 = t12 - t07;
        t05 = t11 - t06;
        t04 = t10 + t05;
        wsp[DCTSIZE * 0] = t00 + t07;
        wsp[DCTSIZE * 1] = t01 + t06;
        wsp[DCTSIZE * 2] = t02 + t05;
        wsp[DCTSIZE * 3] = t03 - t04;
        wsp[DCTSIZE * 4] = t03 + t04;
        wsp[DCTSIZE * 5] = t02 - t05;
        wsp[DCTSIZE * 6] = t01 - t06;
        wsp[DCTSIZE * 7] = t00 - t07;
        wsp++;
        qtp++;
        inp++;
    }

    wsp = workspace;
    for (size_t i = 0; i < DCTSIZE; ++i)
    {
        // now process rows from the work array; write the result to dst.
        float t10 = wsp[0] + wsp[4];
        float t11 = wsp[0] - wsp[4];
        float t13 = wsp[2] + wsp[6];
        float t12 =(wsp[2] - wsp[6]) * i13 - t13;
        float t00 = t10    + t13;
        float t03 = t10    - t13;
        float t01 = t11    + t12;
        float t02 = t11    - t12;
        float z13 = wsp[5] + wsp[3];
        float z10 = wsp[5] - wsp[3];
        float z11 = wsp[1] + wsp[7];
        float z12 = wsp[1] - wsp[7];
        float t07 = z11    + z13;
        t11       =(z11    - z13) * i11;
        float z05 =(z10    + z12) * i05;
        t10       = i10    * z12  - z05;
        t12       = i12    * z10  + z05;
        float t06 = t12    - t07;
        float t05 = t11    - t06;
        float t04 = t10    + t05;
        dst[0] = t00 + t07;
        dst[7] = t00 - t07;
        dst[1] = t01 + t06;
        dst[6] = t01 - t06;
        dst[2] = t02 + t05;
        dst[5] = t02 - t05;
        dst[4] = t03 + t04;
        dst[3] = t03 - t04;
        dst   += DCTSIZE;
        wsp   += DCTSIZE;
    }
}

/// @summary Performs an inverse 2D DCT on an 8x8 block of DCT coefficients to
/// retrieve sample values. The DCT coefficients are assumed to have been
/// de-quantized, and to have been scaled down by a factor of 8. The integer
/// DCT is not perfectly reversable. For more information, see:
/// http://fgiesen.wordpress.com/2013/11/04/bink-2-2-integer-dct-design-part-1/
/// http://fgiesen.wordpress.com/2013/11/10/bink-2-2-integer-dct-design-part-2/
/// https://github.com/rygorous/dct_blog/blob/master/bink_idct_B2_partial.m
/// @summary dst A 64-element array where the output will be written.
/// @summary src A 64-element array of dequantized DCT coefficient values.
/// @summary quant The 64-element scaled quantization coefficient array, as
/// output by the scaled_qtable_int16() function.
static void idct8x8id_base(
    int16_t       * restrict dst,
    int16_t const * restrict src,
    int16_t const * restrict quant)
{
    #define DCTSIZE       8U
    #define COLUMNID(x)  (inp[DCTSIZE*x] * qtp[DCTSIZE*x])

    int32_t        workspace[64];
    int16_t const *qtp = (int16_t const*) quant;
    int16_t const *inp = (int16_t const*) src;
    int32_t       *wsp = (int32_t*) workspace;

    // @note: intermediate values are stored in 32-bit integers in C
    // since overflow of signed integer values is technically undefined.

    for (size_t i = DCTSIZE; i > 0; --i)
    {
        // process columns from the input; write the result to workspace.
        int32_t c0 = COLUMNID(0);
        int32_t d4 = COLUMNID(1);
        int32_t c2 = COLUMNID(2);
        int32_t d6 = COLUMNID(3);
        int32_t c1 = COLUMNID(4);
        int32_t d5 = COLUMNID(5);
        int32_t c3 = COLUMNID(6);
        int32_t d7 = COLUMNID(7);
        int32_t c4 = d4;
        int32_t c5 = d5 + d6;
        int32_t c7 = d5 - d6;
        int32_t c6 = d7;
        int32_t b4 = c4 + c5;
        int32_t b5 = c4 - c5;
        int32_t b6 = c6 + c7;
        int32_t b7 = c6 - c7;
        int32_t b0 = c0 + c1;
        int32_t b1 = c0 - c1;
        int32_t b2 = c2 +(c2 >> 2) + (c3 >> 1);
        int32_t b3 =(c2 >> 1) - c3 - (c3 >> 2);
        int32_t a4 =(b7 >> 2) + b4 + (b4 >> 2) - (b4 >> 4);
        int32_t a7 =(b4 >> 2) - b7 - (b7 >> 2) + (b7 >> 4);
        int32_t a5 = b5       - b6 + (b6 >> 2) + (b6 >> 4);
        int32_t a6 = b6       + b5 - (b5 >> 2) - (b5 >> 4);
        int32_t a0 = b0 + b2;
        int32_t a3 = b0 - b2;
        int32_t a1 = b1 + b3;
        int32_t a2 = b1 - b3;
        wsp[DCTSIZE*0]  = a0 + a4;
        wsp[DCTSIZE*1]  = a1 + a5;
        wsp[DCTSIZE*2]  = a2 + a6;
        wsp[DCTSIZE*3]  = a3 + a7;
        wsp[DCTSIZE*4]  = a3 - a7;
        wsp[DCTSIZE*5]  = a2 - a6;
        wsp[DCTSIZE*6]  = a1 - a5;
        wsp[DCTSIZE*7]  = a0 - a4;
        wsp++;
        inp++;
    }

    wsp = workspace;
    for (size_t i = 0; i < DCTSIZE; ++i)
    {
        // now process rows from the work array; write the result to dst.
        int32_t c0 = wsp[0];
        int32_t d4 = wsp[1];
        int32_t c2 = wsp[2];
        int32_t d6 = wsp[3];
        int32_t c1 = wsp[4];
        int32_t d5 = wsp[5];
        int32_t c3 = wsp[6];
        int32_t d7 = wsp[7];
        int32_t c4 = d4;
        int32_t c5 = d5 + d6;
        int32_t c7 = d5 - d6;
        int32_t c6 = d7;
        int32_t b4 = c4 + c5;
        int32_t b5 = c4 - c5;
        int32_t b6 = c6 + c7;
        int32_t b7 = c6 - c7;
        int32_t b0 = c0 + c1;
        int32_t b1 = c0 - c1;
        int32_t b2 = c2 +(c2 >> 2) + (c3 >> 1);
        int32_t b3 =(c2 >> 1) - c3 - (c3 >> 2);
        int32_t a4 =(b7 >> 2) + b4 + (b4 >> 2) - (b4 >> 4);
        int32_t a7 =(b4 >> 2) - b7 - (b7 >> 2) + (b7 >> 4);
        int32_t a5 = b5       - b6 + (b6 >> 2) + (b6 >> 4);
        int32_t a6 = b6       + b5 - (b5 >> 2) - (b5 >> 4);
        int32_t a0 = b0 + b2;
        int32_t a3 = b0 - b2;
        int32_t a1 = b1 + b3;
        int32_t a2 = b1 - b3;
        dst[0]     =(int16_t) ((a0 + a4) >> 6);   // descale by 64
        dst[1]     =(int16_t) ((a1 + a5) >> 6);   // descale by 64
        dst[2]     =(int16_t) ((a2 + a6) >> 6);   // descale by 64
        dst[3]     =(int16_t) ((a3 + a7) >> 6);   // descale by 64
        dst[4]     =(int16_t) ((a3 - a7) >> 6);   // descale by 64
        dst[5]     =(int16_t) ((a2 - a6) >> 6);   // descale by 64
        dst[6]     =(int16_t) ((a1 - a5) >> 6);   // descale by 64
        dst[7]     =(int16_t) ((a0 - a4) >> 6);   // descale by 64
        dst       += DCTSIZE;
        wsp       += DCTSIZE;
    }
}

/// @summary Loads an 8x8 sub-block from a 16x16 block of pixels. This routine
/// is used to grab sub-blocks of the luma channel.
/// @param samples A 64-element array used to store the sampled data.
/// @param ycocg The 768 element array of YCoCg pixel data being sampled.
/// @param x The quadrant of the sub-block to sample, 0 = left, 1 = right.
/// @param y The quadrant of the sub-block to sample, 0 = top, 1 = bottom.
/// @param c The channel index to sample, 0 = Y, 1 = Co, 2 = Cg.
template <typename T>
static void subblock(
    T             * restrict samples,
    int16_t const * restrict YCoCg,
    size_t                   x,
    size_t                   y,
    size_t                   channel)
{
    int16_t const *src = NULL;
    T             *dst = samples;
    // each column has three elements, so a row has 16 * 3 = 48 elements.
    // convert x to a column index by multiplying by 8 * 3 = 24 for x[0, 1].
    // convert y to a row index by multiplying by 8 for y[0, 1].
    x = (x * 24)  + channel;
    y = (y *  8);
    for (size_t i = 0; i < 8; ++i)
    {
         src   =&YCoCg[(y + i)  *  48] + x;
        *dst++ =   src[(0 * 3)];
        *dst++ =   src[(1 * 3)];
        *dst++ =   src[(2 * 3)];
        *dst++ =   src[(3 * 3)];
        *dst++ =   src[(4 * 3)];
        *dst++ =   src[(5 * 3)];
        *dst++ =   src[(6 * 3)];
        *dst++ =   src[(7 * 3)];
    }
}

/// @summary Loads an 8x8 sub-block from a 16x16 block of pixels, performing
/// downsampling by half in each of the horizontal and vertical dimensions,
/// such that the resulting 8x8 sub-block contains a downsampled version of the
/// entire 16x16 source block, instead of just an 8x8 sub-region. This routine
/// is used to subsample the chroma channels, Co and Cg.
/// @param samples A 64-element array used to store the sampled data.
/// @param ycocg The 768 element array of YCoCg pixel data being sampled.
/// @param channel The channel index to sample, 0 = Y, 1 = Co, 2 = Cg.
template <typename T>
static void subsample(
    T             * restrict samples,
    int16_t const * restrict YCoCg,
    size_t                   channel)
{
    int16_t const *src1  = NULL;
    int16_t const *src2  = NULL;
    T             *dst   = samples;
    for (size_t i = 0; i < 16; i += 2)
    {
         src1  =&YCoCg[(i + 0) * 48] + channel;
         src2  =&YCoCg[(i + 1) * 48] + channel;
        *dst++ = ((src1[0 * 3] + src1[1 * 3] + src2[0 * 3] + src2[1 * 3]) >> 2);
        *dst++ = ((src1[2 * 3] + src1[3 * 3] + src2[2 * 3] + src2[3 * 3]) >> 2);
        *dst++ = ((src1[4 * 3] + src1[5 * 3] + src2[4 * 3] + src2[5 * 3]) >> 2);
        *dst++ = ((src1[6 * 3] + src1[7 * 3] + src2[6 * 3] + src2[7 * 3]) >> 2);
        *dst++ = ((src1[8 * 3] + src1[9 * 3] + src2[8 * 3] + src2[9 * 3]) >> 2);
        *dst++ = ((src1[10* 3] + src1[11* 3] + src2[10* 3] + src2[11* 3]) >> 2);
        *dst++ = ((src1[12* 3] + src1[13* 3] + src2[12* 3] + src2[13* 3]) >> 2);
        *dst++ = ((src1[14* 3] + src1[15* 3] + src2[14* 3] + src2[15* 3]) >> 2);
    }
}

/// @summary Combine four packed 8x8 blocks stored in a contiguous array back
/// into a single 16x16 block. This is used to undo the effect of splitting the
/// luma channel from a 16x16 block into four 8x8 blocks.
/// @param dst A 256 element array to store the output 16x16 block.
/// @param src A 256 element array storing the packed 8x8 input blocks.
template <typename T>
static void merge_blocks(T * restrict dst, T const * restrict src)
{
    T const * restrict src0 = &src[0];     // src[0...63]
    T const * restrict src1 = &src[64];    // src[64...127]
    T const * restrict src2 = &src[128];   // src[128...191]
    T const * restrict src3 = &src[192];   // src[192...255]
    T       * restrict dst0 = &dst[0];     // col[0...7],  row[0...7]
    T       * restrict dst1 = &dst[8];     // col[8...15], row[0...7]
    T       * restrict dst2 = &dst[128];   // col[0...7],  row[8...15]
    T       * restrict dst3 = &dst[128+8]; // col[8...15], row[8...15]
    for (size_t i = 0; i < 8; ++i)
    {
        // copy each row from source to destination.
        for (size_t j = 0; j < 8; ++j)
        {
            // copy each column from source to destination.
            dst0[j] = src0[j];
            dst1[j] = src1[j];
            dst2[j] = src2[j];
            dst3[j] = src3[j];
        }
        src0 +=  8; dst0 += 16;
        src1 +=  8; dst1 += 16;
        src2 +=  8; dst2 += 16;
        src3 +=  8; dst3 += 16;
    }
}

/// @summary Scale an 8x8 block of samples up to 16x16 by sample doubling. This
/// is used to scale the chroma-orange and chroma-green blocks back up to 16x16
/// for easy merging with the luma channel values.
/// @param dst A 256-element array to store the output 16x16 block.
/// @param src A 64-element array specifying the 8x8 input block.
template <typename T>
static void scale_block(T * restrict dst, T const * restrict src)
{
    T const * restrict srcRow  = src;
    T       * restrict dstRow0 =&dst[0];
    T       * restrict dstRow1 =&dst[16];
    for (size_t i = 0; i < 8; ++i)
    {
        // copy each row from source to destination.
        for (size_t j = 0; j < 8; ++j)
        {
            // each sample in src becomes a 2x2 block in dst.
            dstRow0[j * 2 + 0] = srcRow[j];
            dstRow0[j * 2 + 1] = srcRow[j];
            dstRow1[j * 2 + 0] = srcRow[j];
            dstRow1[j * 2 + 1] = srcRow[j];
        }
        srcRow     += 8;
        dstRow0    += 32;
        dstRow1    += 32;
    }
}

size_t tile_count(size_t *num_x, size_t *num_y, image_tiler_config_t const *config)
{
    size_t  borders   = (size_t)(config->BorderSize * 2);
    float   image_w   = (float) (config->ImageWidth);
    float   image_h   = (float) (config->ImageHeight);
    float   tile_w    = (float) (config->TileWidth  - borders);
    float   tile_h    = (float) (config->TileHeight - borders);
    size_t  tiles_x   = (size_t) ceilf(image_w / tile_w);
    size_t  tiles_y   = (size_t) ceilf(image_h / tile_h);
    if (num_x) *num_x = tiles_x;
    if (num_y) *num_y = tiles_y;
    return (tiles_x   * tiles_y);
}

bool tile_alloc(image_tile_t *tile, image_tiler_config_t const *config)
{
    tile->SourceX         = 0;
    tile->SourceY         = 0;
    tile->SourceWidth     = 0;
    tile->SourceHeight    = 0;
    tile->TileX           = 0;
    tile->TileY           = 0;
    tile->TileIndex       = 0;
    tile->TileWidth       = 0;
    tile->TileHeight      = 0;
    tile->BytesPerRow     = 0;
    tile->BytesPerTile    = 0;
    tile->Pixels          = malloc(config->TileWidth * config->TileHeight * 4);
    return (tile->Pixels != NULL);
}

void tile_free(image_tile_t *tile)
{
    if (tile->Pixels != NULL)
    {
        free(tile->Pixels);
        tile->Pixels  = NULL;
    }
}

bool copy_tile(image_tile_t *tile, image_tiler_config_t const *config, size_t index)
{
    size_t tiles_x = 0;
    size_t tiles_y = 0;
    size_t tiles_n = tile_count(&tiles_x, &tiles_y, config);

    if (index >= tiles_n)
        return false;

    // convert index into x,y in tile space, and from there, figure
    // out the tile bounding rectangle on the source image.
    size_t  tile_y    = index  / tiles_x;
    size_t  tile_x    = index  % tiles_x;
    size_t  source_w  = config->TileWidth  - (config->BorderSize * 2); // px
    size_t  source_h  = config->TileHeight - (config->BorderSize * 2); // px
    size_t  source_x  = tile_x * source_w; // px
    size_t  source_y  = tile_y * source_h; // px

    // calculate the pointer to the first row of the tile on the source image.
    uint32_t *src_row =(uint32_t *)config->Pixels;
    src_row          +=(source_y * config->ImageWidth);
    src_row          += source_x;

    // calculate the pointer to the first row of the destination tile.
    uint32_t *dst_row = (uint32_t*)tile->Pixels;
    size_t    dst_num = config->TileWidth; // px
    size_t    pad_right  = 0;              // px
    size_t    pad_bottom = 0;              // rows

    // calculate how much padding we have on the right/bottom edges, if any.
    if (source_x  + source_w > config->ImageWidth)
    {   // this tile includes data outisde of the right edge of the source.
        size_t n  = config->ImageWidth - source_x;
        pad_right = source_w - n;
        source_w  = n;
    }
    if (source_y  + source_h > config->ImageHeight)
    {   // this tile includes data outside of the bottom edge of the source.
        size_t n  = config->ImageHeight - source_y;
        pad_bottom= source_h - n;
        source_h  = n;
    }

    // generate the top border of the tile.
    for (size_t i = 0; i < config->BorderSize; ++i)
    {
        read_row_border(dst_row, dst_num, src_row, source_w, pad_right, config);
        dst_row += dst_num;
    }

    // copy padded rows out of the source image into the tile.
    for (size_t i = 0; i < source_h; ++i)
    {
        read_row(dst_row, src_row, source_w, pad_right, config);
        src_row += config->ImageWidth;
        dst_row += dst_num;
    }

    // duplicate the last row to pad out the bottom edge.
    uint32_t *last_row = dst_row - dst_num;
    for (size_t i = 0; i < pad_bottom; ++i)
    {
        uint32_t *row = last_row;
        for (size_t j = 0; j < dst_num; ++j)
            *dst_row++= *row++;
    }

    // generate the bottom border of the tile.
    for (size_t i = 0; i < config->BorderSize; ++i)
    {
        read_row_border(dst_row, dst_num, src_row, source_w, pad_right, config);
        dst_row += dst_num;
    }

    // finally, fill out metadata in the tile record and clean up.
    tile->SourceX       = source_x;
    tile->SourceY       = source_y;
    tile->SourceWidth   = source_w;
    tile->SourceHeight  = source_h;
    tile->TileX         = tile_x;
    tile->TileY         = tile_y;
    tile->TileIndex     = index;
    tile->TileWidth     = config->TileWidth;
    tile->TileHeight    = config->TileHeight;
    tile->BytesPerRow   = config->TileWidth  * 4;
    tile->BytesPerTile  = config->TileWidth * config->TileHeight * 4;
    return true;
}

void quantization_table(
    int16_t       * restrict Q,
    int16_t const * restrict Qbase,
    int                      quality)
{
    if (quality < 1)   quality =  1;
    if (quality > 100) quality =  100;
    int16_t q = (quality < 50) ? (5000 / quality) : (200 - quality * 2);
    for (size_t i = 0; i < 64; ++i)
    {
        int16_t c = (Qbase[i] * q + 50) / 100;
        *Q++      =  qmin(qmax (c,   1),  255);
    }
}

void quantization_table_luma(int16_t *Qluma, int32_t quality)
{
    quantization_table(Qluma, JPEGLumaQuant, quality);
}

void quantization_table_chroma(int16_t *Qchroma, int32_t quality)
{
    quantization_table(Qchroma, JPEGChromaQuant, quality);
}

void quantization_table_scale(
    float         * restrict Qidct,
    float         * restrict Qfdct,
    int16_t const * restrict Qbase)
{
    float  CSFtable[64];
    csf_from_qtable(CSFtable, Qbase);
    scaled_qtable_float(Qidct, Qfdct, CSFtable);
}

void quantization_table_scale(
    int16_t       * restrict Qidct,
    int16_t       * restrict Qfdct,
    int16_t const * restrict Qbase)
{
    scaled_qtable_int16(Qidct, Qfdct, Qbase);
}

void qtables_encode(float * restrict Qluma, float * restrict Qchroma, int quality)
{
    int16_t Qbase_y[64]; // base quantization table for the luma channel.
    int16_t Qbase_c[64]; // base quantization table for the chroma channel.
    float   Qidct_x[64]; // discarded; not used during encode/FDCT.
    quantization_table_luma  (Qbase_y, quality);
    quantization_table_chroma(Qbase_c, quality);
    quantization_table_scale (Qidct_x, Qluma, Qbase_y);
    quantization_table_scale (Qidct_x, Qchroma, Qbase_c);
}

void qtables_encode(int16_t * restrict Qluma, int16_t * restrict Qchroma, int quality)
{
    int16_t Qbase_y[64]; // base quantization table for the luma channel.
    int16_t Qbase_c[64]; // base quantization table for the chroma channel.
    int16_t Qidct_x[64]; // discarded; not used during encode/FDCT.
    quantization_table_luma  (Qbase_y, quality);
    quantization_table_chroma(Qbase_c, quality);
    quantization_table_scale (Qidct_x, Qluma, Qbase_y);
    quantization_table_scale (Qidct_x, Qchroma, Qbase_c);
}

void qtables_decode(float * restrict Qluma, float * restrict Qchroma, int quality)
{
    int16_t Qbase_y[64]; // base quantization table for the luma channel.
    int16_t Qbase_c[64]; // base quantization table for the chroma channel.
    float   Qfdct_x[64]; // discarded; not used during decode/IDCT.
    quantization_table_luma  (Qbase_y, quality);
    quantization_table_chroma(Qbase_c, quality);
    quantization_table_scale (Qluma,   Qfdct_x, Qbase_y);
    quantization_table_scale (Qchroma, Qfdct_x, Qbase_c);
}

void qtables_decode(int16_t * restrict Qluma, int16_t * restrict Qchroma, int quality)
{
    int16_t Qbase_y[64]; // base quantization table for the luma channel.
    int16_t Qbase_c[64]; // base quantization table for the chroma channel.
    int16_t Qfdct_x[64]; // discarded; not used during decode/IDCT.
    quantization_table_luma  (Qbase_y, quality);
    quantization_table_chroma(Qbase_c, quality);
    quantization_table_scale (Qluma,   Qfdct_x, Qbase_y);
    quantization_table_scale (Qchroma, Qfdct_x, Qbase_c);
}

void fdct8x8f(float * restrict dst, float const * restrict src)
{
    fdct8x8f_base(dst, src);
}

void fdct8x8i(int16_t * restrict dst, int16_t const * restrict src)
{
    fdct8x8i_base(dst, src);
}

void fdct8x8fq(
    float         * restrict dst,
    float   const * restrict src,
    float   const * restrict Qfdct)
{
    fdct8x8fq_base(dst, src, Qfdct);
}

void fdct8x8iq(
    int16_t       * restrict dst,
    int16_t const * restrict src,
    int16_t const * restrict Qfdct)
{
    fdct8x8iq_base(dst, src, Qfdct);
}

void idct8x8f(float * restrict dst, float const * restrict src)
{
    idct8x8f_base(dst, src);
}

void idct8x8i(int16_t * restrict dst, int16_t const * restrict src)
{
    idct8x8i_base(dst, src);
}

void idct8x8fd(
    float         * restrict dst,
    float   const * restrict src,
    float   const * restrict Qidct)
{
    idct8x8fd_base(dst, src, Qidct);
}

void idct8x8id(
    int16_t       * restrict dst,
    int16_t const * restrict src,
    int16_t const * restrict Qidct)
{
    idct8x8id_base(dst, src, Qidct);
}

void encode16x16i(
    int16_t       * restrict Y,
    int16_t       * restrict Co,
    int16_t       * restrict Cg,
    uint8_t       * restrict A,
    int16_t const * restrict Qluma,
    int16_t const * restrict Qchroma,
    uint8_t const * restrict RGBA)
{
    int16_t YCoCg[768];
    int16_t samp00[64];
    int16_t samp10[64];
    int16_t samp01[64];
    int16_t samp11[64];
    int16_t sampCo[64];
    int16_t sampCg[64];

    // perform colorspace conversion and extract the alpha channel.
    rgba_to_ycocga(YCoCg, A, RGBA);

    // extract and quantize the luma channel into four 8x8 blocks.
    subblock(samp00, YCoCg, 0, 0, 0);
    subblock(samp10, YCoCg, 1, 0, 0);
    subblock(samp01, YCoCg, 0, 1, 0);
    subblock(samp11, YCoCg, 1, 1, 0);
    fdct8x8iq_base(&Y[0],   samp00, Qluma);
    fdct8x8iq_base(&Y[64],  samp10, Qluma);
    fdct8x8iq_base(&Y[128], samp01, Qluma);
    fdct8x8iq_base(&Y[192], samp11, Qluma);

    // downsample and quantize the chroma channels into one 8x8 block each.
    subsample(sampCo, YCoCg, 1);
    subsample(sampCg, YCoCg, 2);
    fdct8x8iq_base(Co, sampCo, Qchroma);
    fdct8x8iq_base(Cg, sampCg, Qchroma);
}

void decode16x16i_rgb(
    uint8_t       * restrict RGB,
    int16_t const * restrict Y,
    int16_t const * restrict Co,
    int16_t const * restrict Cg,
    int16_t const * restrict Qluma,
    int16_t const * restrict Qchroma)
{
    int16_t Yd[256]; // buffer for the dequantized luma blocks
    int16_t Ym[256]; // buffer for the merged luma samples
    int16_t Os[256]; // 16x16 chroma-orange samples
    int16_t Gs[256]; // 16x16 chroma-green samples
    int16_t Od[64];  // dequantized chroma-orange
    int16_t Gd[64];  // dequantized chroma-green

    // dequantize and IDCT the four luma blocks into Yd.
    // merge them back into a single 16x16 block in Ym.
    idct8x8id_base(&Yd[0],   &Y[0],   Qluma);
    idct8x8id_base(&Yd[64],  &Y[64],  Qluma);
    idct8x8id_base(&Yd[128], &Y[128], Qluma);
    idct8x8id_base(&Yd[192], &Y[192], Qluma);
    merge_blocks(Ym, Yd);

    // dequantize the 8x8 chroma-orange block, and then
    // scale it back up to 16x16 to match the luma channel.
    // do the same for the the chroma-green channel.
    idct8x8id_base(Od, Co, Qchroma);
    idct8x8id_base(Gd, Cg, Qchroma);
    scale_block(Os, Od);
    scale_block(Gs, Gd);

    // now convert from YCoCg back to RGB. the IDCT may have
    // resulted in values outside of the valid range, so clamping
    // to the range [0, 255] is necessary.
    for (size_t i = 0; i < 256; ++i)
    {
        int32_t y = Ym[i];
        int32_t co= Os[i];
        int32_t cg= Gs[i];
        int16_t t = y  - (cg >> 1);
        int16_t g = cg +  t;
        int16_t b = t  - (co >> 1);
        int16_t r = b  +  co;
        *RGB++    = clamp(r);
        *RGB++    = clamp(g);
        *RGB++    = clamp(b);
    }
}

void decode16x16i_rgba(
    uint8_t       * restrict RGBA,
    int16_t const * restrict Y,
    int16_t const * restrict Co,
    int16_t const * restrict Cg,
    uint8_t const * restrict A,
    int16_t const * restrict Qluma,
    int16_t const * restrict Qchroma)
{
    int16_t Yd[256]; // buffer for the dequantized luma blocks
    int16_t Ym[256]; // buffer for the merged luma samples
    int16_t Os[256]; // 16x16 chroma-orange samples
    int16_t Gs[256]; // 16x16 chroma-green samples
    int16_t Od[64];  // dequantized chroma-orange
    int16_t Gd[64];  // dequantized chroma-green

    // dequantize and IDCT the four luma blocks into Yd.
    // merge them back into a single 16x16 block in Ym.
    idct8x8id_base(&Yd[0],   &Y[0],   Qluma);
    idct8x8id_base(&Yd[64],  &Y[64],  Qluma);
    idct8x8id_base(&Yd[128], &Y[128], Qluma);
    idct8x8id_base(&Yd[192], &Y[192], Qluma);
    merge_blocks(Ym, Yd);

    // dequantize the 8x8 chroma-orange block, and then
    // scale it back up to 16x16 to match the luma channel.
    // do the same for the the chroma-green channel.
    idct8x8id_base(Od, Co, Qchroma);
    idct8x8id_base(Gd, Cg, Qchroma);
    scale_block(Os, Od);
    scale_block(Gs, Gd);

    // now convert from YCoCg back to RGB. the IDCT may have
    // resulted in values outside of the valid range, so clamping
    // to the range [0, 255] is necessary.
    for (size_t i = 0; i < 256; ++i)
    {
        int32_t y = Ym[i];
        int32_t co= Os[i];
        int32_t cg= Gs[i];
        int16_t t = y  - (cg >> 1);
        int16_t g = cg +  t;
        int16_t b = t  - (co >> 1);
        int16_t r = b  +  co;
        *RGBA++   = clamp(r);
        *RGBA++   = clamp(g);
        *RGBA++   = clamp(b);
        *RGBA++   =*A++;
    }
}
