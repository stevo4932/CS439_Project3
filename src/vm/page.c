#include "vm/page.h"
#include <stdbool.h>
#include <stddef.h>
#include "vm/page.h"
#include <string.h>
#include "threads/palloc.h"
#include "devices/block.h"
#include "threads/pte.h"

static uint32_t *active_pd (void);
static void invalidate_pagedir (uint32_t *);

/* Creates a new page directory that has mappings for kernel
   virtual addresses, but none for user virtual addresses.
   Returns the new page directory, or a null pointer if memory
   allocation fails. */
uint32_t *
supdir_create (uint32_t vaddr) 
{
  uint64_t *pd = (uint64_t) get_user_page (vaddr);
  if (pd != NULL)
    memcpy (pd, init_page_dir, PGSIZE);
  return pd;
}

/* Destroys page directory PD, freeing all the pages it
   references. */
void
supdir_destroy (uint64_t *pd) 
{
  uint64_t *pde;

  if (pd == NULL)
    return;

  ASSERT (pd != init_page_dir);
  palloc_free_page (pd);
}

uint32_t *pde_get_pt_sup (uint32_t pde) {
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
          pt = get_user_page (vaddr);
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
supdir_set_page (uint32_t *pd, void *vaddr, block_sector_t sector, size_t read_bytes);
{
  uint64_t *pte;
  uint64_t large_location;
  ASSERT (pg_ofs (vaddr) == 0);
  ASSERT (is_user_vaddr (vaddr));
  ASSERT (pd != init_page_dir);

  pte = lookup_sup_page (pd, vaddr, true);

  if (read_bytes == PGSIZE)
  	large_location = FILE_SYS;
  else
  	large_location = ZERO_SYS;

  if (pte != NULL) 
    {
    	uint64_t large_read_bytes = read_bytes;
      *pte = (large_read_bytes << 34) | (large_location << 32) | sector ;
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
  uint64_t *pte;

  ASSERT (is_user_vaddr (vaddr));
  
  pte = lookup_sup_page (pd, vaddr, false);
  if (pte != NULL)
    return (uint64_t) *pte; //Make sure this gets the lowest 32 bits!!!!!!!!!!!
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
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  pte = lookup_sup_page (pd, upage, false);
  if (pte != NULL)
    {
      *pte = 0;
    }
}

bool
load_page (uint32_t *addr)
{
  uint64_t pte = lookup_sup_page (thread_current ()->suptable, addr, false);
  if(pte != NULL)
    {
      uint8_t location = (pte >> 32) & 0x3;
      uint32_t sector = (uint32_t) pte;
      
      /*like fill this in please! thanks*/
      if (location == ZERO_SYS)
        {

        }
      else if (location == FILE_SYS)
        {

        }
      else
        {

        }
    }
}

