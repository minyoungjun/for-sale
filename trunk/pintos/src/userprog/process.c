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
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *temp, *name, *aux;
  tid_t tid;

  /* Make a copy of "FILE_NAME".
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
	temp = palloc_get_page(0);
  if (fn_copy == NULL || temp == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
	strlcpy (temp, file_name, PGSIZE);
	name = strtok_r (temp, " ", &aux);

	struct child_process *child = (struct child_process *)malloc(sizeof(struct child_process));
	if (child == NULL) {
		palloc_free_page(fn_copy);
		palloc_free_page(temp);
		thread_exit();
	}
	sema_init(&child->sema, 0);

	struct exec_arg *args = (struct exec_arg *)malloc(sizeof(struct exec_arg));
	if (args == NULL) {
		palloc_free_page(fn_copy);
		palloc_free_page(temp);
		free(child);
		thread_exit();
	}
	args->filename = fn_copy;
	sema_init(&args->sema, 0);
	args->success = false;

  /* Create a new thread to execute FILE_NAME. */
	lock_acquire(&thread_current()->lock);
  tid = thread_create (name, PRI_DEFAULT, start_process, args);
	child->pid_t = tid;
	if (tid != TID_ERROR)
		list_push_front(&thread_current()->childs, &child->elem);
	lock_release(&thread_current()->lock);

	palloc_free_page(temp);

  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy);
		free(child);
		free(args);
	} else {
		sema_down(&args->sema);
		if (!args->success)
			tid = TID_ERROR;
		free(args);
	}

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
	struct exec_arg *args = file_name_;
  char *file_name = args->filename;
  struct intr_frame if_;
  bool success;

	thread_current()->is_userprog = true;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
	args->success = success;
	sema_up(&args->sema);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer %esp to our stack frame
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
  struct thread *cur = thread_current();
	struct child_process *child;
	int ret;

	lock_acquire(&cur->lock);
	child = find_child(child_tid, cur);
	lock_release(&cur->lock);

	if (child == NULL)
		return -1;

	sema_down(&child->sema);

	lock_acquire(&cur->lock);
	list_remove(&child->elem);
	lock_release(&cur->lock);

	ret = child->exit_status;
	free(child);
	return ret;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
	struct open_file *open_file;
	struct child_process *child = NULL;
	int ret;

	//이 스레드의 모든 open file을 close
	while (!list_empty(&cur->open_files)) {
		open_file = list_entry(list_pop_front(&cur->open_files), struct open_file, elem);
		file_close(open_file->fd);
		free(open_file);
	}

	if (cur->parent != NULL) {
		lock_acquire(&cur->parent->lock);
		child = find_child(cur->tid, cur->parent);
		lock_release(&cur->parent->lock);
	}

	if (cur->is_userprog) {
		if (!cur->exited_by_exit_call) {
			printf("%s: exit(-1)\n", cur->name);
			child->exit_status = -1;
		}
		else {
			printf("%s: exit(%d)\n", cur->name, cur->exit_status);
			child->exit_status = cur->exit_status;
		}
	}
/*
	if (cur->file != NULL) {
		lock_acquire(&lock_syscall);
		file_allow_write(cur->file);
		file_close(cur->file);
		lock_release(&lock_syscall);
		cur->file = NULL;
	}
*/

#ifdef VM
	raFTable(thread_current());
	destroyST();
#endif

  /* Destroys the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
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

	if (child != NULL)
		sema_up(&child->sema);
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

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directories. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

	// Argument Parsing 하기
	struct list argv;
	int argc;
	if ( !arg_parse (file_name, &argv, &argc) )
		return (success = false);

	/* 파일명 추출 : argv[]에서 파일명을 pop하는게 아니다.
		 단순히 읽어올 뿐이다.*/
	struct parameter *new_name;
	new_name = list_entry(list_back(&argv), struct parameter, elem); 

  /* Open executable file. */
	lock_acquire(&lock_syscall);
  file = filesys_open (new_name->str);
	lock_release(&lock_syscall);
	t->file = file;

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", new_name->str);
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
      printf ("load: %s: error loading executable\n", new_name->str);
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
                  /* Entirely zero
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
  if (!setup_stack (esp))
    goto done;

	// Argument Pushing
	*esp = arg_push(&argv, argc);
	// 아래는 pushing이 제대로 이뤄졌는지를 확인하는 함수
	//hex_dump ((int)(*esp), *esp, (size_t)100, true);


  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
/*
	lock_acquire(&lock_syscall);
	file_deny_write(file);
	lock_release(&lock_syscall);
*/
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}


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
  /*if (phdr->p_vaddr < PGSIZE)
    return false;*/

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
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifdef VM
	uint32_t cnt_loaded_pages = 0;
#else
  file_seek (file, ofs);
