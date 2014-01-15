/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines an interface to a file I/O system optimized for performing
/// reads into page-sized, page-aligned scratch memory, bypassing the kernel
/// disk cache. The alignment is necessary to increase the likelyhood that I/O
/// operations will be performed by DMA directly into the user-space buffer.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

#define _DARWIN_USE_64BIT_INODE
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

/*////////////////
//   Includes   //
////////////////*/
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ioutils.hpp"

/// @summary Define the file_t structure for this operating system. This 
/// defines the data needed to access the file, and any safely-cached values.
struct file_t
{
    int     RawFD;      /// The file descriptor, for direct I/O
    FILE   *Stream;     /// The file stream, for buffered I/O
    size_t  SectorSize; /// The disk physical sector size
    int32_t ModeFlags;  /// The io_file_mode_e flags
};

/// @summary Determines whether a size is an even multiple of a value.
/// @param size The size value to check.
/// @param alignment The desired alignment. This value must be a power-of-two.
/// @return true if size is an even multiple of alignment.
static inline bool aligned_to(size_t size, size_t alignment)
{
    return ((size & (alignment - 1)) == 0);
}

/// @summary Determines whether an address is aligned to a value.
/// @param address The address to check.
/// @param alignment The destired alignment. This value must be a power-of-two.
/// @return true if the address has the specified alignment.
static inline bool aligned_to(void const *address, size_t alignment)
{
    return ((((size_t)address) & (alignment - 1)) == 0);
}

static inline bool is_set(int32_t bitflags, int32_t flag)
{
    return ((bitflags & flag) != 0);
}

file_t* open_file(char const *path, int32_t mode, int32_t access)
{
    // validate the mode flags. a file can't be opened for both 
    // direct and buffered access at the same time.
    if (is_set(mode, IO_FILE_DIRECT) && is_set(mode, IO_FILE_BUFFERED))
        return NULL;

    // validate the mode flags. a file can't be opened for both 
    // sequential and random access at the same time.
    if (is_set(mode, IO_FILE_SEQUENTIAL) && is_set(mode, IO_FILE_RANDOM))
        return NULL;

    // if a file is opened with create or append access, that implies write.
    if (is_set(access, IO_FILE_CREATE) || is_set(access, IO_FILE_APPEND))
        access |= IO_FILE_WRITE;

    // if a file is opened in append mode, that implies create.
    // clear the create bit if it is set to make our logic easier.
    if (is_set(access, IO_FILE_APPEND))
        access &=~IO_FILE_CREATE;

    // if a file is opened in write mode, that implies read access.
    // clear the read bit if it is set to make our logic easier.
    if (is_set(access, IO_FILE_WRITE))
        access &=~IO_FILE_READ;

    // determine the open mode parameters based on the access flags.
    char const *sm = NULL;   // stream mode (for fopen)
    int         rm = 0;      // raw mode (for open)

    if (is_set(access, IO_FILE_CREATE))
    {
        sm = "w+b";
        rm = O_RDWR | O_CREAT | O_TRUNC;
    }
    else if (is_set(access, IO_FILE_APPEND))
    {
        sm = "a+b";
        rm = O_RDWR | O_APPEND | O_CREAT;
    }
    else
    {
        if (is_set(access, IO_FILE_READ))
        {
            sm = "rb";
            rm = O_RDONLY;
        }
        if (is_set(access, IO_FILE_WRITE))
        {
            sm = "wb";
            rm = O_RDWR;
        }
    }
#ifdef __linux__
    if (is_set(mode, IO_FILE_DIRECT))
    {
        rm |= O_DIRECT;
    }
#endif

    // open the file using the low-level I/O interface.
    int raw_fd = open(path, rm);
    if (raw_fd < 0)
        return NULL;

    // attach a stream to the file if opening in buffered mode.
    FILE *stream = NULL;
    if (is_set(mode, IO_FILE_BUFFERED))
    {
        stream = fdopen(raw_fd, sm);
        if (NULL == stream)
        {
            close(raw_fd);
            return NULL;
        }
    }

#ifdef __APPLE__
    // OSX uses fcntl to disable I/O through the system page file.
    if (is_set(mode, IO_FILE_DIRECT))
    {
        int nocache = 1;
        fcntl(raw_fd, F_NOCACHE, &nocache);
    }
#endif

    // retrieve the physical block size for the disk containing the file.
    struct stat st;
    if (fstat(raw_fd, &st) < 0)
    {
        if (stream) fclose(stream); // also closes raw_fd
        else close(raw_fd);
        return NULL;
    }

    // allocate the file descriptor and populate it.
    file_t *fd = (file_t*) malloc(sizeof(file_t));
    if (NULL == fd)
    {
        if (stream) fclose(stream); // also closes raw_fd
        else close(raw_fd);
        return NULL;
    }
    fd->RawFD      = raw_fd;
    fd->Stream     = stream;
    fd->SectorSize = st.st_blksize;
    fd->ModeFlags  = mode;
    return fd;
}

