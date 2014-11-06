#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>

#define FILE_SYS 3;
#define SWAP_SYS 2;
#define ZERO_SYS 1;

void sup_page_init (void);
bool update_sup_table (uint8_t location, uint32_t old_address, uint32_t new_address);
bool insert_sup_table (uint8_t location, uint32_t address);
bool sup_page_free (void);

#endif /* vm/page.h */