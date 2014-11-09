#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/vaddr.h" 
#include "threads/thread.h"


static uint64_t *ft;
static int ft_index;
static int evict_index;

void 
frame_table_init ()
{
	ft = (uint64_t *) palloc_get_page (PAL_USER | PAL_ZERO);
	ft_index = 0;
	evict_index = 0;
}


/* Obtains an unused frame for the user.
  If one is free, add the address to the frame table
  and return it to the user. */
void *
get_user_page (uint8_t *vaddr)
{
	void *page;
	/*get a page from user pool for caller to use*/
	if (!(page = palloc_get_page (PAL_USER | PAL_ZERO)))
		/*No page avalible. Return NULL FOR NOW. WILL NEED TO CHAGE !!!!!!!!!!!*/
		return page;
	uint64_t bigv;
	/*store the page address into frame table*/
	if (vaddr == -1)
		bigv = (uint64_t) 0 | (int) page;
	else
		bigv = (uint64_t) 0 | (int) vaddr;
	ft[ft_index++] = (bigv << 32) | (int) thread_current ();
	return page;
}

/* evict a page using FIFO */
void *
evict_page (uint8_t *new_addr, bool write)
{
	uint32_t old_addr = (ft[evict_index] >> 32) ;
	struct thread *t = (struct thread *) ft[evict_index];
	uint32_t frame_addr = pagedir_get_page (t->pagedir, old_addr);
	uint64_t big_addr = (uint64_t) 0 | (int) new_addr;
	pagedir_clear_page (t->pagedir, old_addr);
	ft[evict_index++] = (big_addr << 32) | (int) thread_current ();
	if (pagedir_set_page (thread_current ()->pagedir, new_addr, frame_addr, write))
		return frame_addr;
	else
		return NULL;
}

void
frame_table_free ()
{
	palloc_free_page (ft);
}

