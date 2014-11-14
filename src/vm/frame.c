#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h" 
#include "threads/thread.h"
#include "devices/block.h"

static struct hash *ft;
unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
bool frame_less_than (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED);

void 
frame_table_init ()
{
	ft = malloc (sizeof (struct hash));
	hash_init (ft, (hash_hash_func *) frame_hash, (hash_less_func *) frame_less_than, NULL);
}

/* Obtains an unused frame for the user.
  If one is free, add the address to the frame table
  and return kernel virtual address of frame to the user. */
void *
get_user_page (uint8_t *vaddr)
{
	void *page;
	/*get a page from user pool for caller to use*/
	if (!(page = palloc_get_page (PAL_USER | PAL_ZERO)))
		/*No page avalible. Return NULL FOR NOW. WILL NEED TO CHAGE !!!!!!!!!!!*/
		return page;
	/*store the page address into frame table*/
	struct ft_entry *entry = malloc (sizeof (struct ft_entry));
	entry->vaddr = (uint32_t) vaddr;
	entry->thread = thread_current ();
	hash_replace (ft, &entry->elem);
	return page;
}

/* evict a page using hash iterator */
void *
evict_page (uint8_t *new_addr)
{
	//printf ("Eviction time!\n");
	struct hash_iterator iterator;
	hash_first (&iterator, ft);
	struct hash_elem *e = hash_next (&iterator);
	struct ft_entry *entry = hash_entry (e, struct ft_entry, elem);
	struct thread *victim = entry->thread;
	void *old_addr = (void *) entry->vaddr;
	void *frame_addr = pagedir_get_page (victim->pagedir, old_addr);
	struct spte *spte = lookup_sup_page (victim->supdir, old_addr);
	if (pagedir_is_dirty (victim->pagedir, old_addr))
		{
			//printf ("Swapping out virtual page %p from victim %s\n", old_addr, victim->name);
			block_sector_t sector = swap_write (frame_addr);
			supdir_set_swap (victim->supdir, old_addr, sector);
		}
	/* If the page is read-only, it must live in the file system. Update location in supplemental page table. */
	else if (spte->writable == FALSE)
		{
			spte->location = FILE_SYS;
		}
	pagedir_clear_page (victim->pagedir, old_addr);
	hash_delete (ft, e);
	entry->thread = thread_current ();
	entry->vaddr = (uint32_t) new_addr;
	hash_replace (ft, e);
	return frame_addr;

	/*
	uint32_t old_addr = (ft[evict_index] >> 32);
	printf ("Old addr: %x\n", old_addr);
	//struct thread *t = (struct thread *) (ft[evict_index] & (uint32_t)(-1));
	struct thread *t = (struct thread *) (uint32_t) ft[evict_index];
	uint32_t frame_addr = (uint32_t) pagedir_get_page (t->pagedir, (const void *) old_addr);
	uint64_t big_addr = (uint64_t) 0 | (int) new_addr;
	pagedir_clear_page (t->pagedir, (void *) old_addr);
	ft[evict_index++] = (big_addr << 32) | (int) thread_current ();
	return (void *) frame_addr;
	*/
}

void
free_frame (void *vaddr)
{
	struct thread *t = thread_current ();
	struct ft_entry *entry = malloc (sizeof (struct ft_entry));
	struct hash_elem * del_elem;
	struct ft_entry *remove_entry;
	//find entry in frame table.
	entry->vaddr = (uint32_t) vaddr;
	entry->thread = t;
	del_elem =	hash_delete (ft, entry->elem);
	remove_entry = list_entry (del_elem, struct ft_entry, elem);
	//reclaim entrys.
	free (remove_entry);
	free (entry);
}

void
frame_table_destroy ()
{
	hash_destroy (ft, NULL);
  free (ft);
}

/* Returns a hash value for entry e. */
unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct ft_entry *entry = hash_entry (e, struct ft_entry, elem);
  uint64_t big_vaddr = ((uint64_t) 0 | entry->vaddr) << 32;
  uint64_t identifier = (((uint64_t) 0 | entry->vaddr) << 32) | (uint32_t) entry->thread;
  /* change hash to be vaddr and thread */
  //return hash_bytes (&entry->vaddr, sizeof entry->vaddr);
  return hash_bytes (&identifier, sizeof big_vaddr);
}

/* Returns true if page a precedes page b. */
bool
frame_less_than (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED)
{
  struct ft_entry *entry_a = hash_entry (elem_a, struct ft_entry, elem);
  struct ft_entry *entry_b = hash_entry (elem_b, struct ft_entry, elem);

  return (entry_a->vaddr < entry_b->vaddr) && ((uint32_t) entry_a->thread < (uint32_t) entry_b->thread);
}
