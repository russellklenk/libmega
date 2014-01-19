#include <stdio.h>
#include <stdlib.h>
#include "imutils.hpp"

template <typename T>
static void print_8x8_dec(T const *I)
{
    for (size_t row = 0, i = 0; row < 8; ++row, i += 8)
    {
        printf(
            "% 5d % 5d % 5d % 5d  % 5d % 5d % 5d % 5d\n",
            (int) I[i+0], (int) I[i+1], (int) I[i+2], (int) I[i+3],
            (int) I[i+4], (int) I[i+5], (int) I[i+6], (int) I[i+7]);
    }
    printf("\n");
}

static void print_8x8_hex(int16_t const *I)
{
    for (size_t row = 0, i = 0; row < 8; ++row, i += 8)
    {
        printf(
            "%04X %04X %04X %04X  %04X %04X %04X %04X\n",
            (int) I[i+0], (int) I[i+1], (int) I[i+2], (int) I[i+3],
            (int) I[i+4], (int) I[i+5], (int) I[i+6], (int) I[i+7]);
    }
    printf("\n");
}

template <typename T>
static void print_16x16_dec(T const *B)
{
    for (size_t row = 0, i = 0; row < 16; ++row, i += 16)
    {
        printf(
            "%3d %3d %3d %3d  %3d %3d %3d %3d  %3d %3d %3d %3d  %3d %3d %3d %3d\n",
            (int) B[i+0],  (int) B[i+1],  (int) B[i+2],  (int) B[i+3],
            (int) B[i+4],  (int) B[i+5],  (int) B[i+6],  (int) B[i+7],
            (int) B[i+8],  (int) B[i+9],  (int) B[i+10], (int) B[i+11],
            (int) B[i+12], (int) B[i+13], (int) B[i+14], (int) B[i+15]);
    }
    printf("\n");
}

static void print_16x16_hex(uint8_t const *B)
{
    for (size_t row = 0, i = 0; row < 16; ++row, i += 16)
    {
        printf(
            "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            (int) B[i+0],  (int) B[i+1],  (int) B[i+2],  (int) B[i+3],
            (int) B[i+4],  (int) B[i+5],  (int) B[i+6],  (int) B[i+7],
            (int) B[i+8],  (int) B[i+9],  (int) B[i+10], (int) B[i+11],
            (int) B[i+12], (int) B[i+13], (int) B[i+14], (int) B[i+15]);
    }
    printf("\n");
}

