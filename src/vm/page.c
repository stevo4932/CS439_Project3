#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
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
struct hash *
supdir_create (void) 
{
  //return (uint32_t *) palloc_get_page (0);
  struct hash *hash_table = malloc (sizeof (struct hash));
  hash_init (hash_table, (hash_hash_func *) hash, (hash_less_func *) less_than, NULL);
  return hash_table;
}

void
supdir_destroy (struct hash *table) 
{
  if (table == NULL)
    return;
  struct hash_iterator iterator;
  hash_first (&iterator, table);
  struct hash_elem *e = hash_next (&iterator);
  while (e != NULL)
    {
      struct spte *entry = hash_entry (e, struct spte, elem);
      if (entry->location == MEM_SYS)
        free_frame ((void *) entry->vaddr); // placeholder
      else if (entry->location == SWAP_SYS)
        swap_remove (entry->sector);
      e = hash_next (&iterator);
    }
  hash_destroy (table, NULL);
  free (table);
}

/* Returns the address of the page table entry for virtual
   address VADDR in page directory PD.
   If PD does not have a page table for VADDR, behavior depends
   on CREATE.  If CREATE is true, then a new page table is
   created and a pointer into it is returned.  Otherwise, a null
   pointer is returned. */
struct spte *
lookup_sup_page (struct hash *table, const void *vaddr)
{
  struct spte entry;
  struct hash_elem *e;

  entry.vaddr = (uint32_t) vaddr;
  e = hash_find (table, &entry.elem);
  return (e != NULL) ? hash_entry (e, struct spte, elem) : NULL;
  /*
  uint64_t *pt;
  uint32_t *pde;

  ASSERT (pd != NULL);

  /* Shouldn't create new kernel virtual mappings. */
  //ASSERT (!create || is_user_vaddr (vaddr));

  /* Check for a page table for VADDR.
     If one is missing, create one if requested. */
  /*
  pde = pd + pd_no (vaddr);
  if (*pde == 0) 
    {
      if (create)
        {
          pt = (uint64_t *) palloc_get_page (PAL_ZERO | PAL_ASSERT);
          
          if (pt == NULL) 
          {
            printf ("Could not palloc a supplemental page table.\n");
            return NULL; 
          }
            
          
          *pde = (uint32_t) pt;
          
        }
      else
        {
          printf("Did not find anything, sorry!\n");
          return NULL;
        }
        
    }

  /* Return the page table entry. */
    /*
  pt = (uint64_t *) *pde;
  printf ("Lookup_sup_page found entry %llx\n", pt[pt_no (vaddr)]);

  return &pt[pt_no (vaddr)]; */
  //pt = (uint64_t *) (*pde + (8 * pt_no (vaddr)));
  
  //printf ("Lookup_sup_page found entry %llx\n", *pt);
  //return pt;
}

bool
supdir_set_page (struct hash *table, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location, bool writable)
{
  struct spte *entry = lookup_sup_page (table, vaddr);
  if (entry == NULL)
    {
      entry = malloc (sizeof (struct spte));
      if (entry == NULL)
        return false;
    }
  entry->vaddr = (uint32_t) vaddr;
  entry->sector = sector;
  entry->read_bytes = read_bytes;
  entry->location = location;
  entry->writable = writable;
  hash_replace (table, &entry->elem);
  return true;
  /*
  uint64_t *spte;
  ASSERT (pg_ofs (vaddr) == 0);
  ASSERT (is_user_vaddr (vaddr));

  spte = lookup_sup_page (pd, (const void *) vaddr, true);
  //printf ("Adding to suptable, virtual address %p, which is %s\n", vaddr, writable ? "writable" : "read-only");

  if (spte != NULL) 
    {
    	uint64_t large_read_bytes = read_bytes;
      uint64_t large_location = (uint64_t) 0 | location;
      uint64_t large_writable = (uint64_t) 0 | writable;
      *spte = (large_writable << WRITABLE_SHIFT) | (large_read_bytes << READ_BYTES_SHIFT) | (large_location << LOC_SHIFT) | sector;
      //printf ("ENTRY %x, virtual address %p, sector %d, %s\n", *spte, vaddr, sector, writable ? "writable" : "read-only");
      //printf ("SET PAGE %llx for vaddr %p\n", *spte, vaddr);
      ASSERT (writable == writable_from_spte (*spte));
      //printf ("supdir_set_page created entry %llx for page %p\n", *spte, vaddr);
      printf ("After setting page, for vaddr %p, looking up the page yields entry %llx\n", vaddr, *lookup_sup_page (pd, vaddr, false));
      return true;
    }
  else
    return false;*/
}

