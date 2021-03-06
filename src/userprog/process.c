#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "devices/block.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static char *parse_push (const char *s, void **esp);
static bool install_page (void *upage, void *kpage, bool writable);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmdline) 
{
  char *cmdline_copy;
  char *file_name;
  char *save_ptr;
  tid_t tid;

  /* Make a copy of CMDLINE.
     Otherwise there's a race between the caller and load(). */
  cmdline_copy = palloc_get_page (0);
  if (cmdline_copy == NULL)
    return TID_ERROR;
  strlcpy (cmdline_copy, cmdline, PGSIZE);

  /* Make a copy of FILE_NAME. */
  file_name = palloc_get_page (0);
  if (file_name == NULL)
  {
    palloc_free_page (cmdline_copy);
    return TID_ERROR;
  }  
  strlcpy (file_name, cmdline, PGSIZE);
  file_name = strtok_r (file_name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, cmdline_copy);

  sema_down (file_sema);
  struct file *file = filesys_open (file_name);
  if(file != NULL)
    {
      /* Disable writing to this executable. */
      file_deny_write (file);
      struct list_elem *e = childelem_from_tid (tid, &thread_current ()->children);
      struct thread *child = list_entry (e, struct thread, childelem);
      int i;
      for (i = 2; i < 1024; i++)
        {
          if (child->files[i] == NULL)
            {
              child->files[i] = file;
              break;
            }
        }
    }
  sema_up (file_sema);

  /* Done with the copy of FILE_NAME. */
  palloc_free_page (file_name);
  
  if (tid == TID_ERROR)
    palloc_free_page (cmdline_copy);
    
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *t = thread_current ();
  /* Initialize interrupt frame and load executable. */

  memset (&if_, 0, sizeof if_);

  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  palloc_free_page (file_name);
  
  /* Edwin driving now. */
  t->parent->success = success;
  sema_up (&t->parent->load_sema);

  /* If load failed, quit. */
  if (!success)
  {
    sema_up (&t->child_sema);
    self_destruct (-1);
  }
  
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  int return_status = -1;
  if (!list_empty (&t->children))
    {
      for (e = list_begin (&t->children); e != list_end (&t->children);
           e = list_next (e))
        {
          struct thread *child = list_entry (e, struct thread, childelem);
          if (child->tid == child_tid)
            {
              sema_down (&child->child_sema);
              return_status = child->return_status;
              break; 
            }
        }
    }

    struct list_elem *child_elem = childelem_from_tid (child_tid, &thread_current ()->children);
    struct thread *child = NULL;
    if (child_elem != NULL)
      child = list_entry (child_elem, struct thread, childelem);
    if (child != NULL)
    {
      /* Remove child from our children list. */
      list_remove (child_elem);
      palloc_free_page (child);
    }
    
  return return_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{

  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  close_all_files ();

  if (!list_empty (&cur->children))
    {
      struct list_elem *e;
      for (e = list_begin (&(cur->children)); e != list_end (&(cur->children));
           e = list_next (e))
        {
          struct thread *child = list_entry (e, struct thread, childelem);
          ASSERT (child != NULL);
          if (sema_try_down (&child->child_sema))
             palloc_free_page (child);
        }
    }

  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* If current thread's parent is still alive, let the parent know we're exiting. */
  if (cur->parent != NULL)
    sema_up (&cur->child_sema);
  /* If the thread's parent has already terminated, free the current thread's struct. */
  else
    palloc_free_page (cur);
      

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (const char *cmdline, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);


/* Scott driving now. */
static char *
parse_push (const char *cmdline, void **esp)
{
  char *token, *save_ptr, *file_name, *s;
  char *my_esp = (char *) *esp;
  s = palloc_get_page (PAL_ZERO | PAL_ASSERT);
  strlcpy (s, cmdline, strnlen (cmdline, MAX_CMD_LEN) + 1);
  int len, argc = 0;

  for (token = strtok_r (s, " ", &save_ptr); token != NULL;
        token = strtok_r (NULL, " ", &save_ptr))
    { 
      len = strnlen (token, MAX_CMD_LEN);
      my_esp = my_esp - (len + 1);
      strlcpy (my_esp, token, len + 1);
      if (argc == 0)
        file_name = my_esp;
      argc++;
    }
  palloc_free_page (s);
  /* End of Scott driving. Stephanie driving now. */
  save_ptr = my_esp;
  /* Word-align. */
  my_esp -= (((unsigned int)my_esp % 4));
  /* Time to push the pointers. Create new esp int pointer. */
  int *esp_int = (int *)my_esp;
  /* Push null pointer. */
  esp_int--;
  save_ptr--;
  /* Walk back up the stack, and push the pointer each time is it pointing to the beginning of an argument. */
  while ((void *)save_ptr < PHYS_BASE - 1)
    {
      if (*save_ptr == '\0')
        {
          esp_int--;
          *esp_int = (int)++save_ptr;
        }
      save_ptr++;
    }
  save_ptr = (char *)esp_int;
  /* Push argv */ 
  esp_int--;
  *esp_int = (int)save_ptr;
  /* Push argc */
  esp_int--;
  *esp_int = argc;
  /* Push "return address." */
  esp_int--;
  *esp = esp_int;
  return file_name;
}

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *cmdline, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  int cmd_len;

  char *cmdline_copy = palloc_get_page (0);
  strlcpy (cmdline_copy, cmdline, strnlen (cmdline, MAX_CMD_LEN) + 1);
  char *save_ptr;
  char *file_name = strtok_r (cmdline_copy, " ", &save_ptr);

  /* Stephanie driving now. */
  cmd_len = strnlen (cmdline, MAX_CMD_LEN + 1);
  if (cmd_len == MAX_CMD_LEN + 1)
    {
      printf ("load: %s: too many characters in command line\n", cmdline);
      goto done;
    }

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  t->supdir = supdir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Edwin driving now. */
  sema_down (file_sema);

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (cmdline, esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 /* We arrive here whether the load is successful or not. */
 done:
  /* Edwin is driving */
  file_close (file);
  palloc_free_page (cmdline_copy);
  sema_up (file_sema); 
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  /* Stephanie is driving */
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = (read_bytes < PGSIZE) ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      if (page_read_bytes > 0)
        {
          uint8_t location = (page_read_bytes == 0) ? 1 : 3;
          struct inode *inode = file_get_inode (file);
          ASSERT (inode != NULL);
          uint32_t sector = byte_to_sector (inode, ofs);
          supdir_set_page (thread_current ()->supdir, upage, sector, page_read_bytes, location, writable);
        }
      else
        {
          supdir_set_page (thread_current ()->supdir, upage, 0, 0, ZERO_SYS, writable);
        }
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (const char *cmdline, void **esp) 
{
  bool success = false;
  struct hash *supdir = thread_current ()->supdir;

  uint8_t *stack_addr = ((uint8_t *) PHYS_BASE) - PGSIZE;
  if (supdir_set_page (supdir, stack_addr, 0, 0, ZERO_SYS, true))
    {
      void *frame = get_user_page (stack_addr);
      if (frame == NULL)
        frame = evict_page (stack_addr);
      ASSERT (frame != NULL);
      success = load_page (stack_addr, frame);
    }
  if (success)
    {
      *esp = PHYS_BASE;
      parse_push (cmdline, esp);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Stephanie driving now. */
struct list_elem *
childelem_from_tid (tid_t tid, struct list *list)
{
  struct list_elem *elem = NULL;
  if (!list_empty (list))
    {
      struct list_elem *e;
      for (e = list_begin (list); e != list_end (list); e = list_next (e))
        {
          struct thread *child = list_entry (e, struct thread, childelem);
          if (child->tid == tid)
            {
              elem = e;
              break;
            }
        }
    }
  return elem;
}
