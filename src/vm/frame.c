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



void 
frame_table_init ()
{
	ft = (uint64_t *) palloc_get_page (PAL_USER | PAL_ZERO);
	ft_index = 0;
}


/*Obtains an unused fram for the user.
  If one is free, add the address to the frame table
  and return it to the user.*/
void *
get_user_page (uint8_t *vaddr)
{
	void *page;
	/*get a page from user pool for caller to use*/
	if (!(page = palloc_get_page (PAL_USER | PAL_ZERO)))
		/*No page avalible. Return NULL FOR NOW. WILL NEED TO CHAGE !!!!!!!!!!!*/
		return page;
	/*store the page address into frame table*/
	uint64_t bigv = (uint64_t) 0 | (int) vaddr;
	ft[ft_index++] = (bigv << 32) | (int) thread_current ();
	return page;
}

void
frame_table_free ()
{
	palloc_free_page (ft);
}

