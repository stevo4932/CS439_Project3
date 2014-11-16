#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"
#include <hash.h>

#define FILE_SYS 3
#define SWAP_SYS 2
#define MEM_SYS	 1
#define ZERO_SYS 0

struct spte
{
	bool writable;
	uint8_t location;
	block_sector_t sector;
	uint32_t read_bytes;
	uint32_t vaddr;
	struct hash_elem elem;
};

struct hash *supdir_create (void);
void supdir_destroy (struct hash *table);
bool sup_page_free (void);
struct spte *lookup_sup_page (struct hash *table, const void *vaddr);
bool supdir_set_page (struct hash *table, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location, bool writable);
block_sector_t supdir_get_sector (struct hash *table, const void *vaddr);
void supdir_clear_page (struct hash *table, void *upage);
bool load_page (void *vpage, void *frame);
bool load_stack_pg (void *vpage, void *frame);

uint8_t location_from_spte (uint64_t entry);
uint32_t read_bytes_from_spte (uint64_t entry);
uint32_t sector_from_spte (uint64_t entry);
bool writable_from_spte (uint64_t entry);
bool supdir_set_swap (struct hash *supdir, void *vaddr, block_sector_t swap_sector);

unsigned hash (const struct hash_elem *elem, void *aux);
bool less_than (const struct hash_elem *a, const struct hash_elem *b, void *aux);

#endif /* vm/page.h */
