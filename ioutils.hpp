/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines an interface to a file I/O system optimized for performing
/// reads into page-sized, page-aligned scratch memory, bypassing the kernel
/// disk cache. The alignment is necessary to increase the likelyhood that I/O
/// operations will be performed by DMA directly into the user-space buffer.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

#ifndef IO_UTILS_HPP
#define IO_UTILS_HPP

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
#define restrict                __restrict
#endif

/// @summary Define the maximum number of operations that can be pending at
/// any one time, per-queue. Worst case (uintptr_t is 8 bytes) this results
/// in 8KB per-queue.
#ifndef IOQ_MAX_OPS
#define IOQ_MAX_OPS             512U
#endif

/// @summary Specifies some pre-defined priority values for I/O operations.
/// Priority values decrease as they increase numerically, so the minimum
/// priority has a value of zero.
enum io_priority_e
{
    IO_PRIORITY_MAX             = 0,
    IO_PRIORITY_NORMAL          = 127,
    IO_PRIORITY_MIN             = 255
};

/// @summary Defines the different modes and hints that can be specified when
/// opening a file. Note the IO_FILE_DIRECT and IO_FILE_BUFFERED are mutually
/// exclusive, as are IO_FILE_SEQUENTIAL and IO_FILE_RANDOM.
enum io_file_mode_e
{
    /// @summary The file will be used for raw I/O, bypassing the kernel
    /// page cache. Reads and writes must be performed in multiples of the
    /// disk physical sector size.
    IO_FILE_DIRECT              = (1 << 0),
    /// @summary The file will be used for buffered I/O, using the kernel
    /// page cache. Reads and writes do not have any special restrictions.
    IO_FILE_BUFFERED            = (1 << 1),
    /// @summary A hint to optimize for sequential access within the file.
    IO_FILE_SEQUENTIAL          = (1 << 2),
    /// @summary A hint to optimize for random access within the file.
    IO_FILE_RANDOM              = (1 << 3),
    /// @summary Force values to be a minimum of 32-bits.
    IO_FILE_MODE_FORCE_32BIT    = 0x7FFFFFFL,
};

/// @summary Defines the types of access the application is requesting when
/// opening a file. None of these values are mutually exclusive.
enum io_file_access_e
{
    /// @summary Open the file for reading data.
    IO_FILE_READ                = (1 << 0),
    /// @summary Open the file for writing data.
    IO_FILE_WRITE               = (1 << 1),
    /// @summary Open or create the file in truncate mode.
    /// Implies IO_FILE_WRITE.
    IO_FILE_CREATE              = (1 << 2),
    /// @summary Open or create the file in append mode.
    /// Implies IO_FILE_WRITE.
    IO_FILE_APPEND              = (1 << 3),
    /// @summary Force values to be a minimum of 32-bits.
    IO_FILE_ACCESS_FORCE_32BIT  = 0x7FFFFFFFL,
};

/// @summary Defines the various seeking behaviors used to position the file
/// pointer within the file for a subsequent read or write operation.
enum io_seek_mode_e
{
    /// @summary The seek will be performed relative to the start of the file;
    /// the offset value specifies the absolute offset within the file.
    IO_SEEK_FROM_START          = 0,
    /// @summary The seek will be performed relative to the current file
    /// pointer; the offset value specifies a relative offset within the file.
    IO_SEEK_FROM_CURRENT        = 1,
    /// @summary The seek will be performed relative to the end of the file;
    /// the offset value is typically negative.
    IO_SEEK_FROM_END            = 2,
    /// @summary Force values to be a minimum of 32-bits.
    IO_SEEK_MODE_FORCE_32BIT    = 0x7FFFFFFFL,
};

/// @summary Represents a single open file. This structure should be considered
/// opaque, as it is specified differently depending on the operating system.
struct file_t;

/// @summary Represents a single I/O operation within the I/O queue. I/Os are
/// ordered by priority, and then by starting offset. The offset is used to
/// uniquely identify the I/O operation.
struct io_queue_op_t
{
    uintptr_t     Offset;               /// Absolute byte offset
    uintptr_t     Priority;             /// Priority value (immediacy)
};

/// @summary Represents a queue of pending I/O operations. Each operation is
/// uniquely identified by its starting offset.
struct io_queue_t
{
    size_t        Count;                /// The number of items in the queue
    io_queue_op_t Items[IOQ_MAX_OPS];   /// Storage for I/O operations
};

/*///////////////
//  Functions  //
///////////////*/
/// @summary Initializes an I/O queue to empty.
/// @param ioq The queue to initialize.
void   io_queue_init(io_queue_t *ioq);

/// @summary Retrieves the number of pending operations in the queue.
/// @param ioq The queue to query.
/// @return The number of pending operations in the queue.
size_t io_queue_size(io_queue_t *ioq);

