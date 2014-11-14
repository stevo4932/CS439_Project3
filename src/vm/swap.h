#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include <bitmap.h>
#include <stddef.h>

bool swap_init (void);
void swap_destroy (void);
size_t swap_write (void *frame_addr);
void swap_read (size_t sector, void *frame_addr);
void swap_remove (size_t sector);

#endif /* vm/swap.h */
