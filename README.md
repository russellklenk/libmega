# Introduction #

Under active development. Do not use.

# License #

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all copyright interest in the software to the public domain. We make this dedication for the benefit of the public at large and to the detriment of our heirs and successors. We intend this dedication to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

# Notepad #

Some terminology:
CSF = Contrast Sensitivity Function
MCU = Minimum Coded Unit, which is a 16x16 block of pixels.
DC and AC are DCT coefficients; DC (singular) represents zero spatial
frequency, the average value for all pixels in the 8x8 block, while AC
coefficients (plural) represent amplitudes of progressively higher
horizontal and vertical spatial frequency components.
DCT operates on 8x8 blocks (64 pixels at a time).
General process:
1. Transform color space
2. Downsample components
3. Interleave color planes
4. Partition into blocks
5. Apply DCT
6. Quantize the DC and AC coefficients
7. Apply entropy encoding to quantized coefficients (ie. RLE+Huffman)
See www.staffs.ac.uk/personal/engineering_and_technology/tsd1/multimediastreaming/Image and video compression.pdf
I think that H2V2 is actually 4:2:0; so this is the only code path we need.
  Yes, in fact it is. His 4x1x1 means 4 Y blocks of 8x8, and 1 8x8 block each for Cb and Cr.
  This means 6 blocks per-MCU, but we output an extra 4 blocks if alpha is present.
So, we'll only implement the H2V2 code path, with m_no_chroma_discrim_flag = false, m_two_pass_flag = true and YCoCg instead of YCbCr.
The quantization tables are constant for a given quality factor, but we recompute them for each block to avoid 'global' state. They
  only need to be written to the output page once, not once per-block.

m_comp_h_samp[0] = m_comp_v_samp[0] = 2
m_comp_h_samp[1] = m_comp_v_samp[1] = 1
m_comp_h_samp[2] = m_comp_v_samp[2] = 1
m_mcu_x = 16
m_mcu_y = 16
m_image_x = 16
m_image_y = 16
m_image_bpp = compressor_input_t::BytesPerPixel
m_image_bpl = compressor_input_t::BytesPerRow
m_image_x_mcu = 16
m_image_y_mcu = 16
m_image_bpl_xlt = 3 or 4 depending on whether alpha is present
m_image_bpl_mcu = 16 * 3 or 4 depending on whether alpha is present
m_mcus_per_row = 1
The MCU buffer is a max of 16 * 4 * 16 = 1024 bytes and has 16 rows/columns
https://jpeg-compressor.googlecode.com/svn/trunk/jpge.h
https://jpeg-compressor.googlecode.com/svn/trunk/jpge.cpp

We want to perform the following steps to make the data in each block more
compressable. Everything acts as if the data is specified as RGBA8, if not
the extra channels are just discarded at the end of the process. The goals
are to maximize the amount of data loaded per-I/O op, and to minimize the
time needed for decompression.

1. Accept 16x16 block of input pixels, Q factor, and output channel count.
2. Convert 16x16 block of RGBA input into 16x16 Y, 8x8 Co, 8x8 Cg, 16x16 A.
3. Note that the alpha channel will likely compress extremely well as-is.
   Maybe we should simply store it separately, and re-interleave it at load?
4. Output 256 bytes of zig-zag ordered DCT coefficients for RGB and 256
   bytes of uncompressed alpha.

we're dealing with 2D, uncompressed RGB and RGBA images only.
we have a description of the uncompressed source image.
we maintain a current x, y offset into the source image.

the order in which we'll tackle these problems is:
1. Be able to chunk a source image into fixed-size pages suitable for
   streaming. - DONE
2. Figure out how to group the pages associated with a single image, and
   for all frames in an animation. Pages should be ordered so that pages
   that will be needed together can be stored together in the order they
   will be needed.
3. Figure out how to load these pages from disk into RAM efficiently. An I/O
   request should be able to load one or more pages. Buffering should be
   easy since each page is the same size, though ideally a DMA is performed
   directly into pinned GPU driver memory (maybe?)
4. Assign coordinates in the virtual texture for a logical image. If an
   image contains multiple frames, space should be allocated in the virtual
   texture for each reference, since each reference could be displaying a
   different frame, though this behavior should be configurable. This is
   basically done when the application is allocating 'sprites'.
5. Get the rest of the system working, without compression.
6. Add in a compressor and decompressor.
