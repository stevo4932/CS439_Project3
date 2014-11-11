#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"

#define FILE_SYS 3
#define SWAP_SYS 2
#define ZERO_SYS 1
#define LOC_MASK 0x3
#define LOC_SHIFT 32
#define READ_BYTES_MASK 0x1FFF
#define READ_BYTES_SHIFT 34
#define WRITABLE_MASK 0x1
#define WRITABLE_SHIFT 47

uint32_t *supdir_create (uint32_t vaddr);
void supdir_destroy (uint32_t *pd);
bool sup_page_free (void);
uint64_t *lookup_sup_page (uint32_t *pd, const void *vaddr, bool create);
bool supdir_set_page (uint32_t *pd, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location, bool writable);
block_sector_t supdir_get_sector (uint32_t *pd, const void *vaddr);
void supdir_clear_page (uint32_t *pd, void *upage);
bool load_page (void *vpage, void *frame);
bool load_stack_pg (void *vpage, void *frame);

uint8_t location_from_spte (uint64_t entry);
uint32_t read_bytes_from_spte (uint64_t entry);
uint32_t sector_from_spte (uint64_t entry);
bool writable_from_spte (uint64_t entry);

#endif /* vm/page.h */