/// @summary Adds an operation to the queue. The I/O operation is uniquely
/// identified by its starting offset value.
/// @param ioq The target I/O queue.
/// @param offset The absolute byte offset of the start of the operation.
/// @param priority The priority value indicating the immediate need of the I/O.
/// @return true if the I/O operation was added to the queue.
bool   io_queue_add(io_queue_t *ioq, uintptr_t offset, uintptr_t priority);

/// @summary Retrieves and removes the next pending I/O operation.
/// @param ioq The I/O queue to query.
/// @param offset On return, this address is updated with the absolute byte
/// offset of the highest-priority I/O operation.
/// @return true if an item was restrieved from the queue.
bool   io_queue_next(io_queue_t *ioq, uintptr_t *offset);

/// @summary Removes all items from the queue.
/// @param ioq The queue to clear.
void   io_queue_clear(io_queue_t *ioq);

/// @summary Reads the entire contents of a file into a caller-managed buffer.
/// @param path The path of the file to read.
/// @param buffer The buffer into which the file contents will be read. Data is
/// only written to the buffer if the entire file can be stored.
/// @param buffer_offset The zero-based byte offset into the buffer at which to
/// begin writing data.
/// @param buffer_size The maximum number of bytes to write to the buffer.
/// @param file_size On return, this value is updated with the actual size of
/// the file data, specfieied in bytes.
/// @return The number of bytes read from the file and written to the buffer,
/// or zero if the buffer is not large enough to hold the file contents.
size_t file_contents(
    char const *path,
    void       *buffer,
    ptrdiff_t   buffer_offset,
    size_t      buffer_size,
    size_t     *file_size);

/// @summary Reads the entire contents of a file into a temporary buffer. The
/// buffer is guaranteed to be NULL-terminated. Free the allocated memory using
/// the file_contents_free() function.
/// @param path The path of the file to read.
/// @param file_size On return, this value is updated with the size of the file
/// data, not including the trailing NULL byte.
/// @return The temporary buffer, or NULL.
char*  file_contents(char const *path, size_t *file_size);

/// @summary Frees a temporary buffer returned by the file_contents() function.
/// @param buffer The pointer returned by the file_contents() function.
void   file_contents_free(void *buffer);

/// @summary Opens or creates a file for access.
/// @param path The path of the file to open.
/// @param mode One or more of the io_file_mode_e flags specifying whether to
/// open the file for direct access or buffered access.
/// @param access One or more of the io_file_access_e flags specifying the type
/// of access the application requires.
/// @return A pointer to the file object, or NULL.
file_t*  open_file(char const *path, int32_t mode, int32_t access);

/// @summary Determines the logical size of a file on disk.
/// @param path The path of the file to query.
/// @return The logical size of the file, in bytes, or zero.
uint64_t file_size(char const *path);

/// @summary Determines the logical size of an open file.
/// @param fp The file object to query.
/// @return The logical size of the file, in bytes.
uint64_t file_size(file_t *fp);

/// @summary Sets the file pointer within a file.
/// @param fp The file object.
/// @param from One of the values of the io_seek_mode_e enumeration specifying
/// how the offset value should be interpreted.
/// @param offset The seek offset, specified in bytes.
/// @return The new file pointer position, or -1 if an error occurred.
int64_t  seek_file(file_t *fp, int32_t from, int64_t offset);

/// @summary Gets the current value of the file pointer for a file.
/// @param fp The file object to query.
/// @return The current file pointer position, or -1 if an error occurred.
int64_t  file_position(file_t *fp);

/// @summary Retrieves the io_file_mode_e flags set when a file was opened.
/// @param fp The file object to query.
/// @return The io_file_mode_e flags specified when the file was opened.
int32_t  file_mode(file_t *fp);

/// @summary Synchronously reads data from a file opened in buffered mode.
/// @param fp The file object to read from.
/// @param buffer The buffer to store data read from the file.
/// @param offset The byte offset at which to begin writing data to the buffer.
/// @param amount The number of bytes to read from the file.
/// @param eof On return, this value is set to true if end of file was reached.
/// @return The number of bytes read from the file and written to the buffer.
size_t   read_file(file_t *fp, void *buffer, ptrdiff_t offset, size_t amount, bool *eof);

/// @summary Synchronously reads data from a file opened in direct mode. The
/// kernel page cache is bypassed and the disk controller DMA's directly into
/// the user-space buffer.
/// @param fp The file object to read from.
/// @param buffer The buffer to store data read from the file. This buffer must
/// be aligned to an even multiple of the disk physical sector size.
/// @param amount The number of bytes to read from the file. This amount must
/// be an even multiple of the disk physical sector size.
/// @param eof On return, this value is set to true if end of file was reached.
/// @return The number of bytes read from the file and written to the buffer.
size_t   read_file_direct(file_t *fp, void *buffer, size_t amount, bool *eof);

