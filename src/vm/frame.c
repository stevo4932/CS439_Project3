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
static struct semaphore *ft_sema;
static int *it_count;

void 
frame_table_init ()
{
	/* Scott was driving */
	ft = malloc (sizeof (struct hash));
	hash_init (ft, (hash_hash_func *) frame_hash, (hash_less_func *) frame_less_than, NULL);
	it_count = malloc (sizeof (int));
	*it_count = 1;
	ft_sema = malloc (sizeof (struct semaphore));
	sema_init (ft_sema, 1);
}

void *
get_user_page (uint8_t *vaddr)
{
	void *page;
	if (!(page = palloc_get_page (PAL_USER | PAL_ZERO)))
		return page;
	struct ft_entry *entry = malloc (sizeof (struct ft_entry));
	entry->vaddr = (uint32_t) vaddr;
	entry->thread = thread_current ();
	sema_init (&entry->pin_sema, 1);
	sema_down (ft_sema);
	hash_replace (ft, &entry->elem);
	sema_up (ft_sema);
	return page;
}

/* evict a page using TBD */
void *
evict_page (uint8_t *new_addr)
{
	/* Stephanie was driving */
	sema_down (ft_sema);
	struct hash_iterator iterator;
	hash_first (&iterator, ft);
	struct hash_elem *e = hash_next (&iterator);
	struct ft_entry *entry = hash_entry (e, struct ft_entry, elem);

	while (!sema_try_down (&entry->pin_sema))
		{
			struct hash_elem *e = hash_next (&iterator);
			entry = hash_entry (e, struct ft_entry, elem);
		}
	struct thread *victim = entry->thread;
	void *old_addr = (void *) entry->vaddr;
	void *frame_addr = pagedir_get_page (victim->pagedir, old_addr);
	struct spte *spte = lookup_sup_page (victim->supdir, old_addr);
	if (pagedir_is_dirty (victim->pagedir, old_addr) || spte->location == SWAP_SYS)
		{
			block_sector_t sector = swap_write (frame_addr);
			supdir_set_swap (victim->supdir, old_addr, sector);
		}
	pagedir_clear_page (victim->pagedir, old_addr);
	hash_delete (ft, e);
	entry->thread = thread_current ();
	entry->vaddr = (uint32_t) new_addr;
	hash_replace (ft, e);
	sema_up (ft_sema);
	return frame_addr;
}

void
free_frame (void *vaddr)
{
	struct thread *t = thread_current ();
	struct ft_entry entry;
	struct hash_elem *del_elem;
	struct ft_entry *remove_entry;
	//find entry in frame table.
	entry.vaddr = (uint32_t) vaddr;
	entry.thread = t;
	sema_down (ft_sema);
	del_elem =	hash_find (ft, &entry.elem);
	if (del_elem != NULL)
		{
			remove_entry = hash_entry (del_elem, struct ft_entry, elem);
			hash_delete (ft, del_elem);
			free (remove_entry);
		}
	sema_up (ft_sema);
}

void
frame_table_destroy ()
{
	sema_down (ft_sema);
	hash_destroy (ft, NULL);
  free (ft);
  sema_up (ft_sema);
}

void
set_pinned (void *vaddr, bool set)
{
	sema_down (ft_sema);
	struct thread *t = thread_current ();
	struct ft_entry entry;
	entry.vaddr = (uint32_t) vaddr;
	entry.thread = t;
	struct ft_entry *pin_entry = hash_entry (hash_find (ft, &entry.elem), struct ft_entry, elem);
	sema_up (ft_sema);
	if (pin_entry != NULL)
		{
			if (set)
				sema_down (&pin_entry->pin_sema);
			else
				{
					sema_try_down (&pin_entry->pin_sema);
					sema_up (&pin_entry->pin_sema);
				}
		}	
}

/* Returns a hash value for entry e. */
unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct ft_entry *entry = hash_entry (e, struct ft_entry, elem);
  uint64_t big_vaddr = ((uint64_t) 0 | entry->vaddr) << 32;
  uint64_t identifier = (((uint64_t) 0 | entry->vaddr) << 32) | (uint32_t) entry->thread;

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
