#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdint.h>
#include <stdbool.h>

void frame_table_init (void);
void *get_user_page (uint8_t *vaddr);
void frame_table_free (void);
void *evict_page (uint8_t *new_addr);

#endif /* vm/frame.h */
