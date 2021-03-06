#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <string.h>

struct bitmap *swap_table;
struct block *swap_device;
struct semaphore *swap_sema;

bool
swap_init ()
{
	/* Scott was driving */
	swap_device = block_get_role (BLOCK_SWAP);
	if ((swap_table = bitmap_create ((size_t) block_size (swap_device))))
		{
			swap_sema = malloc (sizeof (struct semaphore));
			sema_init (swap_sema, 1);
			return true;
		}
	return false; 
}

void
swap_destroy ()
{
	sema_down (swap_sema);
	bitmap_destroy (swap_table);
	sema_up (swap_sema);
}

size_t
swap_write (void *frame_addr)
{
	int write_sector;
	sema_down (swap_sema);
	size_t index = bitmap_scan_and_flip (swap_table, 0, 8, false);
	sema_up (swap_sema);
	size_t ret = index;
	if (index != BITMAP_ERROR)
		{
			for (write_sector = 0; write_sector < 8; write_sector++)
				{
					block_write (swap_device, (block_sector_t) index, frame_addr);
				  index++;
				  frame_addr += BLOCK_SECTOR_SIZE; 
				}
		}
	return ret;
}

void
swap_read (size_t sector, void *frame_addr)
{
	/* Heather was driving */
	int write_sector;
	sema_down (swap_sema);
	bitmap_mark (swap_table, sector);
	sema_up (swap_sema);
	for (write_sector = 0; write_sector < 8; write_sector++)
		{
			block_read (swap_device, (block_sector_t) sector, frame_addr);
		  sector++;
		  frame_addr += BLOCK_SECTOR_SIZE; 
		}
}

/* Used on process termination */
void
swap_remove (size_t sector)
{
	sema_down (swap_sema);
	bitmap_reset (swap_table, sector);
	sema_up (swap_sema);
}
