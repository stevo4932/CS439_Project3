#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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
	/* Get enough pages from palloc to be able to store <?> entries in the frame table. */
	ft = (uint64_t *) palloc_get_multiple (PAL_ZERO, 16);
	ft_index = 0;
	evict_index = 0;
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
	uint64_t bigv;
	/*store the page address into frame table*/
	if ((int32_t) vaddr == -1)
		bigv = (uint64_t) 0 | (int) page;
	else
		bigv = (uint64_t) 0 | (int) vaddr;
	ft[ft_index++] = (bigv << 32) | (int) thread_current ();
	return page;
}

/* evict a page using FIFO */
void *
evict_page (uint8_t *new_addr)
{
	uint32_t old_addr = (ft[evict_index] >> 32) ;
	//struct thread *t = (struct thread *) (ft[evict_index] & (uint32_t)(-1));
	struct thread *t = (struct thread *) (uint32_t) ft[evict_index];
	uint32_t frame_addr = (uint32_t) pagedir_get_page (t->pagedir, (const void *) old_addr);
	uint64_t big_addr = (uint64_t) 0 | (int) new_addr;
	pagedir_clear_page (t->pagedir, (void *) old_addr);
	ft[evict_index++] = (big_addr << 32) | (int) thread_current ();
	return (void *) frame_addr;
}

void
frame_table_free ()
{
	palloc_free_multiple (ft, 16);
}

/*
bool
free_user_page (uint8_t *vaddr)
{
	int i;
	for (i = 0; i < ft_index; i++)
	  {
			
		}
}
*/
