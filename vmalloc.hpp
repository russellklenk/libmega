/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines an interface to the system virtual memory manager. Many 
/// optimal data transfer paths depend on being able to copy into page-aligned
/// memory regions; allocation through the VMM naturally provides this.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

#ifndef VM_ALLOC_HPP
#define VM_ALLOC_HPP

/*////////////////
//   Includes   //
////////////////*/
#include <stddef.h>
#include <stdint.h>

/*////////////////
//  Data Types  //
////////////////*/

/*///////////////
//  Functions  //
///////////////*/
/// @summary Queries the operating system to determine the size of a single 
/// page within the virtual memory manager. This system page size is also 
/// typically the granularity of the system heap.
/// @return The number of bytes in a single page managed by the virtual memory
/// system of the host operating system.
size_t vmm_page_size(void);

/// @summary Reserves a block of contiguous address space within the memory 
/// map of the calling process. Do not attempt to read from or write to the 
/// returned address block until the pages are committed with vmm_commit().
/// @param size_in_bytes The number of bytes to reserve. This size will be 
/// rounded up to the nearest multiple of the system page size.
/// @return A pointer to the start of the reserved address space, or NULL. The
/// address will be aligned to the system page size.
void* vmm_reserve(size_t size_in_bytes);

/// @summary Commits a block of contiguous address space, backing it with 
/// physical memory or space in the system page file. Committed pages are both
/// readable and writable.
/// @param address The start of the address range to commit. This may be a 
/// subset of the address space reserved using vmm_reserve(). This address
/// must be aligned to the system page size.
/// @param size_in_bytes The number of bytes to commit. This will be rounded 
/// up to the nearest multiple of the system page size.
/// @return true if the address space is successfully committed.
bool vmm_commit(void *address, size_t size_in_bytes);

/// @summary De-commits and releases a block of contiguous address space 
/// previously reserved with vmm_reserve() and vmm_commit(). The 
/// address space may contain a mixture of reserved and committed blocks.
/// @parm address The address returned by vmm_reserve(). The entire range
/// of address space is de-committed and released to the operating system.
/// @param size_in_bytes The number of bytes requested by the original call to
/// vmm_reserve().
void vmm_release(void *address, size_t size_in_bytes);

/// @summary Adjusts an address value such that it is aligned to a particular
/// power-of-two boundary. If the address is already an even multiple of the 
/// specified alignment, it is not modified.
/// @param address The address to adjust.
/// @param pow2 The desired alignment value. This value must be a power-of-two.
/// @return The aligned address.
template <typename T>
static inline T* align_to(T *address, size_t pow2)
{
    return (T*) ((((size_t)address) + (pow2-1)) & ~(pow2-1));
}

/// @summary Adjusts a size value such that it is an even multiple of a 
/// particular power-of-two size; the returned value is always greater than 
/// zero. If the size is already an even multiple of the specified value, it
/// is not modified.
/// @param size The size to adjust.
/// @param pow2 The desired alignment value. This value must be a power-of-two.
/// @return The adjusted size value.
static inline size_t align_up(size_t size, size_t pow2)
{
    return (size != 0) ? ((size + (pow2-1)) & ~(pow2-1)) : pow2;
}

#endif /* !defined(VM_ALLOC_HPP) */

