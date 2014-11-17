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

struct hash *
supdir_create (void) 
{
  /* Stephanie was Driving */
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
      uint32_t *pte = lookup_page (thread_current ()->pagedir, (const void *) entry->vaddr, false);
      if ((pte != NULL) && !(*pte & PTE_P) && (entry->location == SWAP_SYS))
        swap_remove (entry->sector);
      e = hash_next (&iterator);
    }
  hash_destroy (table, NULL);
  free (table);
}

struct spte *
lookup_sup_page (struct hash *table, const void *vaddr)
{
  /* Edwin was driving */
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
      return true;
    }
  return false;
}

void
supdir_clear_page (struct hash *table, void *upage) 
{
  /* Heather was driving */
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
  struct spte *entry = lookup_sup_page (thread_current ()->supdir, (const void *) vpage);
  if (entry != NULL)
    {
      uint8_t location = entry->location;
      int32_t read_bytes = entry->read_bytes;
      uint32_t zero_bytes = PGSIZE - read_bytes;
      block_sector_t sector = entry->sector;
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
          swap_read (sector, frame);
        }
      if (zero_bytes > 0)
        memset (frame_, 0, zero_bytes);
      bool writable = entry->writable;
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

