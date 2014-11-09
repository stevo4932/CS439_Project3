#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/block.h"

#define FILE_SYS 3
#define SWAP_SYS 2
#define ZERO_SYS 1

uint32_t *supdir_create (uint32_t vaddr);
void supdir_destroy (uint64_t *pd);
uint32_t *pde_get_pt_sup (uint32_t pde);
bool sup_page_free (void);
bool supdir_set_page (uint32_t *pd, void *vaddr, block_sector_t sector, size_t read_bytes, uint8_t location);
uint64_t supdir_get_sector (uint32_t *pd, const void *vaddr);
void supdir_clear_page (uint32_t *pd, void *upage);
bool load_page (void *vpage, void *frame);

#endif /* vm/page.h */
