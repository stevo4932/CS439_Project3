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

void 
frame_table_init ()
{
	ft = malloc (sizeof (struct hash));
	hash_init (ft, (hash_hash_func *) frame_hash, (hash_less_func *) frame_less_than, NULL);
	ft_sema = malloc (sizeof (struct semaphore));
	sema_init (ft_sema, 1);
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
	// if (vaddr == (uint8_t *) -1)
	// 	{
	// 		printf("Setting entry's vaddr to %p in get_user_page for tid: %d\n", page, thread_current ()->tid);
	// 		entry->vaddr = (uint32_t) page;
	// 	}
	// else
	entry->vaddr = (uint32_t) vaddr;
	entry->thread = thread_current ();
	//printf("(get_user_page) Adding thread: %d and it's address: %p\n", entry->thread->tid, vaddr);
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
	//printf ("Eviction time!\n");
	sema_down (ft_sema);
	struct hash_iterator iterator;
	hash_first (&iterator, ft);
	struct hash_elem *e = hash_next (&iterator);
	struct ft_entry *entry = hash_entry (e, struct ft_entry, elem);

	/* NOPE, need a legit algorithm */

	while (!sema_try_down (&entry->pin_sema))
		{
			struct hash_elem *e = hash_next (&iterator);
			entry = hash_entry (e, struct ft_entry, elem);
		}
	struct thread *victim = entry->thread;
	void *old_addr = (void *) entry->vaddr;
	void *frame_addr = pagedir_get_page (victim->pagedir, old_addr);
	//printf ("Thread %d evicting virtual page %p (in frame %p) from thread %d\n", thread_current ()->tid, frame_addr, old_addr, victim->tid);
	struct spte *spte = lookup_sup_page (victim->supdir, old_addr);
	if (pagedir_is_dirty (victim->pagedir, old_addr))
		{
			//printf ("Swapping out virtual page %p from victim %s\n", old_addr, victim->name);
			block_sector_t sector = swap_write (frame_addr);
			supdir_set_swap (victim->supdir, old_addr, sector);
		}
	else
		spte->in_mem = false;
	//printf ("Clearing entry in victim's (%s) page directory.\n", victim->name);
	pagedir_clear_page (victim->pagedir, old_addr);
	hash_delete (ft, e);
	entry->thread = thread_current ();
	entry->vaddr = (uint32_t) new_addr;
	hash_replace (ft, e);
	sema_up (ft_sema);
	//printf ("Done evicting virtual page %p from thread %d\n", old_addr, victim->tid);
	return frame_addr;
}

void
free_frame (void *vaddr)
{
	struct thread *t = thread_current ();
	struct ft_entry *entry = malloc (sizeof (struct ft_entry));
	struct hash_elem *del_elem;
	struct ft_entry *remove_entry;
	//find entry in frame table.
	entry->vaddr = (uint32_t) vaddr;
	entry->thread = t;
	sema_down (ft_sema);
	del_elem =	hash_delete (ft, &entry->elem);
	remove_entry = hash_entry (del_elem, struct ft_entry, elem);
	sema_up (ft_sema);
	//reclaim entrys.
	free (remove_entry);
	free (entry);
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
	//printf("thread %d is pinning to %s now!\n", t->tid, set ? "true" : "false");
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
	else
		printf ("Yah pin_entry is not correct for thread: %d and vaddr: %p\n", t->tid, vaddr);
	//printf("Did I get here?\n");
	//printf("That's it I'v had it. I'm done! Bye!\n");
	
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
