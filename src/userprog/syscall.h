#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"

struct semaphore *file_sema; /* Global lock for the file system. */

void syscall_init (void);
void close_all_files (void);
void self_destruct (int);

#endif /* userprog/syscall.h */