bool
supdir_set_swap (struct hash *supdir, void *vaddr, block_sector_t swap_sector)
{
  struct spte *entry = lookup_sup_page (supdir, vaddr);
  if (entry != NULL)
    {
      entry->location = SWAP_SYS;
      entry->sector = swap_sector;
      entry->read_bytes = PGSIZE;
      return true;
    }
  return false;
}

/* Marks user virtual page UPAGE "not present" in page
   directory PD.  Later accesses to the page will fault.  Other
   bits in the page table entry are preserved.
   UPAGE need not be mapped. */
void
supdir_clear_page (struct hash *table, void *upage) 
{
  struct spte *spte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  spte = lookup_sup_page (table, upage);
  if (spte != NULL)
    {
      hash_delete (table, &spte->elem);
      free (spte);
    }
}

bool
load_page (void *vpage, void *frame)
{
  //printf ("let's load a page!\n");
  struct spte *entry = lookup_sup_page (thread_current ()->supdir, (const void *) vpage);
  if (entry != NULL && entry->location != MEM_SYS)
    {
      uint8_t location = entry->location;
      int32_t read_bytes = entry->read_bytes;
      uint32_t zero_bytes = PGSIZE - read_bytes;
      block_sector_t sector = entry->sector;
      //printf ("About to attempt to read %d bytes from %s, starting at sector %d\n", read_bytes, location == FILE_SYS ? "file system " : "swap ", sector);
      //printf ("LOAD PAGE %llx for vaddr %p\n", entry, vpage);
      //printf ("Entry %a: Mapped %s virtual page %p to physical frame %p\n", entry, writable ? "writable" : "read-only", vpage, frame);
      void *frame_ = frame;
      if (location == FILE_SYS)
        {
          while (read_bytes > 0)
            {
              block_read (fs_device, sector, frame_);
              sector++;
              if (read_bytes >= BLOCK_SECTOR_SIZE)
                frame_ += BLOCK_SECTOR_SIZE;
              else
                frame_ += read_bytes;
              read_bytes -= BLOCK_SECTOR_SIZE;
            }
        }
      else if (location == SWAP_SYS)
        {
          //printf ("Reading page %p in from swap device for process %s\n", vpage, thread_current ()->name);
          swap_read (sector, frame);
        }
      if (zero_bytes > 0)
        memset (frame_, 0, zero_bytes);
      bool writable = entry->writable;
      //printf ("LOAD %s PAGE for vaddr %p for thread %s\n", writable ? "writable" : "read-only", vpage, thread_current ()->name);
      entry->location = MEM_SYS;
      pagedir_set_page (thread_current ()->pagedir, (void *) vpage, (void *) frame, writable);
      return true;
    }
  else
    return false;
}

/* Returns a hash value for entry e. */
unsigned
hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spte *entry = hash_entry (e, struct spte, elem);
  return hash_bytes (&entry->vaddr, sizeof entry->vaddr);
}

/* Returns true if page a precedes page b. */
bool
less_than (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED)
{
  const struct spte *entry_a = hash_entry (elem_a, struct spte, elem);
  const struct spte *entry_b = hash_entry (elem_b, struct spte, elem);

  return entry_a->vaddr < entry_b->vaddr;
}

