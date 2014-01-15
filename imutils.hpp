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
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

#ifndef IM_UTILS_HPP
#define IM_UTILS_HPP

/*////////////////
//   Includes   //
////////////////*/
#include <stddef.h>
#include <stdint.h>

/*////////////////
//  Data Types  //
////////////////*/
/// @summary Define the restrict keyword, since most compilers still define
/// it using a compiler-specific name.
#ifndef restrict
#define restrict __restrict
#endif

/// @summary Defines the various border sampling modes.
enum border_mode_e
{
    /// @summary Border pixels are set from edge pixels.
    BORDER_CLAMP_TO_EDGE  = 0,
    /// @summary Border pixels are set to a constant color value.
    BORDER_CONSTANT_COLOR = 1,
    /// @summary The default border color source.
    BORDER_MODE_DEFAULT   = BORDER_CLAMP_TO_EDGE
};

/// @summary Describes a single tile output by the image tiler.
struct image_tile_t
{
    size_t   SourceX;        /// The x-coordinate on the source image, in pixels
    size_t   SourceY;        /// The y-coordinate on the source image, in pixels
    size_t   SourceWidth;    /// The width on the source image, in pixels
    size_t   SourceHeight;   /// The height on the source image, in pixels
    size_t   TileX;          /// Column index of the tile
    size_t   TileY;          /// Row index of the tile
    size_t   TileIndex;      /// Absolute index of the tile
    size_t   TileWidth;      /// The tile width, in pixels
    size_t   TileHeight;     /// The tile height, in pixels
    size_t   BytesPerRow;    /// The number of bytes per-row
    size_t   BytesPerTile;   /// The number of bytes in the output buffer
    void    *Pixels;         /// The output pixel data
};

/// @summary Describes the image tiler configuration options.
struct image_tiler_config_t
{
    size_t   TileWidth;      /// The width of a single tile, in pixels
    size_t   TileHeight;     /// The height of a single tile, in pixels
    size_t   ImageWidth;     /// The width of the source image, in pixels
    size_t   ImageHeight;    /// The height of the source image, in pixels
    size_t   BorderSize;     /// The border dimension, in pixels
    int32_t  BorderMode;     /// One of the chunker_border_e values
    uint32_t BorderColor;    /// A one-pixel buffer for the border color
    void    *Pixels;         /// The source image pixels
};

/*///////////////
//  Functions  //
///////////////*/
/// @summary Calculates the number of tiles output gi
/// @param num_x On return, stores the number of tiles in a single row.
/// @param num_y On return, stores the number of tiles in a single column.
/// @param config The image tiler configuration.
/// @return The total number of tiles.
size_t tile_count(size_t *num_x, size_t *num_y, image_tiler_config_t const *config);

/// @summary Allocates memory for a single output tile using the standard C
/// library function malloc(). Free the tile with tile_free().
/// @param tile The tile structure to initialize.
/// @param config The chunker configuration describing the tile dimensions and
/// pixel format of the image data.
/// @return true if allocation was successful.
bool tile_alloc(image_tile_t *tile, image_tiler_config_t const *config);

/// @summary Frees memory for a single output tile allocated with tile_alloc()
/// using the standard C library function free().
/// @param tile The tile to free.
void tile_free(image_tile_t *tile);

/// @summary Extracts a single tile from the source image.
/// @param tile The destination tile memory.
/// @param config The chunker configuration describing the input image.
/// @param index The zero-based index of the tile to retrieve.
/// @return true if the tile was copied successfully.
bool copy_tile(image_tile_t *tile, image_tiler_config_t const *config, size_t index);

/// @summary Calculates a set of quantization coefficients given a set of base
/// coefficients and a user-controllable quality factor. The quantization table
/// does not include scale factors.
/// @param Q A buffer of 64 values to store the quantization coefficients.
/// @param Qbase 64 values specifying the base quantization coefficients.
/// @param quality The user-controllable quality factor, in [1, 100].
void quantization_table_base(
    int32_t       * restrict Q,
    int16_t const * restrict Qbase,
    int                      quality);