#endif

  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
			// 매 페이지를 요구페이징 해야한다.
			struct page *sup_page = (struct page *)malloc(sizeof(struct page));
			if (sup_page == NULL) {
				printf("Error! : load_segment(): malloc sup_page\n");
				return false;
			}

			sup_page->page = upage;
			sup_page->writable = writable;
			sup_page->pg = PAG_EXEC;
			sup_page->location = file;
			sup_page->read_b = page_read_bytes;
			sup_page->zero_b = page_zero_bytes;
			sup_page->ofs = (uint32_t)ofs + (cnt_loaded_pages * (uint32_t)PGSIZE);
			cnt_loaded_pages++;

			if ((uint32_t)thread_current()->max_code_seg_addr
						< ((uint32_t)upage + (uint32_t)PGSIZE))
				thread_current()->max_code_seg_addr = 
						(void *)((uint32_t)upage + (uint32_t)PGSIZE);

			lock_acquire(&thread_current()->lock_spt);
			list_push_back(&thread_current()->sup_page_table, &sup_page->elem);
			lock_release(&thread_current()->lock_spt);
#else
      /* Get a page from memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }
#endif

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
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));

}



/* file_name_을 parsing한다. 
	 strtok_r()을 이용하면 원본 문자열(file_name_)이 손상되므로 
	 char* file_name에 복사하여 작업한다.
*/
bool arg_parse (const char *file_name_, struct list *argv, int *argc)
{
	char *file_name, *token, *save_ptr;
	struct parameter *param;

	list_init(argv);
	
	// file_name_을 file_name으로 복사
	if ( (file_name = (char *) malloc(sizeof(char) * (strlen(file_name_)))) == NULL )
		return false;
	strlcpy(file_name, file_name_, strlen(file_name_)+1);

	// 토큰화하기
	for (token = strtok_r(file_name, " ", &save_ptr) ; token != NULL ;
			 token = strtok_r(NULL, " ", &save_ptr))
	{
		param = (struct parameter*) malloc(sizeof(struct parameter));
		param->str = (char *) malloc(sizeof(char) * (strlen(token)));
		strlcpy(param->str, token, strlen(token)+1);
		list_push_front(argv, &(param->elem));  //right-to-left calling convention을 위해 push_front
		(*argc)++;
	}

	free(file_name);
	return true;
}


/* 메모리에 인자를 push한다. */
void *arg_push (struct list *argv, const int argc)
{
	/* sp는 이 함수 내에서만 사용할 stack pointer
		 user stack은 PHYS_BASE에서 시작하므로 아래와 같이 지정 */
	char *sp = (char *) PHYS_BASE;
	int len;
	struct list argv_addr;      //argv[]들의 주소를 저장할 리스트
	struct parameter *param;	  //param과 addr는 list_entry의 리턴값을 저장하기 위함
	struct argaddress *addr;		

	list_init(&argv_addr);
	sp--;

	while ( !list_empty(argv) )
	{
		param = list_entry( list_pop_front(argv), struct parameter, elem );
		len = strnlen(param->str, 128);  //인자의 길이를 128bytes로 제한
		
		// 인자를 push
		for ( ; len >= 0 ; len-- ) {
			asm ("movb %1, %0; 1:"
						: "=m" (*sp)
						: "r" (param->str[len]));

			if (len == 0) {
				addr = (struct argaddress *) malloc(sizeof(struct argaddress));
				addr->arg = (char *)sp;
				list_push_back(&argv_addr, &(addr->elem));
			}
			sp--;
		}

		free(param);
	}


	// word align 설정
	uint8_t x = 0;
	while ( ((uint32_t)sp % 4) != 0 )		
	{
		sp--;
		asm ("movb %1, %0; 1:"
					: "=m" (*sp)
					: "r" (x));
	}

	// NULL push
	sp -= 4;
	uint32_t n = 0;
	asm ("movl %1, %0; 1:"
				: "=m" (*sp)
				: "r" (n));

	// 인자들의 주소 push
	while ( !list_empty(&argv_addr) )
	{
		sp -= 4;
		addr = list_entry( list_pop_front(&argv_addr), struct argaddress, elem);
		asm ("movl %1, %0; 1:"
					: "=m" (*sp)
					: "r" (addr->arg));
		free(addr);
	}

	// 1st arg (argv[1])의 주소를 push
	char *temp_sp = (char *)sp;
	sp -= 4;
	asm ("movl %1, %0; 1:"
				: "=m" (*sp)
				: "r" (temp_sp));

	// 인자의 개수 push
	sp -= 4;
	asm ("movl %1, %0; 1:"
				: "=m" (*sp)
				: "r" (argc));

	// return address push
	sp -= 4;
	asm ("movl %1, %0; 1:"
				: "=m" (*sp)
				: "r" (n));  // n == 0

	return sp;
}

struct child_process *find_child (tid_t pid, struct thread *t)
{
	struct list_elem *cur;
	struct child_process *child;
	
	for (cur = list_begin(&t->childs) ; cur != list_end(&t->childs) ; cur = list_next(cur)) {
		child = list_entry(cur, struct child_process, elem);
		if (child->pid_t = pid)
			return child;
	}
	return NULL;
}
