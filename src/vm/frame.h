#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdint.h>

void frametable_init (void);
void *get_user_page (uint8_t *vaddr);

#endif
