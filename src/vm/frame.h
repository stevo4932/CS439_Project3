#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <hash.h>

struct ft_entry
{
	struct hash_elem elem;
	struct thread *thread;
	uint32_t vaddr;
	bool pinned;
};

void frame_table_init (void);
void *get_user_page (uint8_t *vaddr);
void frame_table_destroy (void);
void *evict_page (uint8_t *new_addr);
void free_frame (void *vaddr);
void set_pinned (void *vaddr, bool set);

#endif /* vm/frame.h */
