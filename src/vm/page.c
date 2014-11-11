#include "vm/page.h"
#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/block.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"

/* Creates a new page directory that has mappings for kernel
   virtual addresses, but none for user virtual addresses.
   Returns the new page directory, or a null pointer if memory
   allocation fails. */
uint32_t *
supdir_create (uint32_t vaddr) 
{
  /* Should this be a page from the kernel pool, perhaps? */
  return (uint32_t *) get_user_page ((uint8_t *)vaddr);
}

/* Useless at this point. */
void
supdir_destroy (uint32_t *pd) 
{
  if (pd == NULL)
    return;
  palloc_free_page (pd);
}

/* Returns the address of the page table entry for virtual
   address VADDR in page directory PD.
   If PD does not have a page table for VADDR, behavior depends
   on CREATE.  If CREATE is true, then a new page table is
   created and a pointer into it is returned.  Otherwise, a null
   pointer is returned. */
uint64_t *
lookup_sup_page (uint32_t *pd, const void *vaddr, bool create)
{
  uint64_t *pt;
  uint32_t *pde;

  ASSERT (pd != NULL);

  /* Shouldn't create new kernel virtual mappings. */
  ASSERT (!create || is_user_vaddr (vaddr));

  /* Check for a page table for VADDR.
     If one is missing, create one if requested. */
  pde = pd + pd_no (vaddr);
  if (*pde == 0) 
    {
      if (create)
        {
          pt = (uint64_t *) get_user_page ((uint8_t *) vaddr);
          if (pt == NULL) 
            return NULL; 
          *pde = (uint32_t) pt;
        }
      else
        return NULL;
    }

  /* Return the page table entry. */
  pt = (uint64_t *) *pde;
  return &pt[pt_no (vaddr)];
}

/* Adds a mapping in page directory PD from user virtual page
   UPAGE to the physical frame identified by kernel virtual
   address KPAGE.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   If WRITABLE is true, the new page is read/write;
   otherwise it is read-only.
   Returns true if successful, false if memory allocation
   failed. */
bool
supdir_set_page (uint32_t *pd, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location, bool writable)
{
  uint64_t *spte;
  ASSERT (pg_ofs (vaddr) == 0);
  ASSERT (is_user_vaddr (vaddr));

  spte = lookup_sup_page (pd, (const void *) vaddr, true);

  if (spte != NULL) 
    {
    	uint64_t large_read_bytes = read_bytes;
      uint64_t large_location = (uint64_t) 0 | location;
      uint64_t large_writable = (uint64_t) 0 | writable;
      *spte = (large_writable << WRITABLE_SHIFT) | (large_read_bytes << READ_BYTES_SHIFT) | (large_location << LOC_SHIFT) | sector;
      return true;
    }
  else
    return false;
}

/* Looks up and returns the Sector number that corresponds to user virtual
   address VADDR in PD.   Returns a null pointer if
   VADDR is unmapped. */
block_sector_t
supdir_get_sector (uint32_t *pd, const void *vaddr) 
{
  ASSERT (is_user_vaddr (vaddr));
  return (block_sector_t) lookup_sup_page (pd, vaddr, false);
}

/* Marks user virtual page UPAGE "not present" in page
   directory PD.  Later accesses to the page will fault.  Other
   bits in the page table entry are preserved.
   UPAGE need not be mapped. */
void
supdir_clear_page (uint32_t *pd, void *upage) 
{
  uint64_t *spte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  spte = lookup_sup_page (pd, upage, false);
  if (spte != NULL)
    {
      *spte = 0;
    }
}

bool
load_page (void *vpage, void *frame)
{
  uint64_t *spte = lookup_sup_page (thread_current ()->supdir, vpage, false);
  if (spte != NULL)
    {
      uint64_t entry = *spte;
      uint8_t location = location_from_spte (entry);
      int32_t read_bytes = (int32_t) read_bytes_from_spte (entry);
      uint32_t zero_bytes = PGSIZE - read_bytes;
      uint32_t sector = sector_from_spte (entry);
      //printf ("About to attempt to read %d bytes from %s, starting at sector %d\n", read_bytes, location == FILE_SYS ? "file system " : "swap ", sector);
      bool writable = writable_from_spte (entry);
      pagedir_set_page (thread_current ()->pagedir, (void *) vpage, (void *) frame, writable);
      //printf ("Mapped virtual page %p to physical frame %p\n", vpage, frame);
      struct block *swap_device = block_get_role (BLOCK_SWAP);
      if (location == FILE_SYS || location == SWAP_SYS)
        {
          struct block *block_device = (location == FILE_SYS) ? fs_device : swap_device;
          while (read_bytes > 0)
            {
              //printf ("About to block_read sector %d, %d bytes left to read\n", sector, read_bytes);
              block_read (block_device, sector, frame);
              sector++;
              if (read_bytes >= BLOCK_SECTOR_SIZE)
                frame += BLOCK_SECTOR_SIZE;
              else
                frame += read_bytes;
              read_bytes -= BLOCK_SECTOR_SIZE;
            }
        }
      if (zero_bytes > 0)
        memset (frame, 0, zero_bytes);
      return true;
    }
  else
    return false;
}

uint8_t
location_from_spte (uint64_t entry)
{
  return (entry >> LOC_SHIFT) & LOC_MASK;
}

uint32_t
read_bytes_from_spte (uint64_t entry)
{
  return (entry >> READ_BYTES_SHIFT) & READ_BYTES_MASK;
}

uint32_t
sector_from_spte (uint64_t entry)
{
  return (uint32_t) entry;
}

bool
writable_from_spte (uint64_t entry)
{
  return (entry >> WRITABLE_SHIFT) & WRITABLE_MASK;
}

bool 
load_stack_pg (void *vpage, void *frame)
{
  memset (frame, 0, PGSIZE);
  return pagedir_set_page (thread_current ()->pagedir, (void *) vpage, (void *) frame, true);
}