static void merge_int16(int16_t * restrict dst, int16_t const * restrict src)
{
    // combine four 8x8 blocks stored in a contiguous array back into a
    // single 16x16 block. the 8x8 blocks are stored in natural order.
    int16_t const * restrict src0 = &src[0];     // src[0...63]
    int16_t const * restrict src1 = &src[64];    // src[64...127]
    int16_t const * restrict src2 = &src[128];   // src[128...191]
    int16_t const * restrict src3 = &src[192];   // src[192...255]
    int16_t       * restrict dst0 = &dst[0];     // col[0...7],  row[0...7]
    int16_t       * restrict dst1 = &dst[8];     // col[8...15], row[0...7]
    int16_t       * restrict dst2 = &dst[128];   // col[0...7],  row[8...15]
    int16_t       * restrict dst3 = &dst[128+8]; // col[8...15], row[8...15]
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

static void scale_int16(int16_t * restrict dst, int16_t const * restrict src)
{
    // scale an 8x8 block into a 16x16 block using pixel doubling.
    int16_t const * restrict srcRow  = src;
    int16_t       * restrict dstRow0 =&dst[0];
    int16_t       * restrict dstRow1 =&dst[16];
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

static void generate_rgba(uint8_t  *dst, uint8_t start, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        *dst++  = (uint8_t)start  + i + 0;
        *dst++  = (uint8_t)start  + i + 1;
        *dst++  = (uint8_t)start  + i + 2;
        *dst++  = 0xFF;
    }
}

static void transform_block(uint8_t const *rgba, int quality)
{
    int16_t Qluma[64];
    int16_t Qchroma[64];
    qtables_encode(Qluma, Qchroma, quality);

    printf("Qluma_encode:\n");
    print_8x8_dec(Qluma);
    printf("Qchroma_encode:\n");
    print_8x8_dec(Qchroma);

    int16_t Y[256]; // 4 8x8 blocks
    int16_t Co[64]; // 1 8x8 block
    int16_t Cg[64]; // 1 8x8 block
    uint8_t A[256]; // 1 16x16 block; not transformed or quantized
    encode16x16i(Y, Co, Cg, A, Qluma, Qchroma, rgba);

    printf("Y:\n");
    print_8x8_dec(&Y[0]);
    print_8x8_dec(&Y[64]);
    print_8x8_dec(&Y[128]);
    print_8x8_dec(&Y[192]);

    int16_t M[256]; // 8x8 blocks merged back into one 4x4 block.
    merge_int16(M, Y);
    printf("Ymerged:\n");
    print_16x16_dec(M);

    int16_t Os[256];
    int16_t Gs[256];
    scale_int16(Os, Co);
    scale_int16(Gs, Cg);

    printf("Co:\n");
    print_8x8_dec(Co);

    printf("Coscaled:\n");
    print_16x16_dec(Os);

    printf("Cg:\n");
    print_8x8_dec(Cg);

    printf("Cgscaled:\n");
    print_16x16_dec(Gs);

    // now do the decode part:
    qtables_decode(Qluma, Qchroma, quality);
    printf("Qluma_decode:\n");
    print_8x8_dec(Qluma);
    printf("Qchroma_decode:\n");
    print_8x8_dec(Qchroma);

    uint8_t RGBA[1024];
    decode16x16i_rgba(RGBA, Y, Co, Cg, A, Qluma, Qchroma);
    printf("RGBA input:\n");
    print_16x16_dec(rgba);
    printf("RGBA output:\n");
    print_16x16_dec(RGBA);
}

void transform_colorspace(uint8_t const *rgba)
{
    // this is YCoCg-R. see the paper:
    // http://research.microsoft.com/pubs/102040/2008_colortransforms_malvarsullivansrinivasan.pdf
    for (size_t i  = 0; i < 256; ++i)
    {
        int16_t R  = *rgba++;
        int16_t G  = *rgba++;
        int16_t B  = *rgba++;
        int16_t A  = *rgba++;
        int16_t Co = R  -  B;
        int16_t t  = B  + (Co >> 1);
        int16_t Cg = G  -  t;
        int16_t Y  = t  + (Cg >> 1);
        int16_t T  = Y  - (Cg >> 1);
        int16_t g  = Cg +  T;
        int16_t b  = T  - (Co >> 1);
        int16_t r  = b  +  Co;
        printf("RGB: % 3d % 3d % 3d => YCoCg: % 3d % 3d % 3d => RGB: % 3d % 3d % 3d\n",
                R, G, B, Y, Co, Cg, r, g, b);
        (void) A;
    }
}

void ycocg_range(void)
{
    // for all combinations of RGB, convert to YCoCg and
    // determine the min and max for each component.
    int16_t minY   = 0x7FFF;
    int16_t minCo  = 0x7FFF;
    int16_t minCg  = 0x7FFF;
    int16_t maxY   = 0xFFFF;
    int16_t maxCo  = 0xFFFF;
    int16_t maxCg  = 0xFFFF;
    for (int16_t r = 0; r < 256; ++r)
    {
        for (int16_t g = 0; g < 256; ++g)
        {
            for (int16_t b = 0; b < 256; ++b)
            {
                int16_t Co = r  -  b;
                int16_t t  = b  + (Co >> 1);
                int16_t Cg = g  -  t;
                int16_t Y  = t  + (Cg >> 1);
                if (Y  < minY)  minY  =  Y;
                if (Y  > maxY)  maxY  =  Y;
                if (Co < minCo) minCo = Co;
                if (Co > maxCo) maxCo = Co;
                if (Cg < minCg) minCg = Cg;
                if (Cg > maxCg) maxCg = Cg;
            }
        }
    }
    printf("minY:  % 5d    maxY:  % 5d\n", minY,  maxY);
    printf("minCo: % 5d    maxCo: % 5d\n", minCo, maxCo);
    printf("minCg: % 5d    maxCg: % 5d\n", minCg, maxCg);
}

int main(int argc, char **argv)
{
    (void) argc; // unused
    (void) argv; // unused

    uint8_t  RGBA[1024];
    generate_rgba(RGBA, 0, 256);
    transform_block(RGBA,  10);
    //transform_colorspace(RGBA);
    //ycocg_range();
    return 0;
}