/// @summary Calculates a set of scaled quantization coefficients given a set 
/// base coefficients (generated by quantization_table_base/luma/chroma/etc.) 
/// The quantization table coefficients are output in zig-zag order.
/// @param Qidct A 64-element buffer storing coefficients for use with IDCT.
/// @param Qfdct A 64-element buffer storing coefficients for use with FDCT.
/// @param Qbase A 64-element table generated by quantization_table_base(), 
/// quantization_table_luma() or quantization_table_chroma() specifying the 
/// un-scaled quantization coefficients for a given quality level.
void quantization_table_scaled(
    float         * restrict Qidct, 
    float         * restrict Qfdct, 
    int32_t const * restrict Qbase);

/// @summary Calculates a set of quantization coefficients for the luma channel
/// using the standard JPEG base coefficients and a user-controllable quality. 
/// The quantization table does not include scale factors.
/// @param Qluma A buffer of 64 values to store the quantization coefficients.
/// @param quality The user-controllable quality factor, in [1, 100].
void quantization_table_luma(int32_t *Qluma, int quality);

/// @summary Calculates a set of quantization coefficients for the chroma
/// channels using the standard JPEG base coefficients and a user-controllable
/// quality value. The quantization table does not include scale factors.
/// @param Qchroma A buffer of 64 values to store the quantization coefficients.
/// @param quality The user-controllable quality factor, in [1, 100].
void quantization_table_chroma(int32_t *Qchroma, int quality);

/// @summary Calculates the quantization tables used for encoding image data at
/// a given quality level. This function should be called once per-image.
/// @param Qluma A 64-element buffer to store the luma quantization table for 
/// use with a forward discrete cosine transform operation (FDCT).
/// @param Qchroma A 64-element buffer to store the chroma quantization table 
/// for use with a forward discrete cosine transform operation (FDCT).
/// @param quality The user-controllable quality factor, in [1, 100].
void quantization_tables_encode(
    float * restrict Qluma, 
    float * restrict Qchroma, 
    int              quality);

/// @summary Calculates the quantization tables used for decoding image data at
/// a given quality level. This function should be called once per-image.
/// @param Qluma A 64-element buffer to store the luma quantization table for
/// use with an inverse discrete cosine transform operation (IDCT).
/// @param Qchroma A 64-element buffer to store the chroma quantization table 
/// for use with an inverse discrete cosine transform operation (IDCT).
/// @param quality The user-controllable quality factor, in [1, 100].
void quantization_tables_decode(
    float * restrict Qluma, 
    float * restrict Qchroma, 
    int              quality);

/// @summary Executes a discrete cosine transform operation for an 8x8 block of
/// input data representing a single color channel. This is an encode operation.
/// @param dst A 64-element buffer to be filled with descaled and quantized 
/// DCT coefficient values.
/// @param src A 64-element buffer containing the 8x8 block of sample values.
/// @param Qfdct The scaled quantization table for the FDCT.
void fdct8x8(
    float       * restrict dst, 
    float const * restrict src,
    float const * restrict Qfdct);

/// @summary Executes an inverse discrete cosine transform operation for an 
/// 8x8 block of input data representing the quantized DCT coefficients for a
/// single color channel. This is a decode operation.
/// @param dst A 64-element buffer to be filled with de-quantized sample data.
/// @param src A 64-element buffer containing the quantized DCT coefficients.
/// @param Qidct The scaled quantization table for the IDCT.
void idct8x8(
    float       * restrict dst, 
    float const * restrict src, 
    float const * restrict Qidct);

/// @summary Transforms an input block of 16x16 RGBA8 pixels into a form more
/// amenable to compression via traditional lossless methods. Note that the
/// quantization table does not need to be stored with the output; it will
/// suffice to store the quality value used to generate the tables.
/// @param Y A buffer of 256 values to store the four 8x8 blocks of luma.
/// @param Co A buffer of 64 values to store the 8x8 block of chroma-orange.
/// @param Cg A buffer of 64 values to store the 8x8 block of chroma-green.
/// @param A A buffer of 256 values to store the 16x16 block of alpha.
/// @param Qy The 64 quantization coefficients for the luma channel.
/// @param Qc The 64 quantization coefficients for the chroma channels.
/// @param RGBA The 16x16 (1024 byte) block of RGBA8 input pixels.
void encode_block_dct(
    float         * restrict Y,
    float         * restrict Co,
    float         * restrict Cg,
    float         * restrict A,
    float   const * restrict Qluma,
    float   const * restrict Qchroma,
    uint8_t const * restrict RGBA);

#endif /* !defined(IM_UTILS_HPP) */
