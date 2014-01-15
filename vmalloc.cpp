/*/////////////////////////////////////////////////////////////////////////////
/// @summary Defines an interface to the system virtual memory manager. Many 
/// optimal data transfer paths depend on being able to copy into page-aligned
/// memory regions; allocation through the VMM naturally provides this.
/// @author Russell Klenk (contact@russellklenk.com)
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#if defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
    #define VM_ALLOC_WINDOWS    1
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #define VM_ALLOC_WINDOWS    0
    #include <unistd.h>
    #include <execinfo.h>
    #include <sys/mman.h>
#endif

#include "vmalloc.hpp"

#if VM_ALLOC_WINDOWS

    size_t vmm_page_size(void)
    {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        return (size_t) sys_info.dwPageSize;
    }

    void *vmm_reserve(size_t size_in_bytes)
    {
        return VirtualAlloc(NULL, size_in_bytes, MEM_RESERVE, PAGE_NOACCESS);
    }

    bool vmm_commit(void *address, size_t size_in_bytes)
    {
        return (VirtualAlloc(address, size_in_bytes, MEM_COMMIT, PAGE_READWRITE) != NULL);
    }

    void vmm_release(void *address, size_t /*size_in_bytes*/)
    {
        VirtualFree(address, 0, MEM_RELEASE);
    }

#else

    size_t vmm_page_size(void)
    {
        return (size_t) sysconf(_SC_PAGESIZE);
    }

    void* vmm_reserve(size_t size_in_bytes)
    {
        int    flags       = MAP_PRIVATE | MAP_ANON;
        void  *map_result  = mmap(NULL, size_in_bytes, PROT_NONE, flags, -1, 0);
        return map_result != MAP_FAILED ? map_result : NULL;
    }

    bool vmm_commit(void *address, size_t size_in_bytes)
    {
        return (mprotect(address, size_in_bytes, PROT_READ | PROT_WRITE) == 0);
    }

    void vmm_release(void *address, size_t size_in_bytes)
    {
        munmap(address, size_in_bytes);
    }

#endif

