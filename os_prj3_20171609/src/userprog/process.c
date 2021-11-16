#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/stdio.h"
#include "lib/string.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

void func_input_command(const char* file_name, char* command);
void calculate_esp(const char* input, void** esp);

tid_t
process_execute(const char *file_name)
{
	char *fn_copy;
	tid_t tid;
	char command[256];

	func_input_command(file_name, command);

	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	if (!filesys_open(command))
		return -1;

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(command, PRI_DEFAULT, start_process, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}


static void
start_process (void *file_name_)
{
	char *file_name = file_name_;
	struct intr_frame if_;
	bool success;
	int qrts;

	memset(&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;
	
	success = load(file_name, &if_.eip, &if_.esp);
	qrts = qrts + 1;

	palloc_free_page(file_name);
	sema_up(&thread_current()->parent->load);
    if (!success) {
        thread_current()->loaded = 0;
        exit(-1);
    }
  else
    thread_current()->loaded = 1;

	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
	NOT_REACHED();
}


int
process_wait (tid_t child_tid) 
{
	struct thread *parent = thread_current();
	struct thread *tmp_thre;
	struct list_elem* e;

	int i = 0, exit_status;
	int switch_byun;

	thread_current()->waiting = 1;
	for (e = list_begin(&(thread_current()->child)); i < list_size(&(thread_current()->child)); e = list_next(e))
	{
		tmp_thre = list_entry(e, struct thread, child_elem);
		if (tmp_thre->tid == child_tid) 
		{
		 switch_byun = 1;
		  break;
		}
		i++;
	}

	  if (i >= list_size(&(thread_current()->child)) || tmp_thre->parent != thread_current() || tmp_thre->waiting)
		return -1;

	  if (switch_byun && tmp_thre){
		sema_down(&(tmp_thre->exit));
		exit_status = tmp_thre->exit_status;
		list_remove(&(tmp_thre->child_elem));
		sema_up(&(tmp_thre->wait));
		return exit_status;
	  }
	  else 
		return -1;
}

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  sema_up(&(cur->exit));
  sema_down(&cur->wait);

  pd = cur->pagedir;

  if (pd != NULL) 
    {

      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

}

void
process_activate (void)
{
  struct thread *t = thread_current ();
  pagedir_activate (t->pagedir);
  tss_update ();
}


typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

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

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

bool
load(const char* file_name, void (**eip) (void), void** esp)
{
    struct thread* t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file* file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;
    char* tmp[2];
    char prog_name[256];

    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL)
        goto done;
    process_activate();

	func_input_command(file_name, prog_name);

    /* Open executable file. */
    file = filesys_open(prog_name);
    if (file == NULL)
    {
        printf("load: %s: open failed\n", prog_name);
        goto done;
    }

    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr
        || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7)
        || ehdr.e_type != 2
        || ehdr.e_machine != 3
        || ehdr.e_version != 1
        || ehdr.e_phentsize != sizeof(struct Elf32_Phdr)
        || ehdr.e_phnum > 1024)
    {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++)
    {
        struct Elf32_Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
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
            if (validate_segment(&phdr, file))
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
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE)
                        - read_bytes);
                }
                else
                {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void*)mem_page,
                    read_bytes, zero_bytes, writable))
                    goto done;
            }
            else
                goto done;
            break;
        }
    }


    if (!setup_stack(esp))
        goto done;

	calculate_esp(file_name, esp);

    /* Start address. */
    *eip = (void (*) (void)) ehdr.e_entry;

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    file_close(file);
    return success;
}

void func_input_command(const char* file_name, char* command)
{
	char *command_in1, *command_out1;
	strlcpy(command, file_name, strlen(file_name) + 1);
	command_in1 = strtok_r(command, " ", &command_out1);
}

void calculate_esp(const char* file_name, void** esp)
{

	int argc = 0, length, total_lengh = 0;
	char command[256];
	char *cmd_in1, *cmd_out1;
	int jskim = 4;
	strlcpy(command, file_name, strlen(file_name) + 1);
	cmd_in1 = strtok_r(command, " ", &cmd_out1);

	while (cmd_in1 != NULL)
	{
		cmd_in1 = strtok_r(NULL, " ", &cmd_out1);
		argc = argc + 1;
	}

	char* argv[argc];
	strlcpy(command, file_name, strlen(file_name) + 1);
	cmd_in1 = strtok_r(command, " ", &cmd_out1);

	for (int i = 0; i<argc; i++)
	{
		if (!cmd_in1) break;
		argv[i] = cmd_in1;
		cmd_in1 = strtok_r(NULL, " ", &cmd_out1);
	}

	for (int j = argc - 1; j >= 0; j--)
	{
		length = strlen(argv[j]) + 1;
		total_lengh = total_lengh + length;
		*esp -= length;
		strlcpy(*esp, argv[j], length);
		argv[j] = *esp;
	}

	if (total_lengh % 4 != 0)
		*esp -= 4 - (total_lengh % 4);

	*esp -= 4;
	**(unsigned**)esp = 0;

	for (int k = argc - 1; k >= 0; k--) {
		*esp -= 4;
		**(unsigned int**)esp = (unsigned int)argv[k];
	}

	// push argv address
	*esp -= 4;
	**(unsigned int**)esp = (unsigned int)(*esp) + 4;

	*esp -= 4;
	**(unsigned int**)esp = argc;

	*esp -= 4;
	**(unsigned int**)esp = 0;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);


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


static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *knpage = palloc_get_page (PAL_USER);
      if (knpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, knpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (knpage);
          return false; 
        }
      memset (knpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, knpage, writable)) 
        {
          palloc_free_page (knpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool sunggong = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
	  sunggong = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (sunggong)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return sunggong;
}


static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *th = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (th->pagedir, upage) == NULL
          && pagedir_set_page (th->pagedir, upage, kpage, writable));
}