uint64_t file_size(char const *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return (uint64_t) st.st_size;
    else
        return (uint64_t) 0;
}

uint64_t file_size(file_t *fp)
{
    off_t curr = lseek(fp->RawFD, 0, SEEK_CUR);
    off_t endp = lseek(fp->RawFD, 0, SEEK_END);
    lseek(fp->RawFD, curr, SEEK_SET);
    return (uint64_t)endp;
}

int64_t seek_file(file_t *fp, int32_t from, int64_t offset)
{
    int whence = SEEK_SET;
    switch (from)
    {
        case IO_SEEK_FROM_START:   whence = SEEK_SET; break;
        case IO_SEEK_FROM_CURRENT: whence = SEEK_CUR; break;
        case IO_SEEK_FROM_END:     whence = SEEK_END; break;
        default: return (uint64_t) lseek(fp->RawFD, 0, SEEK_CUR);
    }
    return (uint64_t) lseek(fp->RawFD, (off_t) offset, whence);
}

int64_t file_position(file_t *fp)
{
    return (uint64_t) lseek(fp->RawFD, 0, SEEK_CUR);
}

int32_t file_mode(file_t *fp)
{
    return fp->ModeFlags;
}

size_t read_file(file_t *fp, void *buffer, ptrdiff_t offset, size_t amount, bool *eof)
{
    if (fp->Stream)
    {
        uint8_t *buf = ((uint8_t*) buffer) + offset;
        size_t   num = fread(buf, amount, 1, fp->Stream);
        if (eof)*eof = feof(fp->Stream);
        return num;
    }
    else
    {
        // no buffered I/O interface available for this file.
        if (eof) *eof = false;
        return 0;
    }
}

size_t read_file_direct(file_t *fp, void *buffer, size_t amount, bool *eof)
{
    // @note: the call could return an error (EINVAL, in errno) if 
    // the file pointer is not also a multiple of the sector size.
    assert(aligned_to(buffer, fp->SectorSize));
    assert(aligned_to(amount, fp->SectorSize));
    ssize_t  num = read(fp->RawFD, buffer, amount);
    if (num >= 0)
    {
        if (eof) *eof = ((size_t) num) < amount;
        return (size_t) num;
    }
    else
    {
        // the call returned an error. in the case of EOF
        // it would have returned 0.
        if (eof) *eof = false;
        return (size_t) 0;
    }
}

size_t write_file(file_t *fp, void const *buffer, ptrdiff_t offset, size_t amount)
{
    if (fp->Stream)
    {
        uint8_t const *buf = ((uint8_t const*) buffer) + offset;
        return fwrite(buf, amount, 1, fp->Stream);
    }
    else return 0; // no buffered I/O interface available for this file.
}

size_t write_file_direct(file_t *fp, void const *buffer, size_t amount)
{
    // @note: the call could return an error (EINVAL, in errno) if 
    // the file pointer is not also a multiple of the sector size.
    assert(aligned_to(buffer, fp->SectorSize));
    assert(aligned_to(amount, fp->SectorSize));
    ssize_t num  = write(fp->RawFD, buffer, amount);
    return (num >= 0) ? (size_t) num : 0;
}

size_t physical_sector_size(file_t *fp)
{
    return fp->SectorSize;
}

void flush_file(file_t *fp)
{
    if (fp->Stream) fflush(fp->Stream);
}

void close_file(file_t *fp)
{
    if (fp)
    {
        if (fp->Stream)
        {
            // @note: also closes the underlying file descriptor.
            fclose(fp->Stream);
            fp->Stream = NULL;
        }
        else if (fp->RawFD >= 0)
        {
            close(fp->RawFD);
            fp->RawFD = -1;
        }
        free(fp);
    }
}
