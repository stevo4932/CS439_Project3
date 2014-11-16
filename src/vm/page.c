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
      if (entry->in_mem)
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
  entry->in_mem = false;
  hash_replace (table, &entry->elem);
  return true;
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
      entry->in_mem = false;
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
  //printf ("Loading virtual page %p into frame: %p for thread %d\n", vpage, frame, thread_current ()->tid);
  struct spte *entry = lookup_sup_page (thread_current ()->supdir, (const void *) vpage);
  if (entry->in_mem)
    printf ("Uh oh, trying to read in a page that's already in memory.\n");
  if (entry != NULL && !entry->in_mem)
    {
      uint8_t location = entry->location;
      int32_t read_bytes = entry->read_bytes;
      uint32_t zero_bytes = PGSIZE - read_bytes;
      block_sector_t sector = entry->sector;
      //printf ("LOAD PAGE %llx for vaddr %p\n", entry, vpage);
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
      entry->in_mem = true;
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

