#include "vm/page.h"
#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/palloc.h"
#include "devices/block.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "filesys/filesys.h"

/* Creates a new page directory that has mappings for kernel
   virtual addresses, but none for user virtual addresses.
   Returns the new page directory, or a null pointer if memory
   allocation fails. */
uint32_t *
supdir_create (uint32_t vaddr) 
{
  return (uint32_t *) get_user_page ((uint8_t *)vaddr);
}

/* Useless at this point. */
void
supdir_destroy (uint64_t *pd) 
{
  if (pd == NULL)
    return;
  palloc_free_page (pd);
}

uint32_t *
pde_get_pt_sup (uint32_t pde) {
  return ptov (pde & PTE_ADDR);
}

/* Returns the address of the page table entry for virtual
   address VADDR in page directory PD.
   If PD does not have a page table for VADDR, behavior depends
   on CREATE.  If CREATE is true, then a new page table is
   created and a pointer into it is returned.  Otherwise, a null
   pointer is returned. */
static uint64_t *
lookup_sup_page (uint64_t *pd, const void *vaddr, bool create)
{
  uint64_t *pt, *pde;

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
      
          *pde = pde_create (pt);
        }
      else
        return NULL;
    }

  /* Return the page table entry. */
  pt = pde_get_pt_sup (*pde);
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
supdir_set_page (uint32_t *pd, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location)
{
  uint64_t *spte;
  uint64_t large_location;
  ASSERT (pg_ofs (vaddr) == 0);
  ASSERT (is_user_vaddr (vaddr));

  spte = lookup_sup_page (pd, vaddr, true);

  if (spte != NULL) 
    {
    	uint64_t large_read_bytes = read_bytes;
      large_location = (uint64_t) 0 | location;
      *spte = (large_read_bytes << 34) | (large_location << 32) | sector;
      return true;
    }
  else
    return false;
}

/* Looks up and returns the Sector number that corresponds to user virtual
   address VADDR in PD.   Returns a null pointer if
   VADDR is unmapped. */
uint64_t
supdir_get_sector (uint32_t *pd, const void *vaddr) 
{
  uint64_t *spte;

  ASSERT (is_user_vaddr (vaddr));
  
  spte = lookup_sup_page (pd, vaddr, false);
  if (spte != NULL)
    return (uint64_t) *spte; //Make sure this gets the lowest 32 bits!!!!!!!!!!!
  else
    return NULL;
}

/* Marks user virtual page UPAGE "not present" in page
   directory PD.  Later accesses to the page will fault.  Other
   bits in the page table entry are preserved.
   UPAGE need not be mapped. */
void
supdir_clear_page (uint32_t *pd, void *upage) 
{
  uint32_t *spte;

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
  uint64_t spte = lookup_sup_page (thread_current ()->suptable, vpage, false);
  if (spte != NULL)
    {
      uint8_t location = (spte >> 32) & 0x3;
      uint32_t read_bytes = spte >> 34;
      uint32_t zero_bytes = PGSIZE - read_bytes;
      ASSERT (read_bytes <= PGSIZE);
      uint32_t sector = (uint32_t) spte;
      struct block *swap_device = block_get_role (BLOCK_SWAP);
      
      if (location == FILE_SYS || location == SWAP_SYS)
        {
          struct block *block_device = (location == FILE_SYS) ? fs_device : swap_device;
          while (read_bytes > 0)
            {
              block_read (block_device, sector, frame);
              sector++;
              frame += BLOCK_SECTOR_SIZE;
            }
        }
      if (zero_bytes > 0)
        memset (frame, 0, zero_bytes);
      return true;
    }
  else
    return false;
}

