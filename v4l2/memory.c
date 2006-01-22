/* 
    This file is part of genericv4l.

    genericv4l is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    genericv4l is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


/* Memory management stuff goes here */

#define __NO_VERSION__

#include <linux/vmalloc.h>
#ifdef KERNEL2_4
#include <linux/wrapper.h>
#endif

#include "generic.h"
#include "mach64.h"
#include "rage128.h"
#include "memory.h"

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */
inline unsigned long kvirt_to_pa(unsigned long adr)
{
        unsigned long kva, ret;

        kva = (unsigned long) page_address(vmalloc_to_page((void *)adr));
        kva |= adr & (PAGE_SIZE-1); /* restore the offset */
        ret = __pa(kva);
        return ret;
}

void *rvmalloc(unsigned long size)
{
   void *mem;
   unsigned long adr;

   size = PAGE_ALIGN(size);
   if ((mem = vmalloc_32(size)) == NULL){
     printk(KERN_INFO "vmalloc_32(%ld) failed\n",size);
   } else {
     memset(mem, 0, size);
     adr = (unsigned long) mem;
     while (size > 0) {
     /* dont let vm swap this out, so we can map this to user space */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
       mem_map_reserve(vmalloc_to_page((void *)adr));
#else
       SetPageReserved(vmalloc_to_page((void *)adr));
#endif
       adr += PAGE_SIZE;
       size -= PAGE_SIZE;
     }
   }
   return mem;
}

void rvfree(void *mem, unsigned long size)
{
  unsigned long adr;

  if (mem == NULL)
    return;

  adr = (unsigned long) mem;
  while ((long) size > 0) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    mem_map_unreserve(vmalloc_to_page((void *)adr));
#else
    ClearPageReserved(vmalloc_to_page((void *)adr));
#endif
    adr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
  vfree(mem);
}

void build_dma_table(DMA_BM_TABLE *ptr, u32 from_addr, u32 to_addr, u32 bufsize)
{
  int i;

  for (i=0;bufsize >= PAGE_SIZE; i++){
    ptr[i].from_addr = from_addr + i*PAGE_SIZE;
    ptr[i].to_addr = to_addr + i*PAGE_SIZE;
    ptr[i].size = PAGE_SIZE; // amount to transfer?
/* ERROR NOT REQUIRED FOR MACH64 */
    ptr[i].size = PAGE_SIZE | RAGE128_BM_FORCE_TO_PCI; 
    ptr[i].reserved = 0;
    bufsize -= PAGE_SIZE;
  }

  /* if we finish with no bufsize left then it was a multiple of 4096(pagesize)
   * so lets redo the last page so we can make it stop correctly */
  if (bufsize < 1) {
    bufsize = PAGE_SIZE;
    i--;
  }

  ptr[i].from_addr = from_addr + i*PAGE_SIZE;
  ptr[i].to_addr = to_addr + i*PAGE_SIZE;
  ptr[i].size = bufsize | MACH64_DMA_GUI_COMMAND__EOL; //make it stop
/* ERROR NOT REQUIRED FOR MACH64 */
  ptr[i].size = bufsize | RAGE128_BM_FORCE_TO_PCI | MACH64_DMA_GUI_COMMAND__EOL; //make it stop
  ptr[i].reserved = 0;
}
