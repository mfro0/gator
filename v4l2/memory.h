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

#ifndef GENERIC_MEMORY_HEADER
#define GENERIC_MEMORY_HEADER 1

inline unsigned long kvirt_to_pa(unsigned long adr);
void *rvmalloc(unsigned long size);
void rvfree(void *mem, unsigned long size);
void build_dma_table(DMA_BM_TABLE *ptr, u32 from_addr, u32 to_addr, u32 bufsize);

#endif
