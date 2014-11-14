#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include <string.h>
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);
static bool is_pt_valid (const void *pt, struct intr_frame *f, bool is_stack_ref);
static bool process_args (int *esp, int argc, int ptr_pos, struct intr_frame *f);
static void sys_halt (void);
static void sys_exit (int status, struct intr_frame *f);
static void sys_open (const char *file, struct intr_frame *f);
static void sys_wait (pid_t pid, struct intr_frame *f);
static void sys_exec (const char *cmdline, struct intr_frame *f);
static void sys_write (int fd, const void *buffer, unsigned size, struct intr_frame *f);
static void sys_filesize (int fd, struct intr_frame *f);
static void sys_close (int fd);
static void sys_create (const char *file_name, unsigned initial_size, struct intr_frame *f);
static void sys_remove (const char *file_name, struct intr_frame *f);
static void sys_read (int fd, void *buffer, unsigned size, struct intr_frame *f);
static void sys_seek (int fd, unsigned position);
static void sys_tell (int fd, struct intr_frame *f);

/* These defined constants are used in process_args to indicate position of pointer in argument list. */
#define NO_PT 0
#define FIRST_PT 1
#define SECOND_PT 2

void
syscall_init (void) 
{
  sema_init (&file_sema, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Scott is driving. */
static void
syscall_handler (struct intr_frame *f) 
{
  if (is_pt_valid (f->esp, f, false))
    {
      int *esp_int = (int *)f->esp;
      int sys_call_num = *esp_int;
      esp_int++;
      switch(sys_call_num)
      {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
        case SYS_HALT:
          sys_halt ();
          break;
        case SYS_EXIT:
          if (process_args (esp_int, 1, NO_PT, f))
            sys_exit (esp_int[0], f);
          break;
        case SYS_EXEC:
          if (process_args (esp_int, 1, FIRST_PT, f))
            sys_exec ((const char *)esp_int[0], f);
          break;
        case SYS_OPEN:
          if (process_args (esp_int, 1, FIRST_PT, f))
            sys_open ((const char *)esp_int[0], f);
          break;
        case SYS_WAIT:
          if (process_args (esp_int, 1, NO_PT, f))
            sys_wait (esp_int[0], f);
          break;
        case SYS_WRITE:
          if (process_args (esp_int, 3, SECOND_PT, f))
            sys_write (esp_int[0], (const void *)esp_int[1], esp_int[2], f);
          break;
        case SYS_FILESIZE:
          if (process_args (esp_int, 1, NO_PT, f))
            sys_filesize (esp_int[0], f);
          break;
        case SYS_CLOSE:
          if (process_args (esp_int, 1, NO_PT, f))
            sys_close (esp_int[0]);
          break;
        case SYS_CREATE:
          if (process_args (esp_int, 2, FIRST_PT, f))
            sys_create ((const char *)esp_int[0], (unsigned)esp_int[1], f);
          break;
        case SYS_REMOVE:
          if (process_args (esp_int, 1, FIRST_PT, f))
            sys_remove ((const char *)esp_int[0], f);
          break;
        case SYS_READ:
          if (process_args (esp_int, 3, SECOND_PT, f))
            sys_read (esp_int[0], (void *)esp_int[1], (unsigned)esp_int[2], f);
          break;
        case SYS_SEEK:
          if (process_args (esp_int, 2, NO_PT, f))
            sys_seek (esp_int[0], (unsigned)esp_int[1]);
          break;
        case SYS_TELL:
          if (process_args (esp_int, 1, NO_PT, f))
            sys_tell (esp_int[0], f);
          break;
        default:
          sys_exit (-1, f);
          break;
      }
    }
}

/* Helper function to validate arguments for the system calls before actually
   passing arguments to the system call helper functions. argc is the number of
   parameters expected for the system call. ptr_pos represents the position of
   an argument that is a pointer (NO_PT means there is no pointer, FIRST_PT means
   the first argument is a pointer, and SECOND_PT means the second argument is
   a pointer, which covers all of the possibilities).
   If any of the arguments do not validate, the process will be terminated before
   any resources are obtained for the system call. */
static bool
process_args (int *esp, int argc, int ptr_pos, struct intr_frame *f)
{
  /* Stephanie driving now. */
  int i;
  for (i = 0; i < argc; i++)
    {
      if (!is_pt_valid ((esp + i), f, false) || (ptr_pos == (i + 1) && !is_pt_valid ((void *)esp[i], f, true)))
        return false;
    }
  return true;
}

/* Helper function to check whether the given pointer is valid. To be considered valid,
   the pointer must not be null, it must be in user space, and it must be in a page that
   is mapped to in the process's page directory. If any of these conditions do not hold,
   the process is terminated with an exit status of -1. */
static bool
is_pt_valid (const void *pt, struct intr_frame *f, bool allow_stack_growth)
{
  /* Scott is driving now. */
  if (pt == NULL || !is_user_vaddr (pt))
    {
      sys_exit (-1, f);
      return false;
    }
  else if (pagedir_get_page (thread_current ()->pagedir, pt) == NULL)
    {
      //printf ("In is_pt_valid, address was unmapped, attempting to page in.\n");
      bool is_stack_ref = allow_stack_growth ? (pt < PHYS_BASE && pt >= (f->esp - 32)) : false;
      //if (is_stack_ref) printf ("Attempting to grow stack from inside system call.\n");
      page_in ((void *)pt, f, is_stack_ref);
    }
  return true;
}

/* Helper function that is used in syscall.c, process.c, and exception.c to close all
   of the current process's open files and then free the page that contained the file
   pointers. */
void
close_all_files ()
{
  /* Heather is driving now. */
  struct file **files = thread_current ()->files;
  int i; 
  for (i = 2; i < 1024; i++)
    {
      if (files[i] != NULL)
        {
          sys_close (i);
        }
    }
  palloc_free_page (files);
}

/* Helper function for the "exec" system call.
   The command line is passed in as cmdline, which is sent to process_execute
   to be parsed and executed. If a new thread is successfully created,
   the parent waits for the child to load. If it does not load successfully,
   or if the thread could not be created, this function returns -1 in %eax.
   Otherwise, it returns the pid of the new process. */
static void
sys_exec (const char *cmdline, struct intr_frame *f)
{
  /* Edwin is driving now. */
  pid_t pid;
  if ((pid = process_execute (cmdline)) == TID_ERROR)
    {
      f->eax = -1;
    }
  else
    {
      struct thread *parent = thread_current ();
      sema_down (&parent->load_sema);
      /* Find out whether child successfully loaded and store appropriate value in EAX. */
      f->eax = parent->success ? pid : -1;
      /* If unsuccessful, remove child from current thread's list of children. */
      if (!parent->success)
        {
          sema_down (&parent->child_list_sema);
          struct list_elem *e = childelem_from_tid (pid, &parent->children);
          ASSERT (e != NULL);
          list_remove (e);
          sema_up (&parent->child_list_sema);
        }
    }
}

/* Terminates Pintos. */
static void
sys_halt (void)
{
  /* Heather is driving. */
	shutdown_power_off ();
}

/* Waits for a child whose pid is pid. This will return -1 if this process
   has no child with the given pid (which is the case if it already waited on
   the child or the child never existed). If the child already terminated but
   the current process has not previously waited on the child, this function
   will return "immediately" with the child's exit status. Otherwise, the
   parent process will be blocked until the child terminates and will then
   return the child's exit status. */
static void
sys_wait (pid_t pid, struct intr_frame *f)
{
  /* Heather is driving. */
   f->eax = process_wait ((tid_t) pid);
}

/* Helper function to terminate the current thread with exit status STATUS.
   Before calling thread_exit to actually terminate the thread, this function
   iterates through the thread's children list and sets its children's parents
   to NULL to orphan them. */
void
self_destruct (int status)
{
  /* Heather is driving. */
  struct thread *t = thread_current ();
  struct list_elem *e;
  t->return_status = status;
  if (!list_empty (&t->children))
    {
      for (e = list_begin (&(t->children)); e != list_end (&(t->children));
           e = list_next (e))
        {
          struct thread *child = list_entry (e, struct thread, childelem);
          child->parent = NULL;
        }
    }
  supdir_destroy (t->supdir);
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

/* Terminates the current thread with exit status STATUS, and returns the status. */
static void
sys_exit (int status, struct intr_frame *f)
{
  /* Heather is driving. */
  f->eax = status;
  self_destruct (status);
}

/* Attempts to open the file given by file_name. If unsuccessful, this function will
   return -1. Otherwise, it will return, as a file descriptor, the first available index
   in the current process's file list. */
static void 
sys_open (const char *file_name, struct intr_frame *f)
{
  //printf ("Opening %s\n", file_name);
  /* Edwin is driving. */
  sema_down (&file_sema);
  f->eax = -1;
  struct thread *t = thread_current ();
  int i;
  struct file *file = filesys_open (file_name);
  if (file != NULL)
    { 
      for (i = 2; i < 1024; i++)
        {
          if (t->files[i] == NULL)
            {
              t->files[i] = file;
              f->eax = i;
              break;
            }
        }
    }
  sema_up (&file_sema);
  //printf ("Opened %s\n", file_name);
}

/* Attempts to write the contents of BUFFER into the file denoted by the given file descriptor.
   If file descriptor 1 is given, this function uses putbuf to write the buffer to stdout, 300
   bytes at a time. This function returns -1 if there is an error in writing. */
static void 
sys_write (int fd, const void *buffer, unsigned size, struct intr_frame *f)
{
  /* Heather is driving. */
  struct file *file;
  struct thread *t = thread_current ();
  if (fd == 1)
    {
      unsigned pos;
      int bytes_left = size;
      char *char_buffer = (char *)buffer;
      /* Write the buffer to stdout, 300 bytes at a time. */
      for (pos = 0; pos < size; pos += 300)
        {
          char_buffer += pos;
          if (is_pt_valid (char_buffer, f, true))
            {
              if (bytes_left > 300)
                {
                  putbuf (buffer, 300);
                  bytes_left -= 300;
                }
              else
                {
                  putbuf (buffer, bytes_left);
                  bytes_left = 0;
                }
            }
        }
      f->eax = size;
    }
  else if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
  {
    sema_down (&file_sema);
    f->eax = (int)file_write (file, buffer, size);
    sema_up (&file_sema);
  }
  else
    f->eax = -1;
}

/* Returns the size of the file corresponding to the given file descriptor, or -1
   if there is no such open file. */
static void 
sys_filesize (int fd, struct intr_frame *f)
{
  /* Edwin is driving. */
  sema_down (&file_sema);
  f->eax = -1;
  struct thread *t = thread_current ();
  struct file *file;
  if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
    f->eax = file_length (file);
  sema_up (&file_sema);
}

/* Closes the file denoted by the given file descriptor, if such an open files exists. */
static void
sys_close (int fd)
{
  /* Heather is driving. */
  sema_down (&file_sema);
  struct thread *t = thread_current ();
  struct file *file;
  if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
  {
    file_close (file);
    t->files[fd] = NULL;
  }
  sema_up (&file_sema);
}

/* Creates a file called file_name that is of size initial_size. Returns true
   if successful, false otherwise. Does not open the file. */
static void
sys_create (const char *file_name, unsigned initial_size, struct intr_frame *f)
{
  /* Edwin is driving. */
  sema_down (&file_sema);
  f->eax = filesys_create (file_name, initial_size);
  sema_up (&file_sema);
}

/* Deletes the file called file_name. Returns true if successful, false otherwise.
   Does not close the file. */
static void
sys_remove (const char *file_name, struct intr_frame *f)
{
  /* Heather is driving. */
  sema_down (&file_sema);
  f->eax = filesys_remove (file_name);
  sema_up (&file_sema);
}

/* Attempts to read the contents of the file denoted by the given file descriptor into BUFFER.
   If file descriptor 0 is given, this function uses input_getc to read from the keyboard, one
   byte at a time. This function returns -1 if there is an error in reading. */
static void
sys_read (int fd, void *buffer, unsigned size, struct intr_frame *f)
{
  /* Heather is driving. */
  //printf ("Trying to read from fd %d\n", fd);
  struct file *file;
  struct thread *t = thread_current ();
  if (fd == 0)
    {
      uint8_t *byte_buffer = (uint8_t *)buffer;
      int bytes_read = 0;
      unsigned i;
      uint32_t *pd = t->pagedir;
      for (i = 0; i < size; i++)
        {
          if (is_pt_valid (byte_buffer, f, true))
            {
              bool writable = *(lookup_page (pd, byte_buffer, false)) & PTE_W;
              if (!writable)
                self_destruct (-1);
              *byte_buffer = input_getc ();
              byte_buffer++;
              bytes_read++;
            }
        }
      f->eax = bytes_read;
    }
  else if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
  {
    void *buffer_;
    uint32_t *pd = t->pagedir;
    for (buffer_ = (void *) ((uint32_t) buffer & 0xfffff000); (unsigned) buffer_ < (unsigned) buffer + size; buffer_ += PGSIZE)
    {
      //printf ("Validating buffer at %p\n", buffer_);
      if (!is_pt_valid (buffer_, f, true) || !(*(lookup_page (pd, buffer_, false)) & PTE_W))
        self_destruct (-1);
      //printf ("Buffer at %p is %s\n", buffer_, valid ? "valid." : "invalid.");
      //printf ("made it past is_pt_valid!\n");
    }
    sema_down (&file_sema);
    f->eax = (int)file_read (file, buffer, size);
    //printf ("not reached\n");
    sema_up (&file_sema);
  }
  else
    f->eax = -1;
}

/* Changes next byte to be read in file denoted by the given file descriptor to be POSITION,
    using the file function file_seek. Does not return a value. */
static void
sys_seek (int fd, unsigned position)
{
  /* Heather is driving. */
  sema_down (&file_sema);
  struct file *file;
  struct thread *t = thread_current ();
  if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
    file_seek (file, position);
  sema_up (&file_sema);
}

/* Uses file function file_tell to return the next byte to be read or written in the file
   denoted by the given file descriptor. Returns -1 if no such file is open. */
static void
sys_tell (int fd, struct intr_frame *f)
{
  /* Edwin is driving. */
  sema_down (&file_sema);
  f->eax = -1;
  struct thread *t = thread_current ();
  struct file *file;
  if (fd < 1024 && fd >= 2 && (file = t->files[fd]))
    f->eax = file_tell (file);
  sema_up (&file_sema);
}
