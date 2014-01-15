/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines an interface to a file I/O system optimized for performing
/// reads into page-sized, page-aligned scratch memory, bypassing the kernel
/// disk cache. The alignment is necessary to increase the likelyhood that I/O
/// operations will be performed by DMA directly into the user-space buffer.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include "ioutils.hpp"

static inline bool ioq_order(uintptr_t offset, uintptr_t priority, io_queue_op_t const *op)
{
    // note: lower numeric priority indicates higher priority,
    // so the priority condition is inverted in the code below.
    uintptr_t oa  = offset;
    uintptr_t pa  = priority;
    uintptr_t ob  = op->Offset;
    uintptr_t pb  = op->Priority;
    return   (pa == pb) ? (oa < ob) : (pa < pb);
}

static inline bool ioq_order(io_queue_op_t const *a, io_queue_op_t const *b)
{
    // note: lower numeric priority indicates higher priority,
    // so the priority condition is inverted in the code below.
    uintptr_t oa  = a->Offset;
    uintptr_t pa  = a->Priority;
    uintptr_t ob  = b->Offset;
    uintptr_t pb  = b->Priority;
    return   (pa == pb) ? (oa < ob) : (pa < pb);
}

void io_queue_init(io_queue_t *ioq)
{
    ioq->Count = 0;
}

size_t io_queue_size(io_queue_t *ioq)
{
    return ioq->Count;
}

bool io_queue_add(io_queue_t *ioq, uintptr_t offset, uintptr_t priority)
{
    if (ioq->Count < IOQ_MAX_OPS)
    {
        size_t pos = ioq->Count++;
        size_t idx =(pos - 1) / 2;
        while (pos > 0 && ioq_order(offset, priority, &ioq->Items[idx]))
        {
            ioq->Items[pos] = ioq->Items[idx];
            pos = idx;
            idx =(pos - 1) / 2;
        }
        ioq->Items[pos].Priority = priority;
        ioq->Items[pos].Offset   = offset;
        return true;
    }
    return false;
}

bool io_queue_next(io_queue_t *ioq, uintptr_t *offset)
{
    if (ioq->Count > 0)
    {
        // the highest-priority item is at the root/front of the array.
        *offset = ioq->Items[0].Offset;

        // move the last item into the position vacated by the first item.
        size_t  count = ioq->Count - 1;
        ioq->Items[0] = ioq->Items[ioq->Count - 1];
        ioq->Count--;

        // now re-heapify, because moving the last item to the root
        // may have violated the heap order.
        size_t pos = 0;
        while (true)
        {
            size_t l = (2  * pos) + 1; // left child
            size_t r = (2  * pos) + 2; // right child
            size_t m;

            // determine the child node with the highest priority.
            if (l >= count) break; // node at pos has no children.
            if (r >= count) m = l; // node at pos has no right child.
            else m = ioq_order(&ioq->Items[l], &ioq->Items[r]) ? l : r;

            // compare the node at pos with the highest priority child.
            if (ioq_order(&ioq->Items[pos], &ioq->Items[m]))
            {
                // the children have lower priority than the parent.
                // the heap order has been restored.
                break;
            }

            // otherwise, swap the parent with the largest child.
            io_queue_op_t temp = ioq->Items[pos];
            ioq->Items[pos]    = ioq->Items[m];
            ioq->Items[m]      = temp;
            pos = m;
        }
        return true;
    }
    return false;
}

void io_queue_clear(io_queue_t *ioq)
{
    ioq->Count = 0;
}

#ifdef _MSC_VER
    #define STAT64_STRUCT struct __stat64
    #define STAT64_FUNC   _stat64
    #define FTELLO_FUNC   _ftelli64
    #define FSEEKO_FUNC   _fseeki64
#else
    #define STAT64_STRUCT struct stat
    #define STAT64_FUNC   stat
    #define FTELLO_FUNC   ftello
    #define FSEEKO_FUNC   fseeko
#endif /* defined(_MSC_VER) */

size_t file_contents(
    char const *path,
    void       *buffer,
    ptrdiff_t   buffer_offset,
    size_t      buffer_size,
    size_t     *file_size)
{
    FILE *file = fopen(path, "r+b");
    if (file == NULL)
    {
        if (file_size) *file_size = 0;
        return 0;
    }

    // figure out the total size of the input file, in bytes.
    int64_t beg =  FTELLO_FUNC(file);
    FSEEKO_FUNC(file, 0,   SEEK_END);
    int64_t end =  FTELLO_FUNC(file);
    FSEEKO_FUNC(file, 0,   SEEK_SET);
    size_t sz =  (size_t) (end - beg);

    // determine whether our buffer is large enough to hold the file data.
    uint8_t *read_ptr = ((uint8_t*) buffer) + buffer_offset;
    uint8_t *end_ptr  = ((uint8_t*) buffer) + buffer_size;
    if (read_ptr+sz   > end_ptr)
    {
        // the buffer isn't large enough to hold the complete file.
        if (file_size) *file_size = sz;
        fclose(file);
        return 0;
    }

    // read the entire file contents into the buffer and close the file.
    size_t nr = fread(read_ptr, 1, sz, file);
    fclose(file);
    if (file_size != NULL) *file_size = sz;
    return nr;
}

char *file_contents(char const *path, size_t *file_size)
{
    FILE *file = fopen(path, "r+b");
    if (file == NULL)
    {
        if (file_size) *file_size = 0;
        return NULL;
    }

    // figure out the total size of the input file, in bytes.
    int64_t beg =  FTELLO_FUNC(file);
    FSEEKO_FUNC(file, 0,   SEEK_END);
    int64_t end =  FTELLO_FUNC(file);
    FSEEKO_FUNC(file, 0,   SEEK_SET);
    size_t sz =  (size_t) (end - beg);

    // allocate a temporary buffer to hold the file contents.
    // @note: add 1 to the size for a terminating null byte.
    char  *fd =  (char*) malloc(sz + 1);
    if (NULL ==  fd)
    {
        fclose(file);
        if (file_size) *file_size = sz;
        return NULL;
    }

    // read the entire file contents into the buffer.
    // terminate the buffer with a NULL byte.
    size_t nr = fread(fd, 1, sz, file);
    fd[nr]    = 0;

    // we're done, close the file and return success.
    // use free_buffer to free the returned buffer.
    fclose(file);
    if (file_size) *file_size = nr;
    return fd;
}

void file_contents_free(void *buffer)
{
    if (buffer) free(buffer);
}

size_t compression_bound(size_t input_size)
{
    (void) input_size;
    return 0;
}

size_t compress_data(void * restrict dst, void const * restrict src, size_t n)
{
    (void) dst;
    (void) src;
    (void) n;
    return 0;
}

size_t decompress_data(void * restrict dst, void const * restrict src, size_t n)
{
    (void) dst;
    (void) src;
    (void) n;
    return 0;
}