/// @summary Synchronously writes data to a file opened in buffered mode.
/// @param fp The file object to write to.
/// @param buffer The buffer containing the data to write.
/// @param offset The byte offset at which to begin reading data from buffer.
/// @param amount The number of bytes to write to the file.
/// @return The number of bytes written to the file.
size_t   write_file(file_t *fp, void const *buffer, ptrdiff_t offset, size_t amount);

/// @summary Synchronously writes data to a file opened in direct mode. The
/// kernel page cache is bypassed and the disk controller DMA's directly from
/// the user-space buffer.
/// @param fp The file object to write to.
/// @param buffer The buffer containing the data to write. This buffer must be
/// aligned to an even multiple of the disk physical sector size.
/// @param amount The number of bytes to write to the file. This amount must be
/// an even multiple of the disk physical sector size.
/// @return The number of bytes written to the file.
size_t   write_file_direct(file_t *fp, void const *buffer, size_t amount);

/// @summary Determines the physical sector size of the disk on which the given
/// file exists. The sector size is specified in bytes.
/// @param fp The file object to query.
/// @return The physical sector size, specified in bytes.
size_t   physical_sector_size(file_t *fp);

/// @summary Flushes any pending writes to a file opened in buffered mode. For
/// files opened in direct-access mode, this has no effect.
/// @param fp The file object.
void     flush_file(file_t *fp);

/// @summary Closes a file and frees its associated resources.
/// @param fp The file object to close.
void     close_file(file_t *fp);

/// @summary Given an input size, determines the maximum number of bytes that
/// can result if the input ends up being uncompressable. This is useful when
/// determining how large temporary buffers should be.
/// @param input_size The size of the input data, in bytes.
/// @return The number of bytes that can result if the input is uncompressable.
size_t   compression_bound(size_t input_size);

/// @summary Compresses input data using the Finite State Entropy system:
/// http://fastcompression.blogspot.com/2014/01/fse-decoding-how-it-works.html
/// The compression is lossless.
/// @param dst The destination buffer. This should be at least as many bytes as
/// returned by calling compression_bound_fse(size_in_bytes).
/// @param src The source data buffer.
/// @param size_in_bytes The size of the source data, in bytes.
/// @return The number of bytes of compressed data written to @a dst.
size_t   compress_data(
    void       * restrict dst,
    void const * restrict src,
    size_t                size_in_bytes);

/// @summary Decompresses data compressed using the Finite State Entropy system:
/// http://fastcompression.blogspot.com/2014/01/fse-decoding-how-it-works.html
/// @param dst The destination buffer. This should be large enough to hold the
/// entire uncompressed output.
/// @param src The buffer containing the compressed source data.
/// @param size_in_bytes The exact size of the uncompressed output, in bytes.
/// @return The number of bytes of decompressed data written to @a dst.
size_t   decompress_data(
    void       * restrict dst,
    void const * restrict src,
    size_t                size_in_bytes);

// the compression API needs to support streaming.
// Each file in the compressed stream is identified by the following data:
// 1. Page index
// 2. Offset into page (can be stored in 16 bits)
// 3. Attribute flags (16 bits)
// 4. Compressed size
// 5. Uncompressed size
//
// An archive of compressed files starts with the header at the beginning of
// the file, followed by the dictionary. This data is padded out to the next
// page boundary.
// Following this, and starting on a page boundary, are zero or more
// LZ4-compressed data blocks. Files within a data block are packed tightly,
// and the data block is padded out to the next page boundary. Files do not
// cross data block boundaries. Data blocks may be variable-length to accomodate
// large files.
// The total size of the archive is an even multiple of the page size.
//
// We have some trade-offs. We could compress each file individually, which
// reduces the overall compression ratio but allows each file to be decompressed
// independently. Or, we can pack multiple compressed files into a single data
// block, and compress the entire block, which gives better compression, but
// requires the entire block to be decompressed at once. I'm leaning towards
// the first approach, since it makes things simpler. Compressed files can still
// be tightly packed, since they are allowed to span page boundaries - they only
// need to be padded out to a 32-byte boundary for SIMD support. This works well
// for files that don't need to be streamed in chunks (like big movies.) The
// decompression can proceed on a per-page basis, even for streaming files as
// long as each stream maintains its own decompressor state (is there state? -
// kind of, see the fastcompression.blogspot.com front page, which has several
// postings about how to design a streaming API. It sounds like a 64KB buffer
// would be needed for each stream, which is probably fine, but it would be
// nice if that wasn't necessary or it could be smaller.)
//
// In any case, the compressor needs to operate in a stream-oriented fashion,
// since we could be dealing with large input files. But given that, we still
// need a format for streaming data. We could essentially just use the LZ4
// streaming format:
// http://fastcompression.blogspot.com/2013/04/lz4-streaming-format-final.html
// which is designed for this sort of thing, and then wrap it in our archive
// format (so it follows the restrictions outlined above.)

#endif /* !defined(IO_UTILS_HPP) */

